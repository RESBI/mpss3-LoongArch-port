/*
 * 卡端：N 体引力模拟（重计算示例）。
 *   直接求和 O(N^2) + 蛙跳积分 + OpenMP；初始条件由卡端按确定性公式自行生成，
 *   与宿主参考实现逐位一致（因此不需要 COI 缓冲区）。
 *
 * 优化架构（详见 docs/N1..N8 与 N9 系列）：
 *   本文件保持 -O0 编译，只做初始条件生成（sin/cos）、动能、时序与校验和 ——
 *   这些都含标量浮点，一旦用 -mavx512f 编译就会生成 KNC 没有的 xmm 指令。
 *   真正的 O(N^2) 热路径（accel 与势能求和）在 nbody_vec.cpp 里，用
 *   -O2 -mavx512f 编译、全部写成 FP64 向量 intrinsic，由本文件调用。
 *   原因见 docs/N2：KNC 没有硬件 FP64 sqrt/除法（VSQRTPD/VDIVPD 都不存在），
 *   标量路径只能落到 libm，实测每对粒子约 285 个周期。
 *
 * 注意（KNC 实测约束）：
 *   本文件必须用 -O0 编译：-O1 与 -O2 会生成掩码压缩存储（vpackstorelpd）
 *   且其中一条改用非 rbp 基址（disp8*N 压缩位移），卡上执行即崩
 *   （卡端内核日志表现为 segfault）。nbody_vec.cpp 是另一回事：它编译出的
 *   产物 vpackstore/vscatter 为 0 条，所以可以放心用 -O2。
 *
 * 构建：见 t7_nbody.sh —— 两个编译单元分别用不同优化档，最后链接成一个 sink。
 */
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <omp.h>

#include <intel-coi/sink/COIPipeline_sink.h>
#include <intel-coi/sink/COIProcess_sink.h>
#include <intel-coi/common/COIMacros_common.h>

/* nbody_vec.cpp（-O2 -mavx512f）导出的向量内核。
 * 接口刻意只用指针与 int，不传/不返回 double：避免跨编译单元的浮点 ABI 差异。 */
extern "C" void nbody_accel(int n,
                            const double *x, const double *y, const double *z,
                            const double *m, const double *soft2p,
                            double *ax, double *ay, double *az);
extern "C" void nbody_pot_sum(int n,
                              const double *x, const double *y, const double *z,
                              const double *m, const double *soft2p,
                              double *poti);
extern "C" int  nbody_vec_lanes(void);
extern "C" void nbody_rsqrt_probe(const double *in, double *out, int n);
extern "C" void nbody_accel_force2(int on);

/* 向量内核要求 n 是 8 的倍数（本测试三档 1024/8192/16384 均满足） */
#define NB_ALIGN 8

struct Params {
    int    n;
    int    steps;
    double dt;
    double soft2;
    int    report;
    int    nthreads;   /* <=0 表示用运行库默认值 */
};
/* 注意：本结构必须与 nbody_host.cpp 里的 Params **逐字节一致**。
 *
 * 为什么要显式传线程数：宿主是用 COIProcessCreateFromFile 把卡端进程拉起来的，
 * 宿主的环境变量**不会**传过去。实测 OMP_NUM_THREADS=61/122/183/244 四种设置，
 * 内核自报的线程数都还是 240——也就是说在宿主机上设环境变量完全没有作用。
 * 想真正改卡上线程数，只能作为参数传进来，在卡端调 omp_set_num_threads。 */

struct Result {
    int    n;
    int    steps;
    int    threads;
    double e0, e1;
    double rel_energy_err;
    double checksum;
    double secs;
    double gflops;
    double rsqrt_max_rel_err;   /* 卡上 rsqrt 与 libm 1/sqrt 的最大相对误差 */
    double accel_max_rel_err;   /* 向量受力核 vs 标量 libm 参考的最大相对误差 */
    double mpairs;              /* 百万物理粒子对/秒（与 GFLOPS 同口径） */
};
/* 注意：本结构必须与 nbody_host.cpp 里的 Result **逐字节一致**，
 * 任何字段增删都要两边同时改（COI 是把返回结构按内存原样拷回来的）。 */

/* 位置与速度一律用「分离数组」，避免 x[3i+1] 这类交错别名引起的优化歧义。
 * 热路径整体交给 nbody_vec.cpp 的 FP64 向量内核。 */
#ifdef NB_SCALAR_BASELINE
/* 标量基线：与优化前逐字相同的实现，只用于在同一套初始条件下做诚实对比。
 * 用 -DNB_SCALAR_BASELINE=1 构建（见 t7_nbody.sh 的 T7_SCALAR 开关）。 */
static void accel(int n, const double *x, const double *y, const double *z,
                  const double *m, double soft2,
                  double *ax, double *ay, double *az)
{
    #pragma omp parallel for schedule(dynamic, 16)
    for (int i = 0; i < n; i++) {
        double axi = 0.0, ayi = 0.0, azi = 0.0;
        const double xi = x[i], yi = y[i], zi = z[i];
        for (int j = 0; j < n; j++) {
            if (j == i) continue;
            const double dx = x[j] - xi, dy = y[j] - yi, dz = z[j] - zi;
            const double r2 = dx * dx + dy * dy + dz * dz + soft2;
            const double inv = m[j] / (r2 * sqrt(r2));
            axi += dx * inv; ayi += dy * inv; azi += dz * inv;
        }
        ax[i] = axi; ay[i] = ayi; az[i] = azi;
    }
}
#else
static void accel(int n, const double *x, const double *y, const double *z,
                  const double *m, double soft2,
                  double *ax, double *ay, double *az)
{
    nbody_accel(n, x, y, z, m, &soft2, ax, ay, az);
}
#endif

/* 势能：改成「全对求和 + 扣掉对角」的形式，好让向量内核复用同一套循环形状。
 *   sum_{i<j} m_i m_j / r_ij
 *     = 0.5 * ( sum_i m_i * sum_j m_j/r_ij  -  sum_i m_i^2/sqrt(soft2) )
 * 含对角的那版 sum_j 在 j==i 时是 m_i/sqrt(soft2)（不是 0），所以必须显式扣掉。
 * 精度要求只有 1e-3（能量守恒判据），求和次序怎么改都够。 */
static double energy(int n, const double *x, const double *y, const double *z,
                     const double *vx, const double *vy, const double *vz,
                     const double *m, double soft2, double *kin_out,
                     double *poti_scratch)
{
    double kin = 0.0;
    #pragma omp parallel for reduction(+:kin) schedule(dynamic, 16)
    for (int i = 0; i < n; i++)
        kin += 0.5 * m[i] * (vx[i] * vx[i] + vy[i] * vy[i] + vz[i] * vz[i]);

#ifdef NB_SCALAR_BASELINE
    #pragma omp parallel for schedule(dynamic, 16)
    for (int i = 0; i < n; i++) {
        double acc = 0.0;
        for (int j = 0; j < n; j++) {
            const double dx = x[j]-x[i], dy = y[j]-y[i], dz = z[j]-z[i];
            acc += m[j] / sqrt(dx*dx + dy*dy + dz*dz + soft2);
        }
        poti_scratch[i] = acc;
    }
#else
    nbody_pot_sum(n, x, y, z, m, &soft2, poti_scratch);
#endif

    double s = 0.0, diag = 0.0;
    for (int i = 0; i < n; i++) {
        s    += m[i] * poti_scratch[i];
        diag += m[i] * m[i];
    }
    diag /= sqrt(soft2);
    const double pot = -0.5 * (s - diag);

    if (kin_out) *kin_out = kin;
    return kin + pot;
}

/* ---------------------------------------------------------------------------
 * 卡上 rsqrt 精度自检。
 *
 * 动机：整个 T7 加速的支点是自造的 FP64 1/sqrt（KNC 没有硬件开方，见 N10）。
 * 校验和一致只能证明端到端结果对，不能定位到这一段数学；而它是唯一无法用
 * 「与宿主参考比对」间接证明的部分——因为它本身就是被替换掉的那一环。
 * 所以这里直接点名验证：在 r2 的实际取值范围上取对数均匀样本，用卡上的
 * 向量 rsqrt 算一遍，再用标量 libm 的 1/sqrt 逐点比相对误差。
 *
 * 样本范围 [2.6e-4, 15.5] 完整覆盖设计范围 x ∈ [2^-12, 2^4)（见 nbody_vec.cpp
 * 中 rsqrt_pd 的说明），比实际用到的 [1e-3, 3.1] 宽得多。
 * --------------------------------------------------------------------------- */
static double *alloc64(int n);   /* 定义在下方 */

/* 确定性散列。只用整数运算 + 乘 1/2^24（精确），所以卡端与宿主必然逐位一致，
 * 不像 sin/cos 那样依赖两边 libm 的实现细节。 */
static double hash01(unsigned k)
{
    k = (k ^ 61u) ^ (k >> 16);
    k *= 9u;
    k = k ^ (k >> 4);
    k *= 0x27d4eb2du;
    k = k ^ (k >> 15);
    return (double)(k & 0xFFFFFFu) * (1.0 / 16777216.0);
}

/* ---------------------------------------------------------------------------
 * 受力核自检 —— 这是本测试里唯一真正检验受力定律的环节。
 *
 * 为什么必须单独做：端到端校验和对受力几乎不敏感。实测（N=32768，4 步）：
 *     inv = m/r^1.5（正确）      校验和与参考差 2.96e-16   能量误差 3.0e-05
 *     inv = m*x*0.5（错）        校验和与参考差 1.16e-14   能量误差 2.1e-06
 *     inv = 0     （完全无力！）  校验和与参考差 1.58e-14   能量误差 1.2e-06
 * 把受力整个置零，两项检查都通过——因为动力学由初始速度的弹道项主导，受力只
 * 贡献位移的一小部分。所以必须直接拿向量内核的加速度与标量 libm 参考逐点比对。
 * --------------------------------------------------------------------------- */
#define NSB 64            /* 必须是 8 的倍数（向量内核要求） */
static double accel_selftest(const double *x, const double *y, const double *z,
                             const double *m, double soft2)
{
    double *ax = alloc64(NSB), *ay = alloc64(NSB), *az = alloc64(NSB);
    if (!ax || !ay || !az) { free(ax); free(ay); free(az); return -1.0; }

    /* 标量参考 */
    double *rx = alloc64(NSB), *ry = alloc64(NSB), *rz = alloc64(NSB);
    if (!rx || !ry || !rz) { free(ax); free(ay); free(az); free(rx); free(ry); free(rz); return -1.0; }
    for (int i = 0; i < NSB; i++) {
        double sx = 0.0, sy = 0.0, sz = 0.0;
        for (int j = 0; j < NSB; j++) {
            if (j == i) continue;
            const double dx = x[j]-x[i], dy = y[j]-y[i], dz = z[j]-z[i];
            const double r2 = dx*dx + dy*dy + dz*dz + soft2;
            const double inv = m[j] / (r2 * sqrt(r2));
            sx += dx*inv; sy += dy*inv; sz += dz*inv;
        }
        rx[i] = sx; ry[i] = sy; rz[i] = sz;
    }

    /* **两条代码路径都要测**。内核里有两条受力循环：
     *   - 两路展开主循环（承担几乎全部计算量）
     *   - 单块尾循环
     * 默认参数下 NSB=64 只有 8 个块，会被并行度守卫判为「工作项不足」，
     * 于是只走尾循环——主循环等于没验证。实测过：把主循环的 rsqrt 换成
     * 常数，这项自检照样通过。所以显式强制走两路再测一遍。 */
    double worst = 0.0;
    for (int pass = 0; pass < 2; pass++) {
        nbody_accel_force2(pass);          /* pass 0 = 默认路径, 1 = 强制两路 */
        nbody_accel(NSB, x, y, z, m, &soft2, ax, ay, az);
        for (int i = 0; i < NSB; i++) {
            const double scale = fabs(rx[i]) + fabs(ry[i]) + fabs(rz[i]);
            const double d = fabs(ax[i]-rx[i]) + fabs(ay[i]-ry[i]) + fabs(az[i]-rz[i]);
            if (scale > 0.0) {
                const double rel = d / scale;
                if (rel > worst) worst = rel;
            }
        }
    }
    nbody_accel_force2(0);

    free(ax); free(ay); free(az);
    free(rx); free(ry); free(rz);
    return worst;
}


#define RS_N 512          /* 必须是 8 的倍数 */
static double rsqrt_selftest(void)
{
    double *in  = alloc64(RS_N);
    double *out = alloc64(RS_N);
    if (!in || !out) { free(in); free(out); return -1.0; }

    /* 对数均匀：对数尺度上均匀分布，才能均匀覆盖每个二进制数量级 */
    const double lo = 2.6e-4, hi = 15.5;
    for (int i = 0; i < RS_N; i++) {
        const double t = (double)i / (double)(RS_N - 1);
        in[i] = lo * pow(hi / lo, t);
    }
    /* 再补几个边界与常见值 */
    in[0] = lo;  in[1] = hi;  in[2] = 1e-3;  in[3] = 3.0;
    in[4] = 1.0; in[5] = 2.0; in[6] = 0.0625; in[7] = 4.0;

    nbody_rsqrt_probe(in, out, RS_N);

    double worst = 0.0;
    for (int i = 0; i < RS_N; i++) {
        const double ref = 1.0 / sqrt(in[i]);
        const double rel = fabs(out[i] - ref) / ref;
        if (rel > worst) worst = rel;
    }
    free(in); free(out);
    return worst;
}

/* 64 字节对齐分配：向量内核用 vmovapd/vmovapd 访问这些数组，
 * KNC 没有非对齐向量访存（无 vmovupd），用 calloc 的 16 字节对齐会在卡上
 * 触发 general protection。见 docs/N2 约束 3。 */
static double *alloc64(int n)
{
    void *p = NULL;
    if (posix_memalign(&p, 64, (size_t)n * sizeof(double)) != 0) return NULL;
    if (p) memset(p, 0, (size_t)n * sizeof(double));
    return (double *)p;
}

COINATIVELIBEXPORT
void NBodyRun(uint32_t        in_BufferCount,
              void          **in_ppBufferPointers,
              uint64_t       *in_pBufferLengths,
              void           *in_pMiscData,
              uint16_t        in_MiscDataLength,
              void           *in_pReturnValue,
              uint16_t        in_ReturnValueLength)
{
    (void)in_BufferCount; (void)in_ppBufferPointers; (void)in_pBufferLengths;

    Result *r = (Result *)in_pReturnValue;
    if (!r || in_ReturnValueLength < sizeof(*r)) return;
    memset(r, 0, sizeof(*r));
    if (!in_pMiscData || in_MiscDataLength < sizeof(Params)) return;

    Params p;
    memcpy(&p, in_pMiscData, sizeof(p));
    const int n = p.n;
    r->n = n; r->steps = p.steps;
    if (n <= 0) return;

    double *x  = alloc64(n);
    double *y  = alloc64(n);
    double *z  = alloc64(n);
    double *vx = alloc64(n);
    double *vy = alloc64(n);
    double *vz = alloc64(n);
    double *m  = alloc64(n);
    double *ax = alloc64(n);
    double *ay = alloc64(n);
    double *az = alloc64(n);
    /* 势能向量内核的每-i 部分和暂存区 */
    double *poti = alloc64(n);
    if (!x || !y || !z || !vx || !vy || !vz || !m || !ax || !ay || !az || !poti) {
        free(x); free(y); free(z); free(vx); free(vy); free(vz);
        free(m); free(ax); free(ay); free(az); free(poti);
        return;
    }
    /* 向量内核一次处理 nbody_vec_lanes() 个天体 */
    if ((n % NB_ALIGN) != 0) {
        fprintf(stderr, "NBodyRun: n=%d 必须是 %d 的倍数（向量内核要求）\n", n, NB_ALIGN);
        free(x); free(y); free(z); free(vx); free(vy); free(vz);
        free(m); free(ax); free(ay); free(az); free(poti);
        return;
    }

    /* 初始条件：与宿主参考实现同一公式（顺序一致 → 逐位可对） */
    {
        double px = 0.0, py = 0.0, pz = 0.0;
        for (int i = 0; i < n; i++) {
            /* 位置用整数散列散布，**不是**规则点阵 + 微扰。
             * 规则点阵的净引力按对称性几乎完全抵消，会让整个测试对受力不敏感
             * （实测把受力置零仍能通过，见 accel_selftest 的说明）。
             * 散列散布让受力成为动力学的真正主导项。 */
            x[i]  = hash01(3u * (unsigned)i + 0u);
            y[i]  = hash01(3u * (unsigned)i + 1u);
            z[i]  = hash01(3u * (unsigned)i + 2u);
            vx[i] = 0.1  * sin(4.1 * (double)i);
            vy[i] = 0.1  * cos(2.7 * (double)i);
            vz[i] = 0.05 * sin(1.3 * (double)i + 1.0);
            m[i]  = 1.0 / (double)n;
            px += vx[i]; py += vy[i]; pz += vz[i];
        }
        for (int i = 0; i < n; i++) {
            vx[i] -= px / (double)n;
            vy[i] -= py / (double)n;
            vz[i] -= pz / (double)n;
        }
    }

    /* 先做两项内核自检（在真正计算之前，保证报出来的误差属于本次构建） */
    r->rsqrt_max_rel_err = rsqrt_selftest();
    r->accel_max_rel_err = accel_selftest(x, y, z, m, p.soft2);

    /* 线程数必须在任何并行区之前设定；自检里的并行度守卫也会跟着变 */
    if (p.nthreads > 0) omp_set_num_threads(p.nthreads);

    double k0 = 0.0;
    r->e0 = energy(n, x, y, z, vx, vy, vz, m, p.soft2, &k0, poti);

    const double t0 = omp_get_wtime();
    for (int s = 0; s < p.steps; s++) {
        accel(n, x, y, z, m, p.soft2, ax, ay, az);
        for (int i = 0; i < n; i++) {
            vx[i] += ax[i] * p.dt; vy[i] += ay[i] * p.dt; vz[i] += az[i] * p.dt;
            x[i]  += vx[i] * p.dt; y[i]  += vy[i] * p.dt; z[i]  += vz[i] * p.dt;
        }
    }
    const double t1 = omp_get_wtime();

    double k1 = 0.0;
    r->e1 = energy(n, x, y, z, vx, vy, vz, m, p.soft2, &k1, poti);
    r->rel_energy_err = (r->e0 != 0.0) ? fabs((r->e1 - r->e0) / r->e0) : 0.0;
    r->secs = t1 - t0;
    r->threads = omp_get_max_threads();

    double cs = 0.0;
    for (int i = 0; i < n; i++)
        cs += fabs(x[i]) + 2.0 * fabs(y[i]) + 3.0 * fabs(z[i]);
    r->checksum = cs;

    /* 两个速率指标同口径：都按**物理配对数** n(n-1)/2 计（不含自配对），
     * 这样对称优化所省掉的重复计算才体现为速率的提升，而不是被口径变化吃掉。 */
    const double pairs = (double)n * (double)(n - 1) * 0.5;
    r->gflops = (p.steps > 0 && r->secs > 0.0) ? (20.0 * pairs * p.steps / r->secs / 1e9) : 0.0;
    r->mpairs = (p.steps > 0 && r->secs > 0.0) ? (pairs * p.steps / r->secs / 1e6)    : 0.0;

    if (p.report)
        printf("NBodyRun: n=%d steps=%d threads=%d E0=%.6e E1=%.6e rel=%.3e cs=%.9f %.3f s %.2f GFLOPS %.2f MPairs/s rsqrt_relerr=%.3e accel_relerr=%.3e\n",
               n, p.steps, r->threads, r->e0, r->e1, r->rel_energy_err, r->checksum, r->secs, r->gflops,
               r->mpairs, r->rsqrt_max_rel_err, r->accel_max_rel_err);
    fflush(stdout);

    free(x); free(y); free(z); free(vx); free(vy); free(vz);
    free(m); free(ax); free(ay); free(az); free(poti);
}

int main(int, char **)
{
    COIPipelineStartExecutingRunFunctions();
    COIProcessWaitForShutdown();
    return 0;
}
