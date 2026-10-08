/*
 * T9-VEC 卡端：KNC(IMCI) 显式 intrinsics 优化 GEMM
 * ============================================================================
 * 为什么必须这样写（k1om GCC 5.1 + Knights Corner 实测结论）：
 *
 * 1) k1om 后端完全不自动向量化。
 *    -O0/-O2/-O3/-march=knc 生成的代码里 vfmadd 计数为 0，全是标量指令；
 *    -march=knc 下 -Q --help=target 显示 mavx512f 仍是 disabled。
 *    想拿到 512-bit 向量指令，唯一办法是手写 _mm512_* intrinsics。
 *
 * 2) intrinsics 需要 -mavx512f 才可见，但 -mavx512f 会把目标切到通用
 *    AVX-512F，于是**任何标量浮点代码**都会生成 xmm 形式的 vaddss /
 *    vcvtsi2ss / vmovsd，而 KNC 没有 xmm/ymm 寄存器，k1om 汇编器直接报错。
 *
 * 3) KNC 的若干指令集缺口（依据 Phi ISA Reference Manual 逐条核对）：
 *      - 没有 vcvtdq2ps（int->float 转换叫 VCVTFXPNTDQ2PS）
 *      - 没有 64 位整数向量指令（vpaddq/vpsllq/vpsubq 全无）
 *      - 没有非对齐向量访存（无 vmovups/vmovupd，只有 VLOADUNPACK/VPACKSTORE）
 *      - 没有条件搬移（无 cmov）——因此本文件里禁止出现 `? :` 三元运算符，
 *        否则 GCC 会生成 cmova 而汇编失败。这条也解释了历史上
 *        "-O2 的 sink 在卡上必崩"以及更早的 cmova/cmovbe 报错。
 *
 * 由此得到本文件的铁律：**全文件不得出现任何标量浮点运算**。
 *   - 输入数据：全部来自 .rodata 常量表，不做任何数值计算/类型转换
 *   - 计时：只用 gettimeofday 的整数微秒，回传 long long，GFLOPS 由宿主算
 *   - 校验和：用 _mm512_add_ps 做全矩阵向量累加后整块 store，不做标量水平归约
 *
 * 编译：k1om_cxx -O2 -mavx512f -fopenmp -rdynamic ...
 * ============================================================================
 */
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <omp.h>
#include <immintrin.h>

#include <intel-coi/sink/COIPipeline_sink.h>
#include <intel-coi/sink/COIProcess_sink.h>

/* 微内核尺寸：可用 -DMR32= / -DMR64= / -DJSB32= / -DJSB64= 覆盖，便于扫参数。
 * 以下默认值是在 7120P + N=2048 + 244 线程下实测扫描出来的最优点：
 * MR 不是越大越好——MR32=16/MR64=8 时累加器把 zmm 占满，导致溢出与调度变差，
 * 实测明显慢于 MR32=8/MR64=4。 */
#ifndef MR32
#define MR32 8
#endif
#ifndef MR64
#define MR64 4
#endif
#ifndef JSB32
#define JSB32 32
#endif
#ifndef JSB64
#define JSB64 16
#endif

#define NR32 16     /* FP32: 一个 zmm = 16 float  */
#define NR64 8      /* FP64: 一个 zmm = 8  double */

/* --------------------------------------------------------------- 结构体 */
/* 与 gemm_vec_host.cpp 必须逐字节一致。返回区只放整数时间与向量车道，
 * 不放任何标量浮点结果（那会迫使卡端做标量浮点运算）。
 * lanes 必须是首个成员：结构体整体 aligned(64) 后 _mm512_store_ps 才能直接
 * 落进去——KNC 没有非对齐向量存储，落到未对齐地址会直接汇编失败。 */
struct VParams {
    uint32_t N;
    uint32_t threads;
    uint32_t dtype;     /* 0 = FP32, 1 = FP64 */
    uint32_t pad;
};

struct VResult {
    float    lanes[16];     /* 全矩阵和的向量车道（64B）；FP64 时按 8 个 double 解释 */
    int64_t  time_us;       /* 预热后的计时（整数微秒） */
    int64_t  time_us_cold;  /* 首轮，含 OpenMP 线程场创建等一次性开销 */
    uint32_t N;
    uint32_t threads;
    uint32_t tiles;         /* 执行的微内核个数，用于确认分工 */
    int32_t  success;
} __attribute__((aligned(64)));

/* --------------------------------------------------- 常量表（全 .rodata） */
/* 数据完全由常量表提供：不做 int->float 转换、不做整数向量运算。
 * 表值 1 + k/64 全部精确可表示，卡端与宿主的参考实现逐位一致。 */
#define GTAB_LEN 64

static const float GTAB_F[GTAB_LEN] __attribute__((aligned(64))) = {
    1.0f,        1.015625f,   1.03125f,    1.046875f,   1.0625f,     1.078125f,
    1.09375f,    1.109375f,   1.125f,      1.140625f,   1.15625f,    1.171875f,
    1.1875f,     1.203125f,   1.21875f,    1.234375f,   1.25f,       1.265625f,
    1.28125f,    1.296875f,   1.3125f,     1.328125f,   1.34375f,    1.359375f,
    1.375f,      1.390625f,   1.40625f,    1.421875f,   1.4375f,     1.453125f,
    1.46875f,    1.484375f,   1.5f,        1.515625f,   1.53125f,    1.546875f,
    1.5625f,     1.578125f,   1.59375f,    1.609375f,   1.625f,      1.640625f,
    1.65625f,    1.671875f,   1.6875f,     1.703125f,   1.71875f,    1.734375f,
    1.75f,       1.765625f,   1.78125f,    1.796875f,   1.8125f,     1.828125f,
    1.84375f,    1.859375f,   1.875f,      1.890625f,   1.90625f,    1.921875f,
    1.9375f,     1.953125f,   1.96875f,    1.984375f
};

/* B 用逆向表：避免 A/B 同模式导致索引类 bug 互相抵消 */
static const float GTAB_FR[GTAB_LEN] __attribute__((aligned(64))) = {
    1.984375f,   1.96875f,    1.953125f,   1.9375f,     1.921875f,   1.90625f,
    1.890625f,   1.875f,      1.859375f,   1.84375f,    1.828125f,   1.8125f,
    1.796875f,   1.78125f,    1.765625f,   1.75f,       1.734375f,   1.71875f,
    1.703125f,   1.6875f,     1.671875f,   1.65625f,    1.640625f,   1.625f,
    1.609375f,   1.59375f,    1.578125f,   1.5625f,     1.546875f,   1.53125f,
    1.515625f,   1.5f,        1.484375f,   1.46875f,    1.453125f,   1.4375f,
    1.421875f,   1.40625f,    1.390625f,   1.375f,      1.359375f,   1.34375f,
    1.328125f,   1.3125f,     1.296875f,   1.28125f,    1.265625f,   1.25f,
    1.234375f,   1.21875f,    1.203125f,   1.1875f,     1.171875f,   1.15625f,
    1.140625f,   1.125f,      1.109375f,   1.09375f,    1.078125f,   1.0625f,
    1.046875f,   1.03125f,    1.015625f,   1.0f
};

static const double GTAB_D[GTAB_LEN] __attribute__((aligned(64))) = {
    1.0,         1.015625,    1.03125,     1.046875,    1.0625,      1.078125,
    1.09375,     1.109375,    1.125,       1.140625,    1.15625,     1.171875,
    1.1875,      1.203125,    1.21875,     1.234375,    1.25,        1.265625,
    1.28125,     1.296875,    1.3125,      1.328125,    1.34375,     1.359375,
    1.375,       1.390625,    1.40625,     1.421875,    1.4375,      1.453125,
    1.46875,     1.484375,    1.5,         1.515625,    1.53125,     1.546875,
    1.5625,      1.578125,    1.59375,     1.609375,    1.625,       1.640625,
    1.65625,     1.671875,    1.6875,      1.703125,    1.71875,     1.734375,
    1.75,        1.765625,    1.78125,     1.796875,    1.8125,      1.828125,
    1.84375,     1.859375,    1.875,       1.890625,    1.90625,     1.921875,
    1.9375,      1.953125,    1.96875,     1.984375
};

static const double GTAB_DR[GTAB_LEN] __attribute__((aligned(64))) = {
    1.984375,    1.96875,     1.953125,    1.9375,      1.921875,    1.90625,
    1.890625,    1.875,       1.859375,    1.84375,     1.828125,    1.8125,
    1.796875,    1.78125,     1.765625,    1.75,        1.734375,    1.71875,
    1.703125,    1.6875,      1.671875,    1.65625,     1.640625,    1.625,
    1.609375,    1.59375,     1.578125,    1.5625,      1.546875,    1.53125,
    1.515625,    1.5,         1.484375,    1.46875,     1.453125,    1.4375,
    1.421875,    1.40625,     1.390625,    1.375,       1.359375,    1.34375,
    1.328125,    1.3125,      1.296875,    1.28125,     1.265625,    1.25,
    1.234375,    1.21875,     1.203125,    1.1875,      1.171875,    1.15625,
    1.140625,    1.125,       1.109375,    1.09375,     1.078125,    1.0625,
    1.046875,    1.03125,     1.015625,    1.0
};

/* 表偏移一律是 16 的倍数（低 4 位为 0），即 64 字节对齐。
 * GCC 的值域分析证明不了 (x & 63) 仍是 16 的倍数，故显式告知。 */
#define TABP(TAB, OFF) ((const void *)__builtin_assume_aligned(&(TAB)[(OFF)], 64))

/* ----------------------------------------------------------- 数据初始化 */
/* A 用「带填充的行主序」存放：lda = N + PAD。
 * 这是关键优化之一。微内核一次要同时持有 MR 行的当前元素，行距 = N*4 字节。
 * N=2048 时行距正好 8192 = 2^13，而 32KB/8路/64B 的 L1 组索引是地址位 6..11，
 * 于是 MR 行的组索引完全相同，8 路只能容纳 8 行，其余每次访问都冲突缺失——
 * 单核实测只有理论峰值的 13.6%。把行距改成非 2 的幂（+16 个 float = +64B）
 * 后各行组索引彼此错开，冲突消失，单核吞吐提升约 3 倍。
 * 填充区不参与计算，逻辑值仍是 TAB[...]，所以宿主参考实现无需改动。 */
#define PAD32 16    /* float ：行距 +64B */
#define PAD64 8     /* double：行距 +64B */

/* 逻辑数据模式（宿主 gemm_vec_host.cpp 必须一致）：
 *   A[i][k] = 1 + ( (k + 16*(i%4)) % 64 ) / 64
 *   B[k][j] = 1 + ( 63 - ((j + 16*(k%4)) % 64) ) / 64
 * 行内偏移取 %64 后仍是 16 的倍数，所以每次都是表的 64B 对齐切片。
 * 之所以让模式同时随行、列变化：若只依赖扁平下标 i*N+k，而 N 又是 64 的倍数，
 * 则每一行的值完全相同，行索引写错也照样通过校验——那样的测试是假的。 */
static void init32_A(float *A, uint32_t N, uint32_t lda)
{
    if (((N | lda) & 15u) != 0) __builtin_unreachable();
    for (uint32_t i = 0; i < N; i++) {
        float *row = (float *)__builtin_assume_aligned(A + (size_t)i * lda, 64);
        const uint32_t rot = 16u * (i & 3u);
        for (uint32_t k = 0; k + 16 <= N; k += 16)
            _mm512_store_ps(row + k, _mm512_load_ps(TABP(GTAB_F, (k + rot) & 63u)));
    }
}

static void init32_B(float *B, uint32_t N)
{
    for (uint32_t k = 0; k < N; k++) {
        float *row = (float *)__builtin_assume_aligned(B + (size_t)k * N, 64);
        const uint32_t rot = 16u * (k & 3u);
        for (uint32_t j = 0; j + 16 <= N; j += 16)
            _mm512_store_ps(row + j, _mm512_load_ps(TABP(GTAB_FR, (j + rot) & 63u)));
    }
}

static void init64_A(double *A, uint32_t N, uint32_t lda)
{
    if (((N | lda) & 7u) != 0) __builtin_unreachable();
    for (uint32_t i = 0; i < N; i++) {
        double *row = (double *)__builtin_assume_aligned(A + (size_t)i * lda, 64);
        const uint32_t rot = 8u * (i & 7u);
        for (uint32_t k = 0; k + 8 <= N; k += 8)
            _mm512_store_pd(row + k, _mm512_load_pd(TABP(GTAB_D, (k + rot) & 63u)));
    }
}

static void init64_B(double *B, uint32_t N)
{
    for (uint32_t k = 0; k < N; k++) {
        double *row = (double *)__builtin_assume_aligned(B + (size_t)k * N, 64);
        const uint32_t rot = 8u * (k & 7u);
        for (uint32_t j = 0; j + 8 <= N; j += 8)
            _mm512_store_pd(row + j, _mm512_load_pd(TABP(GTAB_DR, (j + rot) & 63u)));
    }
}

/* ------------------------------------------------- B 面板打包（关键优化） */
/* B 是行主序，微内核固定 j 条带、沿 k 前进时要按 8KB 步长在 16MB 地址空间上
 * 跳跃（N=2048 时每 2 次 k 迭代换一个页）。把 B 重排成 [j条带][k][NR] 后，
 * 同一 j 条带的 k 方向变成完全连续的 64B 步进：
 *   - 每次 k 迭代恰好取满一整条 cache line，地址顺序前进，硬件预取器可用
 *   - 工作集从散布在 16MB 收缩为一段连续区间，TLB 压力消失
 * 重排只是 64B 块搬运（NR32=16 float 与 NR64=8 double 都正好 64B），
 * 不涉及元素级转置，纯对齐载入/存储即可，成本 O(N^2) 可忽略。 */
static void pack32(const float * __restrict__ B, float * __restrict__ Bp, uint32_t N)
{
    const float *Bc = (const float *)__builtin_assume_aligned(B, 64);
    float *Pc = (float *)__builtin_assume_aligned(Bp, 64);
    const uint32_t njs = N / NR32;
    for (uint32_t js = 0; js < njs; js++) {
        float *dst = Pc + (size_t)js * N * NR32;
        const float *src = Bc + (size_t)js * NR32;
        for (uint32_t k = 0; k < N; k++)
            _mm512_store_ps(dst + (size_t)k * NR32,
                            _mm512_load_ps(src + (size_t)k * N));
    }
}

static void pack64(const double * __restrict__ B, double * __restrict__ Bp, uint32_t N)
{
    const double *Bc = (const double *)__builtin_assume_aligned(B, 64);
    double *Pc = (double *)__builtin_assume_aligned(Bp, 64);
    const uint32_t njs = N / NR64;
    for (uint32_t js = 0; js < njs; js++) {
        double *dst = Pc + (size_t)js * N * NR64;
        const double *src = Bc + (size_t)js * NR64;
        for (uint32_t k = 0; k < N; k++)
            _mm512_store_pd(dst + (size_t)k * NR64,
                            _mm512_load_pd(src + (size_t)k * N));
    }
}

/* --------------------------------------------- 编译期展开工具（模板递归） */
/* GCC 5 没有 #pragma GCC unroll，累加器数组必须靠模板递归强制展开，
 * 否则下标是运行时的，累加器落不到 zmm 而会溢出到栈上。 */
template <int R> struct UnrollPS {
    static inline void fma(__m512 *c, const float *pa, uint32_t lda, __m512 b, uint32_t k) {
        UnrollPS<R - 1>::fma(c, pa, lda, b, k);
        c[R - 1] = _mm512_fmadd_ps(_mm512_set1_ps(pa[(size_t)(R - 1) * lda + k]), b, c[R - 1]);
    }
    static inline void zero(__m512 *c) {
        UnrollPS<R - 1>::zero(c);
        c[R - 1] = _mm512_setzero_ps();
    }
    static inline void store(float *pc, uint32_t ldc, const __m512 *c) {
        UnrollPS<R - 1>::store(pc, ldc, c);
        _mm512_store_ps(pc + (size_t)(R - 1) * ldc, c[R - 1]);
    }
};
template <> struct UnrollPS<0> {
    static inline void fma(__m512 *, const float *, uint32_t, __m512, uint32_t) {}
    static inline void zero(__m512 *) {}
    static inline void store(float *, uint32_t, const __m512 *) {}
};

template <int R> struct UnrollPD {
    static inline void fma(__m512d *c, const double *pa, uint32_t lda, __m512d b, uint32_t k) {
        UnrollPD<R - 1>::fma(c, pa, lda, b, k);
        c[R - 1] = _mm512_fmadd_pd(_mm512_set1_pd(pa[(size_t)(R - 1) * lda + k]), b, c[R - 1]);
    }
    static inline void zero(__m512d *c) {
        UnrollPD<R - 1>::zero(c);
        c[R - 1] = _mm512_setzero_pd();
    }
    static inline void store(double *pc, uint32_t ldc, const __m512d *c) {
        UnrollPD<R - 1>::store(pc, ldc, c);
        _mm512_store_pd(pc + (size_t)(R - 1) * ldc, c[R - 1]);
    }
};
template <> struct UnrollPD<0> {
    static inline void fma(__m512d *, const double *, uint32_t, __m512d, uint32_t) {}
    static inline void zero(__m512d *) {}
    static inline void store(double *, uint32_t, const __m512d *) {}
};

/* ----------------------------------------------------------- FP32 微内核 */
/* 每次 k 迭代：1 次 B 向量载入 + MR32 次 A 标量广播 + MR32 次 FMA。
 * 实测循环体就是 MR32 个 vbroadcastss + MR32 个 vfmadd231ps，
 * 已是 KNC 上这类 GEMM 的教科书形态。 */
static inline void micro32p(const float * __restrict__ A, uint32_t lda,
                            const float * __restrict__ Bp, uint32_t N,
                            float * __restrict__ C, uint32_t ldc,
                            uint32_t i0, uint32_t js)
{
    __m512 c[MR32];
    UnrollPS<MR32>::zero(c);
    const float *pa = A + (size_t)i0 * lda;
    const float *pb = Bp + (size_t)js * N * NR32;
    for (uint32_t k = 0; k < N; k++) {
        __m512 b = _mm512_load_ps(pb + (size_t)k * NR32);
        UnrollPS<MR32>::fma(c, pa, lda, b, k);
    }
    UnrollPS<MR32>::store(C + (size_t)i0 * ldc + (size_t)js * NR32, ldc, c);
}

/* ----------------------------------------------------------- FP64 微内核 */
static inline void micro64p(const double * __restrict__ A, uint32_t lda,
                            const double * __restrict__ Bp, uint32_t N,
                            double * __restrict__ C, uint32_t ldc,
                            uint32_t i0, uint32_t js)
{
    __m512d c[MR64];
    UnrollPD<MR64>::zero(c);
    const double *pa = A + (size_t)i0 * lda;
    const double *pb = Bp + (size_t)js * N * NR64;
    for (uint32_t k = 0; k < N; k++) {
        __m512d b = _mm512_load_pd(pb + (size_t)k * NR64);
        UnrollPD<MR64>::fma(c, pa, lda, b, k);
    }
    UnrollPD<MR64>::store(C + (size_t)i0 * ldc + (size_t)js * NR64, ldc, c);
}

/* -------------------------------------------- 全矩阵和的向量累加校验和 */
/* 只做向量加法 + 整块 store，绝不做标量水平归约（会生成非法的 vaddss）。
 * 目标缓冲必须 64B 对齐：KNC 没有非对齐向量存储。 */
static void sum32(const float *C, uint32_t N, float * __restrict__ out)
{
    float *o = (float *)__builtin_assume_aligned(out, 64);
    __m512 acc = _mm512_setzero_ps();
    for (uint32_t i = 0; i < N; i++) {
        const float *row = C + (size_t)i * N;
        for (uint32_t j = 0; j + 16 <= N; j += 16)
            acc = _mm512_add_ps(acc, _mm512_load_ps(row + j));
    }
    _mm512_store_ps(o, acc);
}

/* FP64 直接把 8 个 double 车道原样回传，宿主持有同一套解释规则 */
static void sum64(const double *C, uint32_t N, double * __restrict__ out)
{
    double *o = (double *)__builtin_assume_aligned(out, 64);
    __m512d acc = _mm512_setzero_pd();
    for (uint32_t i = 0; i < N; i++) {
        const double *row = C + (size_t)i * N;
        for (uint32_t j = 0; j + 8 <= N; j += 8)
            acc = _mm512_add_pd(acc, _mm512_load_pd(row + j));
    }
    _mm512_store_pd(o, acc);
}

/* ------------------------------------------------------------ 时间戳 */
static inline int64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec;
}

/* -------------------------------------------------- j 分块数自适应选择 */
/* 微内核每处理一个 j 条带都要把 MR 行 A 完整读一遍（MR*N*4 字节），
 * 所以 A 的总流量 = njs * (N/MR) * MR*N*4，N=2048 时高达 2.16GB，是访存流量
 * 的大头。让同一个任务在固定 i0 上连续跑 jsb 个 j 条带，A 行即可在 L1/L2 中
 * 被复用 jsb 次，A 流量按 jsb 倍下降；而并行任务数仍有 (njs/jsb)*(N/MR) 个。
 * 下限取 4 个 j 块：只切 2 块时 N=1024 的任务数刚好一个波次，静态调度下
 * 末尾波次严重不均，实测比 4 块方案慢一倍以上。
 * N 较小时自动退化为小 jsb，避免因 N % (NR*jsb) != 0 而拒绝执行。
 * 注意这里刻意不用 min()/三元：k1om 无 cmov，`? :` 会变成 cmova 而汇编失败，
 * 因此内层上界写成 js0 + jsb 并靠整除性保证不越界。 */
static uint32_t pick_jsb(uint32_t N, uint32_t NR, uint32_t maxjsb, uint32_t MR)
{
    const uint32_t njs = N / NR;
    for (uint32_t j = maxjsb; j > 1; j >>= 1) {
        if ((njs % j) != 0) continue;
        if ((njs / j) < 4) continue;
        if ((N / MR) * (njs / j) < 128) continue;
        return j;
    }
    return 1;
}

/* -------------------------------------------------------------- 一遍 GEMM */
/* schedule(static)：dynamic(1) 在 N=2048 时有 16384 次共享计数器原子操作，
 * 244 线程争抢同一个计数器，实测把固定开销推到 0.15 s 量级。
 * 微内核只写 C（不读 C），所以重复跑幂等，可以安全地做预热轮。 */
template <typename T> struct Pass;

template <> struct Pass<float> {
    static void run(const float * __restrict__ A, uint32_t lda,
                    const float * __restrict__ Bp,
                    float * __restrict__ C, uint32_t N, uint32_t jsb, uint32_t *tiles)
    {
        uint32_t t = 0;
        const uint32_t njs = N / NR32;
        const uint32_t njsb = njs / jsb;
        #pragma omp parallel for collapse(2) schedule(static) reduction(+:t)
        for (uint32_t jb = 0; jb < njsb; jb++)
            for (uint32_t i0 = 0; i0 < N; i0 += MR32) {
                const uint32_t js0 = jb * jsb;
                for (uint32_t js = js0; js < js0 + jsb; js++) {
                    micro32p(A, lda, Bp, N, C, N, i0, js);
                    t++;
                }
            }
        *tiles = t;
    }
};

template <> struct Pass<double> {
    static void run(const double * __restrict__ A, uint32_t lda,
                    const double * __restrict__ Bp,
                    double * __restrict__ C, uint32_t N, uint32_t jsb, uint32_t *tiles)
    {
        uint32_t t = 0;
        const uint32_t njs = N / NR64;
        const uint32_t njsb = njs / jsb;
        #pragma omp parallel for collapse(2) schedule(static) reduction(+:t)
        for (uint32_t jb = 0; jb < njsb; jb++)
            for (uint32_t i0 = 0; i0 < N; i0 += MR64) {
                const uint32_t js0 = jb * jsb;
                for (uint32_t js = js0; js < js0 + jsb; js++) {
                    micro64p(A, lda, Bp, N, C, N, i0, js);
                    t++;
                }
            }
        *tiles = t;
    }
};

/* ============================================================== COI 入口 */
extern "C" void GemmVecRun(uint32_t        in_BufferCount,
                           void          **in_ppBufferPointers,
                           uint64_t       *in_pBufferLengths,
                           void           *in_pMiscData,
                           uint16_t        in_MiscDataLength,
                           void           *in_pReturnValue,
                           uint16_t        in_ReturnValueLength)
{
    (void)in_BufferCount; (void)in_ppBufferPointers; (void)in_pBufferLengths;

    if (in_pMiscData == NULL || in_MiscDataLength < sizeof(VParams)) return;
    if (in_pReturnValue == NULL || in_ReturnValueLength < sizeof(VResult)) return;

    VParams p;
    memcpy(&p, in_pMiscData, sizeof(p));

    VResult r __attribute__((aligned(64)));
    memset(&r, 0, sizeof(r));
    r.N = p.N;
    r.threads = p.threads;
    r.success = 0;

    const uint32_t N = p.N;
    const size_t sz = (size_t)N * N;

    /* 向量微内核只要求 N 是 16 与 MR 的整数倍；j 分块数按 N 自适应选取。
     * 用 if/else 而非三元运算符：k1om 无 cmov。 */
    uint32_t MR;
    if (p.dtype == 0) MR = (uint32_t)MR32;
    else              MR = (uint32_t)MR64;
    if (N == 0 || (N % 16u) != 0 || (N % MR) != 0) {
        memcpy(in_pReturnValue, &r, sizeof(r));
        return;
    }

    omp_set_num_threads((int)p.threads);

    if (p.dtype == 0) {
        const uint32_t lda = N + PAD32;
        float *A  = (float *)_mm_malloc((size_t)N * lda * sizeof(float), 64);
        float *B  = (float *)_mm_malloc(sz * sizeof(float), 64);
        float *Bp = (float *)_mm_malloc(sz * sizeof(float), 64);
        float *C  = (float *)_mm_malloc(sz * sizeof(float), 64);
        if (!A || !B || !Bp || !C) {
            _mm_free(A); _mm_free(B); _mm_free(Bp); _mm_free(C);
            memcpy(in_pReturnValue, &r, sizeof(r));
            return;
        }

        init32_A(A, N, lda);
        init32_B(B, N);
        pack32(B, Bp, N);

        const uint32_t jsb = pick_jsb(N, NR32, JSB32, MR32);
        uint32_t tiles = 0;
        /* 预热轮：把 OpenMP 线程场创建/亲和性设置等一次性开销挪出计时窗口 */
        int64_t c0 = now_us();
        Pass<float>::run(A, lda, Bp, C, N, jsb, &tiles);
        int64_t c1 = now_us();

        int64_t t0 = now_us();
        Pass<float>::run(A, lda, Bp, C, N, jsb, &tiles);
        int64_t t1 = now_us();

        r.time_us_cold = c1 - c0;
        r.time_us = t1 - t0;
        r.tiles = tiles;
        sum32(C, N, r.lanes);
        r.success = 1;

        _mm_free(C); _mm_free(Bp); _mm_free(B); _mm_free(A);

    } else {
        const uint32_t lda = N + PAD64;
        double *A  = (double *)_mm_malloc((size_t)N * lda * sizeof(double), 64);
        double *B  = (double *)_mm_malloc(sz * sizeof(double), 64);
        double *Bp = (double *)_mm_malloc(sz * sizeof(double), 64);
        double *C  = (double *)_mm_malloc(sz * sizeof(double), 64);
        if (!A || !B || !Bp || !C) {
            _mm_free(A); _mm_free(B); _mm_free(Bp); _mm_free(C);
            memcpy(in_pReturnValue, &r, sizeof(r));
            return;
        }

        init64_A(A, N, lda);
        init64_B(B, N);
        pack64(B, Bp, N);

        const uint32_t jsb = pick_jsb(N, NR64, JSB64, MR64);
        uint32_t tiles = 0;
        int64_t c0 = now_us();
        Pass<double>::run(A, lda, Bp, C, N, jsb, &tiles);
        int64_t c1 = now_us();

        int64_t t0 = now_us();
        Pass<double>::run(A, lda, Bp, C, N, jsb, &tiles);
        int64_t t1 = now_us();

        r.time_us_cold = c1 - c0;
        r.time_us = t1 - t0;
        r.tiles = tiles;
        sum64(C, N, (double *)r.lanes);
        r.success = 1;

        _mm_free(C); _mm_free(Bp); _mm_free(B); _mm_free(A);
    }

    memcpy(in_pReturnValue, &r, sizeof(r));
}

int main(int, char **)
{
    COIPipelineStartExecutingRunFunctions();
    COIProcessWaitForShutdown();
    return 0;
}
