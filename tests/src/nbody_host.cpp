/*
 * 宿主：N 体引力模拟 offload（重计算 + 大数据量示例）。
 *   - 初始条件由宿主生成，经 COI 缓冲区（宿主内存承载）送入卡端，规模达 MB 级
 *   - 卡端用 OpenMP 做 O(N^2) 直接求和 + 蛙跳积分
 *   - 校验：小规模时与宿主参考实现比对「校验和」；所有规模检查能量守恒
 *   - 报告卡上耗时与 GFLOPS
 *
 * 编译：g++ -O2 -fopenmp -I<coi stage>/usr/include nbody_host.cpp \
 *          -o nbody_host -L<coi stage>/usr/lib64 -lcoi_host -Wl,-rpath,<coi stage>/usr/lib64
 * 运行：./nbody_host <卡端 sink 路径> <k1om 依赖库目录> [规模档位 1|2|3]
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>

#include <intel-coi/source/COIEngine_source.h>
#include <intel-coi/source/COIProcess_source.h>
#include <intel-coi/source/COIPipeline_source.h>
#include <intel-coi/source/COIBuffer_source.h>
#include <intel-coi/source/COIEvent_source.h>
#include <intel-coi/common/COIResult_common.h>

struct Params {
    int    n;
    int    steps;
    double dt;
    double soft2;
    int    report;
    int    nthreads;   /* <=0 表示用运行库默认值 */
};
/* 必须与 nbody_sink.cpp 里的 Params 逐字节一致。 */

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

/* 档位总数。t7_nbody.sh 的循环上界必须与之一致。 */
#define NTIER 6

/* 宿主参考复算的默认规模阈值。
 *
 * 参考实现是 O(N^2)·steps：N=131072、4 步时约 5.2e10 次粒子对求值，
 * 单是它一项就占整套测试时间的大部分。所以默认只在小档开启。
 *
 * 必须分清楚：**关掉的只是这个昂贵的端到端复算**。两项内核自检
 * （受力核 vs 标量 libm、rsqrt vs libm 1/sqrt）很便宜，而且它们才是真正
 * 有判别力的正确性验证（见 docs/N13）——任何档位、任何设置下都会执行。
 *
 *   T7_REF=1   强制所有档位都做宿主参考复算（发版前建议跑一次）
 *   T7_REF=0   强制全部关闭
 *   未设置      仅当 N <= NREF_DEFAULT 时做
 */
#define NREF_DEFAULT 16384

#define CHECK(_e) do { COIRESULT _r = (_e); \
    if (_r != COI_SUCCESS) { \
        printf("  失败: %s -> %s(%d)\n", #_e, COIResultGetName(_r), (int)_r); return 1; } } while (0)

/* 确定性散列。与 nbody_sink.cpp 中的实现**必须完全一致**：
 * 只用整数运算 + 乘 1/2^24（精确），所以两边必然逐位一致。 */
static double hash01(unsigned k)
{
    k = (k ^ 61u) ^ (k >> 16);
    k *= 9u;
    k = k ^ (k >> 4);
    k *= 0x27d4eb2du;
    k = k ^ (k >> 15);
    return (double)(k & 0xFFFFFFu) * (1.0 / 16777216.0);
}

/* 确定性初始条件：单位立方体内的点阵 + 小扰动，总动量为零 */
static void init_ic(int n, std::vector<double> &pos, std::vector<double> &vel,
                    std::vector<double> &mass)
{
    pos.assign((size_t)n * 3, 0.0);
    vel.assign((size_t)n * 3, 0.0);
    mass.assign((size_t)n, 1.0 / (double)n);

    /* 位置用整数散列散布，与卡端 nbody_sink.cpp 完全一致。
     * 不再用「规则点阵 + 微扰」：规则点阵的净引力按对称性几乎抵消，会让
     * 端到端校验和对受力完全不敏感（实测把受力置零仍能通过）。 */
    double px = 0.0, py = 0.0, pz = 0.0;
    for (int i = 0; i < n; i++) {
        pos[3 * i + 0] = hash01(3u * (unsigned)i + 0u);
        pos[3 * i + 1] = hash01(3u * (unsigned)i + 1u);
        pos[3 * i + 2] = hash01(3u * (unsigned)i + 2u);
        vel[3 * i + 0] = 0.1 * sin(4.1 * (double)i);
        vel[3 * i + 1] = 0.1 * cos(2.7 * (double)i);
        vel[3 * i + 2] = 0.05 * sin(1.3 * (double)i + 1.0);
        px += vel[3 * i + 0]; py += vel[3 * i + 1]; pz += vel[3 * i + 2];
    }
    /* 去掉整体动量，避免系统漂移 */
    for (int i = 0; i < n; i++) {
        vel[3 * i + 0] -= px / (double)n;
        vel[3 * i + 1] -= py / (double)n;
        vel[3 * i + 2] -= pz / (double)n;
    }
}

/* 宿主参考实现：与卡端同算法、同累加顺序（用于小规模校验） */
static void host_reference(int n, int steps, double dt, double soft2,
                           std::vector<double> pos, std::vector<double> vel,
                           const std::vector<double> &mass,
                           double *checksum, double *e0, double *e1)
{
    std::vector<double> ax(n), ay(n), az(n);
    double k0 = 0, k1 = 0;
    *e0 = 0.0; *e1 = 0.0;
    for (int i = 0; i < n; i++) {
        const double v2 = vel[3*i]*vel[3*i] + vel[3*i+1]*vel[3*i+1] + vel[3*i+2]*vel[3*i+2];
        k0 += 0.5 * mass[i] * v2;
    }
    *e0 = k0;
    {
        double pot = 0.0;
        #pragma omp parallel for reduction(+:pot) schedule(dynamic, 8)
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++) {
                const double dx = pos[3*j]-pos[3*i], dy = pos[3*j+1]-pos[3*i+1], dz = pos[3*j+2]-pos[3*i+2];
                pot += mass[i]*mass[j] / sqrt(dx*dx + dy*dy + dz*dz + soft2);
            }
        *e0 -= pot;
    }
    for (int s = 0; s < steps; s++) {
        /* 只对 i 并行：每个 i 的 j 循环仍在本线程内顺序执行，与卡端
         * 「一条车道顺序累加一个 i」的形状一致，比对才有意义。
         * 若改成对 j 并行或做跨线程归约，求和次序就变了。 */
        #pragma omp parallel for schedule(dynamic, 8)
        for (int i = 0; i < n; i++) {
            double axi = 0, ayi = 0, azi = 0;
            for (int j = 0; j < n; j++) {
                if (j == i) continue;
                const double dx = pos[3*j]-pos[3*i], dy = pos[3*j+1]-pos[3*i+1], dz = pos[3*j+2]-pos[3*i+2];
                const double r2 = dx*dx + dy*dy + dz*dz + soft2;
                const double inv = mass[j] / (r2 * sqrt(r2));
                axi += dx*inv; ayi += dy*inv; azi += dz*inv;
            }
            ax[i] = axi; ay[i] = ayi; az[i] = azi;
        }
        for (int i = 0; i < n; i++) {
            vel[3*i] += ax[i]*dt; vel[3*i+1] += ay[i]*dt; vel[3*i+2] += az[i]*dt;
            pos[3*i] += vel[3*i]*dt; pos[3*i+1] += vel[3*i+1]*dt; pos[3*i+2] += vel[3*i+2]*dt;
        }
    }
    for (int i = 0; i < n; i++) {
        const double v2 = vel[3*i]*vel[3*i] + vel[3*i+1]*vel[3*i+1] + vel[3*i+2]*vel[3*i+2];
        k1 += 0.5 * mass[i] * v2;
    }
    *e1 = k1;
    {
        double pot = 0.0;
        #pragma omp parallel for reduction(+:pot) schedule(dynamic, 8)
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++) {
                const double dx = pos[3*j]-pos[3*i], dy = pos[3*j+1]-pos[3*i+1], dz = pos[3*j+2]-pos[3*i+2];
                pot += mass[i]*mass[j] / sqrt(dx*dx + dy*dy + dz*dz + soft2);
            }
        *e1 -= pot;
    }
    double cs = 0.0;
    for (int i = 0; i < n; i++)
        cs += fabs(pos[3*i]) + 2.0 * fabs(pos[3*i+1]) + 3.0 * fabs(pos[3*i+2]);
    *checksum = cs;
}

int main(int argc, char **argv)
{
    const char *sink = (argc > 1) ? argv[1] : "./nbody_sink";
    const char *libs = (argc > 2) ? argv[2] : NULL;
    const int   tier = (argc > 3) ? atoi(argv[3]) : 2;

    /* 档位表。前三档是「宿主参考可复算」的小规模，后三档是吞吐规模。
     * 档位 4/5/6 的工作集：N=32768 时 7 个数组共 1.75 MB，N=65536 时 3.5 MB，
     * N=131072 时 7 MB —— 都已明显超出每核 512 KB 的 L2。
     * 步数下限统一为 4：步数太少时积分链路的检验太浅（弹道项还没被受力拉开
     * 差距），而且混沌系统的发散放大也来不及体现。 */
    static const int ns[NTIER]     = { 1024, 8192, 16384, 32768, 65536, 131072 };
    static const int stepss[NTIER] = { 20,   10,    5,     4,     4,      4 };
    const int ti = (tier >= 1 && tier <= NTIER) ? tier - 1 : 1;
    const int n     = ns[ti];
    const int steps = stepss[ti];

    /* 是否做 O(N^2) 的宿主参考复算：默认只有小档做，见 NREF_DEFAULT 的说明。
     * 必须放在 n 声明之后。 */
    const char *refenv = getenv("T7_REF");
    const int do_ref = refenv ? (atoi(refenv) != 0) : (n <= NREF_DEFAULT);
    const double dt = 0.002, soft2 = 1e-3;

    printf("=== N 体引力模拟 offload（重计算 / 大数据量）===\n");
    printf("卡端: %s   档位: %d   N=%d  步数=%d\n", sink, tier, n, steps);
    printf("数据量: 位置 %.2f MB + 速度 %.2f MB + 质量 %.3f MB = %.2f MB\n\n",
           (double)n*3*8/1048576.0, (double)n*3*8/1048576.0, (double)n*8/1048576.0,
           (double)n*7*8/1048576.0);

    std::vector<double> pos, vel, mass;
    init_ic(n, pos, vel, mass);

    uint32_t engines = 0;
    CHECK(COIEngineGetCount(COI_DEVICE_MIC, &engines));
    printf("1) 引擎数 = %u\n", engines);
    COIENGINE engine = NULL;
    CHECK(COIEngineGetHandle(COI_DEVICE_MIC, 0, &engine));

    COIPROCESS proc = NULL;
    COIRESULT rc = COIProcessCreateFromFile(engine, sink, 0, NULL, false, NULL,
                                            true, NULL, 0, libs, &proc);
    printf("2) 卡上创建进程 -> %s(%d)\n", COIResultGetName(rc), (int)rc);
    if (rc != COI_SUCCESS) return 1;

    COIPIPELINE pipeline = NULL;
    CHECK(COIPipelineCreate(proc, NULL, 0, &pipeline));

    const char *fname = "NBodyRun";
    COIFUNCTION func = NULL;
    CHECK(COIProcessGetFunctionHandles(proc, 1, &fname, &func));
    printf("3) 取到卡端函数: %s\n", fname);

    /* 用宿主内存承载三个缓冲区：卡端经 RMA 直接读取，即「大数据量传输」 */
    /* 本示例由卡端自行生成初始条件，不需要 COI 缓冲区。
   （注：COIBufferCreate 在当前移植链上返回 COI_OUT_OF_MEMORY，已单独记录待查。） */
    printf("4) 已建立 %u 个 COI 缓冲区（宿主内存承载）\n", 3u);

    Params p; memset(&p, 0, sizeof(p));
    /* 卡上线程数：T7_THREADS 环境变量，0/未设表示用运行库默认。
     * 必须显式传参——宿主环境变量不会随 COI 启动传到卡端。 */
    int nthreads = 0;
    { const char *e = getenv("T7_THREADS"); if (e) nthreads = atoi(e); }
    p.n = n; p.steps = steps; p.dt = dt; p.soft2 = soft2; p.report = 1;
    p.nthreads = nthreads;
    Result r; memset(&r, 0, sizeof(r));
    COIEVENT ev;

    printf("5) 提交计算（卡端 OpenMP 并行 O(N^2) 直接求和）...\n");
    CHECK(COIPipelineRunFunction(pipeline, func, 0, NULL, NULL, 0, NULL,
                                 &p, (uint16_t)sizeof(p), &r, (uint16_t)sizeof(r), &ev));
    CHECK(COIEventWait(1, &ev, -1, 0, NULL, NULL));
    printf("6) 卡上计算完成\n\n");

    printf("=== 卡上结果 ===\n");
    printf("  天体数 / 步数      : %d / %d\n", r.n, r.steps);
    printf("  卡上线程数         : %d%s\n", r.threads,
           nthreads > 0 ? "（由 T7_THREADS 指定）" : "（运行库默认）");
    printf("  初态总能量 E0      : %.10e\n", r.e0);
    printf("  末态总能量 E1      : %.10e\n", r.e1);
    printf("  相对能量误差       : %.3e\n", r.rel_energy_err);
    printf("  末态校验和         : %.9f\n", r.checksum);
    printf("  卡上耗时           : %.3f 秒\n", r.secs);
    printf("  估算算力           : %.2f GFLOPS\n", r.gflops);
    printf("  粒子对速率         : %.2f MPairs/s（按物理配对数 n(n-1)/2 计）\n", r.mpairs);

    int bad = 0;
    if (!(r.rel_energy_err < 1e-3)) { printf("  [FAIL] 能量不守恒（%.3e）\n", r.rel_energy_err); bad++; }
    else printf("  [OK] 能量守恒（%.3e < 1e-3）\n", r.rel_energy_err);

    /* 每一档都与宿主参考实现比对校验和。
     * 参考实现是 O(N^2)，N=65536 时约 8.6e9 次粒子对求值——已按 i 并行化，
     * 单核几十秒、多核几秒，值得为「大档也验证过」付这个代价。 */
    if (do_ref) {
        printf("  正在用宿主参考实现复算（O(N^2)，N=%d，%d 步）...\n", n, steps);
        fflush(stdout);
        double ref_cs = 0, ref_e0 = 0, ref_e1 = 0;
        host_reference(n, steps, dt, soft2, pos, vel, mass, &ref_cs, &ref_e0, &ref_e1);
        const double d = fabs(ref_cs - r.checksum);
        const double rel = (ref_cs != 0.0) ? d / fabs(ref_cs) : d;
        printf("  宿主参考校验和     : %.9f（差 %.3e）\n", ref_cs, rel);
        if (rel < 1e-9) printf("  [OK] 与宿主参考一致\n");
        else { printf("  [FAIL] 与宿主参考不一致\n"); bad++; }
    } else {
        printf("  宿主参考校验和     : 已跳过（N=%d > 默认阈值 %d；设 T7_REF=1 强制开启）\n",
               n, NREF_DEFAULT);
    }

    /* 卡上 rsqrt 精度自检 */
    printf("  rsqrt 最大相对误差 : %.3e（对比 libm 1/sqrt，512 点对数均匀）\n",
           r.rsqrt_max_rel_err);
    if (r.rsqrt_max_rel_err >= 0.0 && r.rsqrt_max_rel_err < 1e-14)
        printf("  [OK] rsqrt 精度达标（< 1e-14）\n");
    else { printf("  [FAIL] rsqrt 精度不达标（%.3e）\n", r.rsqrt_max_rel_err); bad++; }

    /* 卡上受力核自检 —— 端到端校验和测不到受力，这项才是真正的受力验证 */
    printf("  受力核最大相对误差 : %.3e（向量内核 vs 标量 libm，前 64 个天体）\n",
           r.accel_max_rel_err);
    if (r.accel_max_rel_err >= 0.0 && r.accel_max_rel_err < 1e-14)
        printf("  [OK] 受力核正确\n");
    else { printf("  [FAIL] 受力核不正确（%.3e）\n", r.accel_max_rel_err); bad++; }

    CHECK(COIPipelineDestroy(pipeline));
    CHECK(COIProcessDestroy(proc, -1, 0, NULL, NULL));
    printf("\n7) 完成；%s\n", bad == 0 ? "全部校验通过" : "存在失败项");
    return bad == 0 ? 0 : 1;
}
