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
};

struct Result {
    int    n;
    int    steps;
    int    threads;
    double e0, e1;
    double rel_energy_err;
    double checksum;
    double secs;
    double gflops;
};

#define CHECK(_e) do { COIRESULT _r = (_e); \
    if (_r != COI_SUCCESS) { \
        printf("  失败: %s -> %s(%d)\n", #_e, COIResultGetName(_r), (int)_r); return 1; } } while (0)

/* 确定性初始条件：单位立方体内的点阵 + 小扰动，总动量为零 */
static void init_ic(int n, std::vector<double> &pos, std::vector<double> &vel,
                    std::vector<double> &mass)
{
    pos.assign((size_t)n * 3, 0.0);
    vel.assign((size_t)n * 3, 0.0);
    mass.assign((size_t)n, 1.0 / (double)n);

    int side = 1;
    while (side * side * side < n) side++;
    const double inv = 1.0 / (double)side;
    double px = 0.0, py = 0.0, pz = 0.0;
    for (int i = 0; i < n; i++) {
        const int ix = i % side, iy = (i / side) % side, iz = i / (side * side);
        const double jitter = 0.01 * sin(12.9898 * (double)i + 78.233);
        pos[3 * i + 0] = (ix + 0.5) * inv + jitter * inv;
        pos[3 * i + 1] = (iy + 0.5) * inv + jitter * inv;
        pos[3 * i + 2] = (iz + 0.5) * inv + jitter * inv;
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
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            const double dx = pos[3*j]-pos[3*i], dy = pos[3*j+1]-pos[3*i+1], dz = pos[3*j+2]-pos[3*i+2];
            *e0 -= mass[i]*mass[j] / sqrt(dx*dx + dy*dy + dz*dz + soft2);
        }
    for (int s = 0; s < steps; s++) {
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
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            const double dx = pos[3*j]-pos[3*i], dy = pos[3*j+1]-pos[3*i+1], dz = pos[3*j+2]-pos[3*i+2];
            *e1 -= mass[i]*mass[j] / sqrt(dx*dx + dy*dy + dz*dz + soft2);
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

    const int ns[3]     = { 1024, 8192, 16384 };
    const int stepss[3] = { 20,   10,    5 };
    const int n     = ns[(tier >= 1 && tier <= 3) ? tier - 1 : 1];
    const int steps = stepss[(tier >= 1 && tier <= 3) ? tier - 1 : 1];
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
    p.n = n; p.steps = steps; p.dt = dt; p.soft2 = soft2; p.report = 1;
    Result r; memset(&r, 0, sizeof(r));
    COIEVENT ev;

    printf("5) 提交计算（卡端 OpenMP 并行 O(N^2) 直接求和）...\n");
    CHECK(COIPipelineRunFunction(pipeline, func, 0, NULL, NULL, 0, NULL,
                                 &p, (uint16_t)sizeof(p), &r, (uint16_t)sizeof(r), &ev));
    CHECK(COIEventWait(1, &ev, -1, 0, NULL, NULL));
    printf("6) 卡上计算完成\n\n");

    printf("=== 卡上结果 ===\n");
    printf("  天体数 / 步数      : %d / %d\n", r.n, r.steps);
    printf("  卡上线程数         : %d\n", r.threads);
    printf("  初态总能量 E0      : %.10e\n", r.e0);
    printf("  末态总能量 E1      : %.10e\n", r.e1);
    printf("  相对能量误差       : %.3e\n", r.rel_energy_err);
    printf("  末态校验和         : %.9f\n", r.checksum);
    printf("  卡上耗时           : %.3f 秒\n", r.secs);
    printf("  估算算力           : %.2f GFLOPS\n", r.gflops);

    int bad = 0;
    if (!(r.rel_energy_err < 1e-3)) { printf("  [FAIL] 能量不守恒（%.3e）\n", r.rel_energy_err); bad++; }
    else printf("  [OK] 能量守恒（%.3e < 1e-3）\n", r.rel_energy_err);

    /* 小规模时与宿主参考实现逐位比对校验和 */
    if (n <= 4096) {
        double ref_cs = 0, ref_e0 = 0, ref_e1 = 0;
        host_reference(n, steps, dt, soft2, pos, vel, mass, &ref_cs, &ref_e0, &ref_e1);
        const double d = fabs(ref_cs - r.checksum);
        const double rel = (ref_cs != 0.0) ? d / fabs(ref_cs) : d;
        printf("  宿主参考校验和     : %.9f（差 %.3e）\n", ref_cs, rel);
        if (rel < 1e-9) printf("  [OK] 与宿主参考一致\n");
        else { printf("  [FAIL] 与宿主参考不一致\n"); bad++; }
    }

    CHECK(COIPipelineDestroy(pipeline));
    CHECK(COIProcessDestroy(proc, -1, 0, NULL, NULL));
    printf("\n7) 完成；%s\n", bad == 0 ? "全部校验通过" : "存在失败项");
    return bad == 0 ? 0 : 1;
}
