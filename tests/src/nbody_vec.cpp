/*
 * T7 卡端向量内核：KNC(IMCI) 显式 FP64 向量化的 N 体积分核
 * ============================================================================
 * 为什么单独一个编译单元：
 *   -mavx512f 会把整个编译单元切到通用 AVX-512F，于是**任何标量浮点运算**
 *   都会生成 xmm 形式指令，而 KNC 没有 xmm → 汇编失败（见 docs/N2）。
 *   本文件因此不出现任何标量浮点：所有算术都在 _mm512_* 里。
 *   初始条件（sin/cos）、动能、时序、校验和留在 nbody_sink.cpp，用 -O0 编译。
 *
 * KNC 的 FP64 超越函数缺口（依据 Phi ISA Reference Manual 逐条核对）：
 *   VSQRTPD / VDIVPD / VRSQRTPD / VRCPPD **全部不存在** —— KNC 没有硬件
 *   FP64 开方、除法、倒数、倒数平方根。FP32 侧有 VRSQRT23PS/VRCP23PS，
 *   FP64 侧只有 VGETEXPPD / VGETMANTPD。工具链亦证实：vcvtpd2dq、
 *   vrndscalepd、vrsqrt28pd、vrsqrt14pd 都被 k1om 汇编器拒绝。
 *   所以 sqrt()/除法在卡上是 libm 软实现 —— 实测每对粒子约 285 个周期，
 *   这正是 T7 此前只有 2.7 GFLOPS 的根因。
 *
 * 本文件自己实现 1/sqrt(x)：
 *   x = m * 2^e, m in [1,2)
 *   1/sqrt(x) = q^e * m^(-1/2),  q = 1/sqrt(2)
 *     - q^e：用 VGETEXPPD 取整数指数 e，k = e + 12，再做 4 步二进制
 *            条件缩放（掩码乘 + 掩码减），纯 SIMD、无分支、无整数转换
 *     - m^(-1/2)：5 次极小极大多项式（相对误差 1.17e-5），系数已预先
 *            乘上 2^6 以抵消 q^12
 *     - 再做 2 次牛顿迭代 → 相对误差 6.2e-20（完全收敛，约 1 ulp）
 *
 * 向量化方向选择「按 i 向量化、按 j 广播」而不是反过来：
 *   - 每个 i 的 j 累加顺序与标量参考实现**完全一致**（同一条车道顺序累加），
 *     所以舍入差异只来自 rsqrt 本身的 1 ulp，不来自求和重排
 *   - 结果天然是 8 个一组，用 _mm512_store_pd 对齐写回，
 *     不需要水平归约，也就不会生成 vpackstorelpd（卡上会崩的那条）
 *
 * 构建见 t7_nbody.sh：本文件 -O2 -mavx512f，nbody_sink.cpp -O0。
 * ============================================================================
 */
#include <stdint.h>
#include <immintrin.h>
#include <omp.h>

#define NB_VEC 8    /* FP64: 一个 zmm = 8 double */

/* ---------------------------------------------------------------------------
 * 预取。消融实验（把 rsqrt 整条链换成常数）只快 2.1×，说明**一半时间在访存**：
 * 剩下的变体只剩 4 次广播 + 3 sub + 3 FMA（约 14 条指令），却仍要约 111 个
 * 周期/轮（8 周期/指令）——顺序核在等加载。
 *
 * KNC 没有 SSE 的 prefetcht0：_mm_prefetch / __builtin_prefetch 都会被汇编器
 * 拒绝（Error: `prefetcht0' is not supported on `k1om'）。手册给的是 MVEX 形式
 * 的 VPREFETCH0/1/2/NTA，只能用内联汇编写。手册还注明「命中 L1 时硬件丢弃
 * 该预取」，所以 vprefetch0 正是要用的提示。
 *
 * 预取地址越界是无害的（预取不会触发异常），所以不需要边界判断。
 * --------------------------------------------------------------------------- */
#ifndef NB_PF
#define NB_PF 16        /* 预取距离，单位是 j；0 = 关闭 */
#endif

/* ===========================================================================
 * MVEX 嵌入广播：把「从内存广播一个 double」折进算术指令本身。
 *
 * ISA 手册里 VMULPD/VSUBPD 的操作数写作 Sf64(zmm3/mt)，其中 mt 允许
 * `{1to8}` 形式——只读一个 8 字节元素并复制到 8 条车道。手册的 MVEX 表：
 *     001  [rax] {1to8}  8
 * 它能把内层循环里的 vbroadcastsd 全部消掉。
 *
 * 两个坑：
 *  1) GCC 从不自动生成它。`_mm512_fmadd_pd(_mm512_set1_pd(p[3]), a, a)`
 *     产出的是 vbroadcastsd + vfmadd132pd 两条。
 *  2) 内联汇编里**花括号会被 GCC 的 asm 模板吞掉**：写 `(%2){1to8}` 时
 *     GCC 生成的是 `(%rdi)1to8`（少了花括号），汇编器报
 *     ``junk `1to8' after expression``。必须写成 `%{1to8%}`，
 *     实测生成 `vmulpd (%rdi){1to8}, %zmm0, %zmm0`，编码前缀字节 0x18
 *     而普通形式是 0x08——确实是两条不同的指令。
 *
 * 注意 MVEX 的内存操作数只能落在 Intel 记法的最后一个源操作数（AT&T 的第一个），
 * 所以 sub 只能算出 `a - broadcast(*p)`，不能反过来。调用方用 fnmadd 取代 fmadd
 * 把符号补回来——符号翻转在 IEEE 下是精确的，所以结果逐位不变。
 * =========================================================================== */
static inline __m512d sub_mem_pd(__m512d a, const double *p)
{
    __m512d r;
    __asm__("vsubpd (%2)%{1to8%}, %1, %0" : "=v"(r) : "v"(a), "r"(p));
    return r;                       /* a - broadcast(*p) */
}

static inline __m512d mul_mem_pd(__m512d a, const double *p)
{
    __m512d r;
    __asm__("vmulpd (%2)%{1to8%}, %1, %0" : "=v"(r) : "v"(a), "r"(p));
    return r;                       /* a * broadcast(*p) */
}

#ifndef NB_BCAST_FOLD
/* 实测（档位 6，best-of-3）：开=5204.68 / 关=5567.46 MPairs/s，开了反而慢 6.5%，
 * 而且受力核自检与校验和都一致，所以不是算错。默认关闭，开关保留以便复现。
 * 可能的解释：MVEX 形式编码更长（多一个前缀字节 + disp8），或广播-访存形式
 * 在访存端口上的行为不如独立的 vbroadcastsd + 寄存器操作数。 */
#define NB_BCAST_FOLD 0             /* 0 = 用独立的 vbroadcastsd（默认，更快） */
#endif

static inline void nb_prefetch0(const void *p)
{
    __asm__ volatile("vprefetch0 (%0)" :: "r"(p));
}

/* q = 2^-0.5；q^e 的二进制缩放常数 */
#define Q_ONE   0.70710678118654752440      /* q^1  = 2^-0.5 */
#define Q_TWO   0.50000000000000000000      /* q^2  = 2^-1   */
#define Q_FOUR  0.25000000000000000000      /* q^4  = 2^-2   */
#define Q_EIGHT 0.06250000000000000000      /* q^8  = 2^-4   */

/* k = e + EXP_OFF_D，取 12 使 k 落在 [0,15]（4 位二进制缩放够用）。
 * r2 >= soft2 = 1e-3 保证 e >= -10；r2 上界约 3 给 e <= 1。
 * 多项式整体乘 2^6，用来抵消 q^12 = 2^-6。
 * 注意 EXP_OFF_D 必须是字面量：写成 (double)EXP_OFF 会引入标量 int->double
 * 转换（vcvtsi2sd，xmm 形式），KNC 上直接汇编失败。 */
#define EXP_OFF_D 12.0

/* m^(-1/2) 在 m in [1,2) 上的 5 次极小极大多项式，已乘 64。
 * 相对误差 1.17e-5；2 次牛顿迭代后 6.2e-20。 */
#define PB0  1.458420823144776079e+02
#define PB1 -1.706815358164975293e+02
#define PB2  1.420908449938614524e+02
#define PB3 -6.957543232001837907e+01
#define PB4  1.833312835738097135e+01
#define PB5 -2.009833734602153754e+00

/* 二进制缩放的阈值与乘数，**必须用 8 车道常量数组 + vmovapd 加载**。
 * 原因：掩码运算（_mm512_mask_mul_pd / _mm512_mask_sub_pd）需要寄存器操作数，
 * 若写成 _mm512_set1_pd(字面量)，GCC 会生成
 *     vmovsd       .LCn(%rip), %xmm14     <- 非法：KNC 无 xmm
 *     vbroadcastsd %xmm14, %zmm30         <- 非法：KNC 无寄存器形式广播
 * 从数组整块加载则退化为 vmovapd（合法）。
 * 见 docs/N2 约束 2 与约束 3。 */
#define NKB 8
static const double NB_K[NKB + 3][NKB] __attribute__((aligned(64))) = {
    { 8.0, 8.0, 8.0, 8.0, 8.0, 8.0, 8.0, 8.0 },   /* 0 阈值 8 */
    { 4.0, 4.0, 4.0, 4.0, 4.0, 4.0, 4.0, 4.0 },   /* 1 阈值 4 */
    { 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0 },   /* 2 阈值 2 */
    { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 },   /* 3 阈值 1 / 常数 1 */
    { Q_EIGHT, Q_EIGHT, Q_EIGHT, Q_EIGHT, Q_EIGHT, Q_EIGHT, Q_EIGHT, Q_EIGHT }, /* 4 q^8 */
    { Q_FOUR,  Q_FOUR,  Q_FOUR,  Q_FOUR,  Q_FOUR,  Q_FOUR,  Q_FOUR,  Q_FOUR  }, /* 5 q^4 */
    { Q_TWO,   Q_TWO,   Q_TWO,   Q_TWO,   Q_TWO,   Q_TWO,   Q_TWO,   Q_TWO   }, /* 6 q^2 */
    { Q_ONE,   Q_ONE,   Q_ONE,   Q_ONE,   Q_ONE,   Q_ONE,   Q_ONE,   Q_ONE   }, /* 7 q^1 */
    { 1.5, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5 },   /* 8 牛顿迭代常数 1.5 */
    { 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5 },   /* 9 牛顿迭代常数 0.5 */
    { EXP_OFF_D, EXP_OFF_D, EXP_OFF_D, EXP_OFF_D,
      EXP_OFF_D, EXP_OFF_D, EXP_OFF_D, EXP_OFF_D } /* 10 k = e + 12 */
};
#define NK(i) _mm512_load_pd(NB_K[i])

/* 尾数多项式系数同样用 8 车道常量数组。
 * FMA 只有一个内存操作数，若两个操作数都是 _mm512_set1_pd(字面量)，
 * GCC 会把其中一个广播到寄存器 —— 又是 vmovsd + vbroadcastsd %xmm 的非法路径。 */
static const double NB_P[6][NKB] __attribute__((aligned(64))) = {
    { PB0, PB0, PB0, PB0, PB0, PB0, PB0, PB0 },
    { PB1, PB1, PB1, PB1, PB1, PB1, PB1, PB1 },
    { PB2, PB2, PB2, PB2, PB2, PB2, PB2, PB2 },
    { PB3, PB3, PB3, PB3, PB3, PB3, PB3, PB3 },
    { PB4, PB4, PB4, PB4, PB4, PB4, PB4, PB4 },
    { PB5, PB5, PB5, PB5, PB5, PB5, PB5, PB5 }
};
#define NP(i) _mm512_load_pd(NB_P[i])

/* VCMPPD 的谓词编码陷阱（手册 Table 6.3 / Operation 段）：
 * KNC 的 vcmppd 只使用立即数的低 3 位 IMM8[2:0]：
 *     000=eq 001=lt 010=le 011=unord 100=neq 101=nlt 110=nle 111=ord
 * 没有 gt/ge/ngt/nge —— 手册明确要求「交换操作数，改用 LT / LE」。
 * GCC 按通用 AVX-512 规则把 _CMP_GE_OS 编成立即数 13（0xd），
 * 低 3 位是 5（nlt），硬件直接报 invalid opcode（实测 trap invalid opcode）。
 * 所以这里一律写成 _mm512_cmp_pd_mask(阈值, k, _CMP_LE_OS)，即「阈值 <= k」，
 * 等价于「k >= 阈值」，编码为立即数 2，合法。 */

/* ===========================================================================
 * rsqrt 的第二种实现：用多项式取代整条掩码缩放链。
 *
 * 动机来自大档上的消融实测（见 N12）：把 4 步掩码缩放换成无掩码等价物
 * （语义错，但算子数相近），N=65536 上从 98.4 涨到 135.2 GFLOPS —— **+37%**。
 * 去掉的只有 4 条 vcmppd 和掩码本身，指令数只少 4 条，所以那 37% 不可能来自
 * 发射带宽（4/82 ≈ 5%），只能来自**掩码依赖链**：cmp→k、掩码乘、掩码减、
 * cmp→k…… 顺序核必须一步一步走完这条链，中间插不进别的独立工作。
 *
 * 替代方案：既然 VGETEXPPD 直接给出整数指数 e，就用一个以 e 为变量的多项式
 * 直接逼近 2^(-e/2)，和尾数多项式并行求值。两条链互相独立，天然有 ILP，
 * 且完全没有比较和掩码。
 *
 *   x = m · 2^e,  m in [1,2),  e in [-12, 4]
 *   1/sqrt(x) = 2^(-e/2) · m^(-1/2)
 *
 *   P(e) ≈ 2^(-e/2)   deg 9，相对误差 1.23e-5   （系数已把 u=-e/2 折算进 e）
 *   Q(m) ≈ m^(-1/2)   deg 5，相对误差 1.17e-5
 *   合成种子误差 2.40e-5 -> 1 次 NR 8.6e-10 -> 2 次 NR 1.1e-18（低于 double eps）
 *
 * 相比掩码版：31 条指令 -> 28 条，其中 4 条比较 + 8 条掩码运算全部消失。
 * =========================================================================== */
#define PE0 +9.999877406433109739e-01
#define PE1 -3.465712829254284366e-01
#define PE2 +6.007260903221120496e-02
#define PE3 -6.940960727462263530e-03
#define PE4 +5.980802553502567471e-04
#define PE5 -4.137146286399854753e-05
#define PE6 +2.616894770060334760e-06
#define PE7 -1.181455911334564762e-07
#define PE8 +2.490665876667130796e-11
#define PE9 -6.669878679314150682e-10

#define PM0 +2.278784505777788016e+00
#define PM1 -2.666905089023546616e+00
#define PM2 +2.220176770095757313e+00
#define PM3 -1.087120380106638473e+00
#define PM4 +2.864563174091905307e-01
#define PM5 -3.140377831156948574e-02

static const double NB_E[10][NKB] __attribute__((aligned(64))) = {
    { PE0, PE0, PE0, PE0, PE0, PE0, PE0, PE0 },
    { PE1, PE1, PE1, PE1, PE1, PE1, PE1, PE1 },
    { PE2, PE2, PE2, PE2, PE2, PE2, PE2, PE2 },
    { PE3, PE3, PE3, PE3, PE3, PE3, PE3, PE3 },
    { PE4, PE4, PE4, PE4, PE4, PE4, PE4, PE4 },
    { PE5, PE5, PE5, PE5, PE5, PE5, PE5, PE5 },
    { PE6, PE6, PE6, PE6, PE6, PE6, PE6, PE6 },
    { PE7, PE7, PE7, PE7, PE7, PE7, PE7, PE7 },
    { PE8, PE8, PE8, PE8, PE8, PE8, PE8, PE8 },
    { PE9, PE9, PE9, PE9, PE9, PE9, PE9, PE9 }
};
static const double NB_M[6][NKB] __attribute__((aligned(64))) = {
    { PM0, PM0, PM0, PM0, PM0, PM0, PM0, PM0 },
    { PM1, PM1, PM1, PM1, PM1, PM1, PM1, PM1 },
    { PM2, PM2, PM2, PM2, PM2, PM2, PM2, PM2 },
    { PM3, PM3, PM3, PM3, PM3, PM3, PM3, PM3 },
    { PM4, PM4, PM4, PM4, PM4, PM4, PM4, PM4 },
    { PM5, PM5, PM5, PM5, PM5, PM5, PM5, PM5 }
};
#define NE(i) _mm512_load_pd(NB_E[i])
#define NM(i) _mm512_load_pd(NB_M[i])

#ifndef NB_RSQRT_POLY
#define NB_RSQRT_POLY 0
#endif

#if NB_RSQRT_POLY
/* y = 1/sqrt(x)：多项式版，无比较、无掩码、无分支 */
static inline __m512d rsqrt_pd(__m512d x)
{
    const __m512d vone5 = NK(8);
    const __m512d vhalf = NK(9);

    const __m512d e = _mm512_getexp_pd(x);
    const __m512d m = _mm512_getmant_pd(x, _MM_MANT_NORM_1_2, _MM_MANT_SIGN_zero);

    /* P(e) ~ 2^(-e/2)：9 次 FMA 链 */
    __m512d pe = _mm512_fmadd_pd(NE(9), e, NE(8));
    pe = _mm512_fmadd_pd(pe, e, NE(7));
    pe = _mm512_fmadd_pd(pe, e, NE(6));
    pe = _mm512_fmadd_pd(pe, e, NE(5));
    pe = _mm512_fmadd_pd(pe, e, NE(4));
    pe = _mm512_fmadd_pd(pe, e, NE(3));
    pe = _mm512_fmadd_pd(pe, e, NE(2));
    pe = _mm512_fmadd_pd(pe, e, NE(1));
    pe = _mm512_fmadd_pd(pe, e, NE(0));

    /* Q(m) ~ m^(-1/2)：5 次 FMA 链，与上面完全独立 */
    __m512d qm = _mm512_fmadd_pd(NM(5), m, NM(4));
    qm = _mm512_fmadd_pd(qm, m, NM(3));
    qm = _mm512_fmadd_pd(qm, m, NM(2));
    qm = _mm512_fmadd_pd(qm, m, NM(1));
    qm = _mm512_fmadd_pd(qm, m, NM(0));

    __m512d y = _mm512_mul_pd(pe, qm);      /* 种子，相对误差 <= 2.40e-5 */

    const __m512d hx = _mm512_mul_pd(x, vhalf);
    __m512d t = _mm512_mul_pd(y, y);
    t = _mm512_fnmadd_pd(t, hx, vone5);
    y = _mm512_mul_pd(y, t);
    t = _mm512_mul_pd(y, y);
    t = _mm512_fnmadd_pd(t, hx, vone5);
    y = _mm512_mul_pd(y, t);
    return y;
}
#else
/* y = 1/sqrt(x)，x > 0。纯 SIMD，无标量浮点、无分支、无类型转换。 */
static inline __m512d rsqrt_pd(__m512d x)
{
    const __m512d vone  = NK(3);
    const __m512d vone5 = NK(8);
    const __m512d vhalf = NK(9);

    /* x = m * 2^e，m in [1,2) */
    const __m512d e = _mm512_getexp_pd(x);
    const __m512d m = _mm512_getmant_pd(x, _MM_MANT_NORM_1_2, _MM_MANT_SIGN_zero);

    /* k = e + 12，二进制分解出 q^k */
    __m512d k = _mm512_add_pd(e, NK(10));
    __m512d r = vone;
    __mmask8 mk;

    mk = _mm512_cmp_pd_mask(NK(0), k, _CMP_LE_OS);
    r = _mm512_mask_mul_pd(r, mk, r, NK(4));
    k = _mm512_mask_sub_pd(k, mk, k, NK(0));

    mk = _mm512_cmp_pd_mask(NK(1), k, _CMP_LE_OS);
    r = _mm512_mask_mul_pd(r, mk, r, NK(5));
    k = _mm512_mask_sub_pd(k, mk, k, NK(1));

    mk = _mm512_cmp_pd_mask(NK(2), k, _CMP_LE_OS);
    r = _mm512_mask_mul_pd(r, mk, r, NK(6));
    k = _mm512_mask_sub_pd(k, mk, k, NK(2));

    mk = _mm512_cmp_pd_mask(NK(3), k, _CMP_LE_OS);
    r = _mm512_mask_mul_pd(r, mk, r, NK(7));
    k = _mm512_mask_sub_pd(k, mk, k, NK(3));

    /* 尾数多项式（Horner），系数已含 2^6 抵消因子 */
    __m512d p = _mm512_fmadd_pd(NP(5), m, NP(4));
    p = _mm512_fmadd_pd(p, m, NP(3));
    p = _mm512_fmadd_pd(p, m, NP(2));
    p = _mm512_fmadd_pd(p, m, NP(1));
    p = _mm512_fmadd_pd(p, m, NP(0));

    __m512d y = _mm512_mul_pd(r, p);        /* 种子，相对误差 <= 1.17e-5 */

    /* 2 次牛顿迭代：y <- y * (1.5 - 0.5*x*y*y) */
    const __m512d hx = _mm512_mul_pd(x, vhalf);
    __m512d t = _mm512_mul_pd(y, y);
    t = _mm512_fnmadd_pd(t, hx, vone5);
    y = _mm512_mul_pd(y, t);
    t = _mm512_mul_pd(y, y);
    t = _mm512_fnmadd_pd(t, hx, vone5);
    y = _mm512_mul_pd(y, t);
    return y;
}
#endif  /* NB_RSQRT_POLY */

/* ===========================================================================
 * 两路同步（lockstep）求 1/sqrt：把两个 i 块的计算逐条交错。
 *
 * 动机来自反汇编实测：GCC **不会**把两次 rsqrt_pd 内联后的两条独立依赖链
 * 交错开——内层循环里块 0 的 vgetexppd 在 0x889，块 1 的 vgetexppd 在 0x96d，
 * 中间隔着块 0 整条链的约 30 条指令。KNC 是**顺序核**，一条指令停顿会挡住
 * 程序序里它后面的一切，所以等在块 0 的链上时，块 1 的指令压根还没进发射窗口。
 * 这正是「2 路展开只拿到 +4%」的原因：第二个块等于白加。
 *
 * 这里手工把两条链写成 a、b 交替，保证程序序里任意时刻都有另一条独立链可用。
 * 编译器无法把它重新串行化（数据依赖不允许），所以这个交错是可靠的。
 *
 * 实测见 docs/N12。
 * =========================================================================== */
#if NB_RSQRT_POLY
/* 多项式版不做 lockstep：它不是默认路径，且实测与掩码版打平 */
static inline void rsqrt_pd_2(__m512d xa, __m512d xb, __m512d *ya, __m512d *yb)
{
    *ya = rsqrt_pd(xa);
    *yb = rsqrt_pd(xb);
}
#else
static inline void rsqrt_pd_2(__m512d xa, __m512d xb, __m512d *outa, __m512d *outb)
{
    const __m512d vone  = NK(3);
    const __m512d vone5 = NK(8);
    const __m512d vhalf = NK(9);

    const __m512d ea = _mm512_getexp_pd(xa);
    const __m512d eb = _mm512_getexp_pd(xb);
    const __m512d ma = _mm512_getmant_pd(xa, _MM_MANT_NORM_1_2, _MM_MANT_SIGN_zero);
    const __m512d mb = _mm512_getmant_pd(xb, _MM_MANT_NORM_1_2, _MM_MANT_SIGN_zero);

    __m512d ka = _mm512_add_pd(ea, NK(10));
    __m512d kb = _mm512_add_pd(eb, NK(10));
    __m512d ra = vone, rb = vone;
    __mmask8 mka, mkb;

    mka = _mm512_cmp_pd_mask(NK(0), ka, _CMP_LE_OS);
    mkb = _mm512_cmp_pd_mask(NK(0), kb, _CMP_LE_OS);
    ra = _mm512_mask_mul_pd(ra, mka, ra, NK(4));
    rb = _mm512_mask_mul_pd(rb, mkb, rb, NK(4));
    ka = _mm512_mask_sub_pd(ka, mka, ka, NK(0));
    kb = _mm512_mask_sub_pd(kb, mkb, kb, NK(0));

    mka = _mm512_cmp_pd_mask(NK(1), ka, _CMP_LE_OS);
    mkb = _mm512_cmp_pd_mask(NK(1), kb, _CMP_LE_OS);
    ra = _mm512_mask_mul_pd(ra, mka, ra, NK(5));
    rb = _mm512_mask_mul_pd(rb, mkb, rb, NK(5));
    ka = _mm512_mask_sub_pd(ka, mka, ka, NK(1));
    kb = _mm512_mask_sub_pd(kb, mkb, kb, NK(1));

    mka = _mm512_cmp_pd_mask(NK(2), ka, _CMP_LE_OS);
    mkb = _mm512_cmp_pd_mask(NK(2), kb, _CMP_LE_OS);
    ra = _mm512_mask_mul_pd(ra, mka, ra, NK(6));
    rb = _mm512_mask_mul_pd(rb, mkb, rb, NK(6));
    ka = _mm512_mask_sub_pd(ka, mka, ka, NK(2));
    kb = _mm512_mask_sub_pd(kb, mkb, kb, NK(2));

    mka = _mm512_cmp_pd_mask(NK(3), ka, _CMP_LE_OS);
    mkb = _mm512_cmp_pd_mask(NK(3), kb, _CMP_LE_OS);
    ra = _mm512_mask_mul_pd(ra, mka, ra, NK(7));
    rb = _mm512_mask_mul_pd(rb, mkb, rb, NK(7));
    ka = _mm512_mask_sub_pd(ka, mka, ka, NK(3));
    kb = _mm512_mask_sub_pd(kb, mkb, kb, NK(3));

    /* 两条尾数多项式链交替 */
    __m512d pa = _mm512_fmadd_pd(NP(5), ma, NP(4));
    __m512d pb = _mm512_fmadd_pd(NP(5), mb, NP(4));
    pa = _mm512_fmadd_pd(pa, ma, NP(3));
    pb = _mm512_fmadd_pd(pb, mb, NP(3));
    pa = _mm512_fmadd_pd(pa, ma, NP(2));
    pb = _mm512_fmadd_pd(pb, mb, NP(2));
    pa = _mm512_fmadd_pd(pa, ma, NP(1));
    pb = _mm512_fmadd_pd(pb, mb, NP(1));
    pa = _mm512_fmadd_pd(pa, ma, NP(0));
    pb = _mm512_fmadd_pd(pb, mb, NP(0));

    __m512d ya = _mm512_mul_pd(ra, pa);
    __m512d yb = _mm512_mul_pd(rb, pb);

    /* 两次牛顿迭代，同样交替 */
    const __m512d hxa = _mm512_mul_pd(xa, vhalf);
    const __m512d hxb = _mm512_mul_pd(xb, vhalf);
    __m512d ta = _mm512_mul_pd(ya, ya);
    __m512d tb = _mm512_mul_pd(yb, yb);
    ta = _mm512_fnmadd_pd(ta, hxa, vone5);
    tb = _mm512_fnmadd_pd(tb, hxb, vone5);
    ya = _mm512_mul_pd(ya, ta);
    yb = _mm512_mul_pd(yb, tb);
    ta = _mm512_mul_pd(ya, ya);
    tb = _mm512_mul_pd(yb, yb);
    ta = _mm512_fnmadd_pd(ta, hxa, vone5);
    tb = _mm512_fnmadd_pd(tb, hxb, vone5);
    ya = _mm512_mul_pd(ya, ta);
    yb = _mm512_mul_pd(yb, tb);

    *outa = ya;
    *outb = yb;
}
#endif

/* y^3 * mm，即 m_j / r^3 */
static inline __m512d inv_cubed_pd(__m512d x, __m512d mm)
{
    const __m512d y  = rsqrt_pd(x);
    const __m512d y2 = _mm512_mul_pd(y, y);
    return _mm512_mul_pd(mm, _mm512_mul_pd(y2, y));
}

/* ==========================================================================
 * 加速度：ax[i] = sum_{j != i} m_j (r_j - r_i) / |r_j - r_i|^3
 * 按 i 向量化（8 个一组），内层 j 顺序广播。
 * j == i 时 dx=dy=dz 恰好为 0，贡献恰为 0，因此无需特判跳过。
 *
 * ---------------------------------------------------------------------------
 * 为什么每个 j 迭代处理两个 i 块（2 路展开）：
 *   实测内层循环 41 条指令却要 230 个周期（约 5.6 周期/指令）——KNC 是**顺序
 *   核**，一条指令停顿会挡住程序序里它后面的一切。而单次迭代内部的依赖链很深：
 *   比较 → 掩码 → 掩码乘 → 掩码减 → 5 层 FMA 多项式 → 两次牛顿迭代（各 3 条
 *   依赖指令），合计约 15–20 条互相依赖的向量操作。程序序里没有别的独立工作
 *   可以让调度器穿插，于是每轮都串行地付掉整条链的延迟。
 *
 *   两个 i 块共用同样的四个广播值（x[j],y[j],z[j],m[j]），所以**访存流量完全
 *   不变**，翻倍的只是可交错的独立 FMA 链与 rsqrt 链的数量。
 *
 *   每个块仍保持自己顺序的 j 循环，所以每个天体的求和次序与舍入次数与标量
 *   参考完全一致——档位 1 的逐位校验和因此不受影响。
 * ---------------------------------------------------------------------------
 */
/* 自检用开关：见 nbody_accel 里 need 的计算 */
static int g_force_2way = 0;
extern "C" void nbody_accel_force2(int on) { g_force_2way = on ? 1 : 0; }

extern "C" void nbody_accel(int n,
                            const double * __restrict__ x,
                            const double * __restrict__ y,
                            const double * __restrict__ z,
                            const double * __restrict__ m,
                            const double * __restrict__ soft2p,
                            double * __restrict__ ax,
                            double * __restrict__ ay,
                            double * __restrict__ az)
{
    const __m512d vsoft = _mm512_set1_pd(*soft2p);
    const unsigned nv = (unsigned)n / (unsigned)NB_VEC;
    unsigned nvp = nv & ~1u;            /* 走两路展开的块数（偶数） */

    /* 两路展开把并行工作项**减半**：N=1024 时 nv=128，两路只剩 64 组，而卡上
     * 有 240 个线程——一半以上线程会分不到活，档位 1 因此从 2.86 掉到 0.72。
     * 工作项不够铺满线程时就退回单路（nvp=0，下面那个循环会接管全部块）。
     * 阈值取 4×线程数，保证每个线程至少 4 组可调度。
     *
     * 注意这里必须用乘法而不是条件赋值：KNC 没有 cmov，`if (c) x = 0;` 和
     * `c ? 0 : x` 都会被 GCC 编成 cmovb，汇编器直接报
     * `Error: 'cmovb' is not supported on 'k1om'`（见 docs/N2 约束 4）。
     * 比较结果转成 0/1 再相乘，得到的是 setae，合法。 */
    /* g_force_2way 供自检使用：默认 0，need 就是 4x 线程数；
     * 置 1 后 (g-1)=0，need=0，条件恒真，强制走两路展开。
     * 写成掩码相与而不是 ?:，因为 KNC 没有 cmov（docs/N2 约束 4）。 */
    const unsigned need = (4u * (unsigned)omp_get_max_threads())
                        & (unsigned)(g_force_2way - 1);
    nvp *= (unsigned)(nvp >= need);

    #pragma omp parallel for schedule(static)
    for (unsigned ib = 0; ib < nvp; ib += 2u) {
        const unsigned i0 = ib * (unsigned)NB_VEC;
        const unsigned i1 = i0 + (unsigned)NB_VEC;

        const __m512d x0 = _mm512_load_pd(x + i0);
        const __m512d y0 = _mm512_load_pd(y + i0);
        const __m512d z0 = _mm512_load_pd(z + i0);
        const __m512d x1 = _mm512_load_pd(x + i1);
        const __m512d y1 = _mm512_load_pd(y + i1);
        const __m512d z1 = _mm512_load_pd(z + i1);

        __m512d ax0 = _mm512_setzero_pd(), ay0 = _mm512_setzero_pd(), az0 = _mm512_setzero_pd();
        __m512d ax1 = _mm512_setzero_pd(), ay1 = _mm512_setzero_pd(), az1 = _mm512_setzero_pd();

        for (int j = 0; j < n; j++) {
            #if NB_PF > 0
            nb_prefetch0(x + j + NB_PF);
            nb_prefetch0(y + j + NB_PF);
            nb_prefetch0(z + j + NB_PF);
            nb_prefetch0(m + j + NB_PF);
#endif
#if !NB_BCAST_FOLD
            /* 四个广播值两块共用 */
            const __m512d xj = _mm512_set1_pd(x[j]);
            const __m512d yj = _mm512_set1_pd(y[j]);
            const __m512d zj = _mm512_set1_pd(z[j]);
            const __m512d mj = _mm512_set1_pd(m[j]);
#endif

#if NB_BCAST_FOLD
            /* 广播折进减法：算出的是 x0 - x[j]，符号靠下面的 fnmadd 补回。
             * 平方不受符号影响，所以 r2 与原来逐位相同。 */
            const __m512d dx0 = sub_mem_pd(x0, x + j);
            const __m512d dy0 = sub_mem_pd(y0, y + j);
            const __m512d dz0 = sub_mem_pd(z0, z + j);
            const __m512d dx1 = sub_mem_pd(x1, x + j);
            const __m512d dy1 = sub_mem_pd(y1, y + j);
            const __m512d dz1 = sub_mem_pd(z1, z + j);
#else
            const __m512d dx0 = _mm512_sub_pd(xj, x0);
            const __m512d dy0 = _mm512_sub_pd(yj, y0);
            const __m512d dz0 = _mm512_sub_pd(zj, z0);
            const __m512d dx1 = _mm512_sub_pd(xj, x1);
            const __m512d dy1 = _mm512_sub_pd(yj, y1);
            const __m512d dz1 = _mm512_sub_pd(zj, z1);
#endif
            const __m512d r20 = _mm512_fmadd_pd(dz0, dz0,
                                _mm512_fmadd_pd(dy0, dy0,
                                _mm512_fmadd_pd(dx0, dx0, vsoft)));
            const __m512d r21 = _mm512_fmadd_pd(dz1, dz1,
                                _mm512_fmadd_pd(dy1, dy1,
                                _mm512_fmadd_pd(dx1, dx1, vsoft)));

            /* 两条 rsqrt 链手工 lockstep —— 顺序核必须靠程序序拿到独立性，
             * 见 rsqrt_pd_2 的说明 */
            __m512d y0v, y1v;
            rsqrt_pd_2(r20, r21, &y0v, &y1v);

#if NB_BCAST_FOLD
            /* mj 也折进乘法；乘法可交换，结果逐位不变 */
            const __m512d iv0 = mul_mem_pd(_mm512_mul_pd(_mm512_mul_pd(y0v, y0v), y0v), m + j);
            const __m512d iv1 = mul_mem_pd(_mm512_mul_pd(_mm512_mul_pd(y1v, y1v), y1v), m + j);
            /* fnmadd 补回减法翻转掉的符号：-(dx·iv) + acc == dx_orig·iv + acc，
             * 单次舍入、符号精确，与原来的 fmadd 逐位相同 */
            ax0 = _mm512_fnmadd_pd(dx0, iv0, ax0);
            ay0 = _mm512_fnmadd_pd(dy0, iv0, ay0);
            az0 = _mm512_fnmadd_pd(dz0, iv0, az0);
            ax1 = _mm512_fnmadd_pd(dx1, iv1, ax1);
            ay1 = _mm512_fnmadd_pd(dy1, iv1, ay1);
            az1 = _mm512_fnmadd_pd(dz1, iv1, az1);
#else
            const __m512d iv0 = _mm512_mul_pd(mj, _mm512_mul_pd(_mm512_mul_pd(y0v, y0v), y0v));
            const __m512d iv1 = _mm512_mul_pd(mj, _mm512_mul_pd(_mm512_mul_pd(y1v, y1v), y1v));
            ax0 = _mm512_fmadd_pd(dx0, iv0, ax0);
            ay0 = _mm512_fmadd_pd(dy0, iv0, ay0);
            az0 = _mm512_fmadd_pd(dz0, iv0, az0);
            ax1 = _mm512_fmadd_pd(dx1, iv1, ax1);
            ay1 = _mm512_fmadd_pd(dy1, iv1, ay1);
            az1 = _mm512_fmadd_pd(dz1, iv1, az1);
#endif
        }

        _mm512_store_pd(ax + i0, ax0); _mm512_store_pd(ay + i0, ay0); _mm512_store_pd(az + i0, az0);
        _mm512_store_pd(ax + i1, ax1); _mm512_store_pd(ay + i1, ay1); _mm512_store_pd(az + i1, az1);
    }

    /* 单块循环：接管 nvp 之后的所有块。
     * nvp = nv 时它只处理 n 不是 16 的倍数时的残余块；
     * nvp = 0 时（工作项不足）它处理全部块。 */
    #pragma omp parallel for schedule(static)
    for (unsigned ib = nvp; ib < nv; ib++) {
        const unsigned i0 = ib * (unsigned)NB_VEC;
        const __m512d xi = _mm512_load_pd(x + i0);
        const __m512d yi = _mm512_load_pd(y + i0);
        const __m512d zi = _mm512_load_pd(z + i0);
        __m512d accx = _mm512_setzero_pd();
        __m512d accy = _mm512_setzero_pd();
        __m512d accz = _mm512_setzero_pd();

        for (int j = 0; j < n; j++) {
            #if NB_PF > 0
            nb_prefetch0(x + j + NB_PF);
            nb_prefetch0(y + j + NB_PF);
            nb_prefetch0(z + j + NB_PF);
            nb_prefetch0(m + j + NB_PF);
#endif
            const __m512d dx = _mm512_sub_pd(_mm512_set1_pd(x[j]), xi);
            const __m512d dy = _mm512_sub_pd(_mm512_set1_pd(y[j]), yi);
            const __m512d dz = _mm512_sub_pd(_mm512_set1_pd(z[j]), zi);
            const __m512d r2 = _mm512_fmadd_pd(dz, dz,
                               _mm512_fmadd_pd(dy, dy,
                               _mm512_fmadd_pd(dx, dx, vsoft)));
            const __m512d inv = inv_cubed_pd(r2, _mm512_set1_pd(m[j]));
            accx = _mm512_fmadd_pd(dx, inv, accx);
            accy = _mm512_fmadd_pd(dy, inv, accy);
            accz = _mm512_fmadd_pd(dz, inv, accz);
        }
        _mm512_store_pd(ax + i0, accx);
        _mm512_store_pd(ay + i0, accy);
        _mm512_store_pd(az + i0, accz);
    }
}

/* ==========================================================================
 * 势能用的「全对求和」：poti[i] = sum_j m_j / |r_j - r_i|
 * 含 j == i 的自作用项（标量侧再统一扣掉），因为 1/sqrt 不含立方，
 * 自作用项是 m_i/sqrt(soft2) 而不是 0，不能像加速度那样依赖 dx=0。
 * 只用 1/sqrt，不需要立方。
 * ========================================================================== */
extern "C" void nbody_pot_sum(int n,
                              const double * __restrict__ x,
                              const double * __restrict__ y,
                              const double * __restrict__ z,
                              const double * __restrict__ m,
                              const double * __restrict__ soft2p,
                              double * __restrict__ poti)
{
    const __m512d vsoft = _mm512_set1_pd(*soft2p);
    const unsigned nv = (unsigned)n / (unsigned)NB_VEC;

    #pragma omp parallel for schedule(static)
    for (unsigned ib = 0; ib < nv; ib++) {
        const unsigned i0 = ib * (unsigned)NB_VEC;
        const __m512d xi = _mm512_load_pd(x + i0);
        const __m512d yi = _mm512_load_pd(y + i0);
        const __m512d zi = _mm512_load_pd(z + i0);
        __m512d acc = _mm512_setzero_pd();

        for (int j = 0; j < n; j++) {
            #if NB_PF > 0
            nb_prefetch0(x + j + NB_PF);
            nb_prefetch0(y + j + NB_PF);
            nb_prefetch0(z + j + NB_PF);
            nb_prefetch0(m + j + NB_PF);
#endif
            const __m512d dx = _mm512_sub_pd(_mm512_set1_pd(x[j]), xi);
            const __m512d dy = _mm512_sub_pd(_mm512_set1_pd(y[j]), yi);
            const __m512d dz = _mm512_sub_pd(_mm512_set1_pd(z[j]), zi);
            const __m512d r2 = _mm512_fmadd_pd(dz, dz,
                               _mm512_fmadd_pd(dy, dy,
                               _mm512_fmadd_pd(dx, dx, vsoft)));
            acc = _mm512_fmadd_pd(_mm512_set1_pd(m[j]), rsqrt_pd(r2), acc);
        }
        _mm512_store_pd(poti + i0, acc);
    }
}

/* 供 nbody_sink.cpp 报告的向量化能力（非浮点，纯整数） */
extern "C" int nbody_vec_lanes(void) { return NB_VEC; }

/* ---------------------------------------------------------------------------
 * rsqrt 精度探针：把 rsqrt_pd 直接暴露出来，交给标量单元用 libm 逐点比对。
 *
 * 这是整个内核里唯一自造的数学（KNC 没有硬件 FP64 开方，见 N10），也是唯一
 * 无法靠「与宿主参考实现比对校验和」间接证明的部分——校验和只能说明端到端
 * 结果对，说不清单点精度。所以这里单独开一个直接验证的入口。
 *
 * 有效设计范围：EXP_OFF = 12 且二进制链是 4 步（覆盖 k = e+12 ∈ [0,15]），
 * 因此 x = m·2^e 必须落在 e ∈ [-12, 3]，即 x ∈ [2^-12, 2^4) ≈ [2.44e-4, 16)。
 * 实际调用中 r2 = dx²+dy²+dz²+soft2 >= soft2 = 1e-3，且位置在单位立方体内
 * 给出 r2 <= 约 3.1，远在设计范围内。超出该范围时结果不保证——调用方负责。
 * --------------------------------------------------------------------------- */
extern "C" void nbody_rsqrt_probe(const double * __restrict__ in,
                                  double * __restrict__ out, int n)
{
    /* 无符号除法：有符号 n / 8 会让 GCC 生成带符号修正的 cmovs，
     * 而 KNC 没有 cmov（docs/N2 约束 4）。这个陷阱在同一个文件里已经踩到第三次。 */
    const unsigned nv = (unsigned)n / (unsigned)NB_VEC;
    for (unsigned ib = 0; ib < nv; ib++) {
        const unsigned i0 = ib * (unsigned)NB_VEC;
        _mm512_store_pd(out + i0, rsqrt_pd(_mm512_load_pd(in + i0)));
    }
}
