/*
 * T9-VEC 宿主端：驱动 gemm_vec_sink（KNC 显式 IMCI 向量化 GEMM）
 * ============================================================================
 * 参考实现说明：
 *   卡端只回传"全矩阵元素和"的 16 个向量车道（见 gemm_vec_sink.cpp），
 *   宿主用恒等式在 O(N^2) 内算出同一标量：
 *       sum_ij C[i][j] = sum_i sum_k A[i][k] * rowsumB[k],  rowsumB[k] = sum_j B[k][j]
 *   数据模式卡端/宿主完全一致：
 *       A[i] = 1 + ( i      % 64)/64
 *       B[i] = 1 + ((63 - i % 64))/64
 *   小规模(N<=512)额外做一次完整 O(N^3) 参考，交叉验证上面的恒等式本身没写错。
 * ============================================================================
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <sys/time.h>

#include <intel-coi/source/COIEngine_source.h>
#include <intel-coi/source/COIProcess_source.h>
#include <intel-coi/source/COIPipeline_source.h>
#include <intel-coi/source/COIEvent_source.h>
#include <intel-coi/common/COIResult_common.h>

/* 必须与 gemm_vec_sink.cpp 逐字节一致 */
struct VParams {
    uint32_t N;
    uint32_t threads;
    uint32_t dtype;
    uint32_t pad;
};

struct VResult {
    float    lanes[16];
    int64_t  time_us;
    int64_t  time_us_cold;
    uint32_t N;
    uint32_t threads;
    uint32_t tiles;
    int32_t  success;
} __attribute__((aligned(64)));

static double walltime(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

/* ---- 与卡端常量表逐位一致的数据模式（必须与 gemm_vec_sink.cpp 的注释一致）----
 *   A[i][k] = 1 + ( (k + 16*(i%4)) % 64 ) / 64
 *   B[k][j] = 1 + ( 63 - ((j + 16*(k%4)) % 64) ) / 64
 * 模式同时随行、列变化，所以行/列索引写错一定会改变全矩阵和。 */
static inline double pat_A(size_t i, size_t k)
{
    return 1.0 + (double)((k + 16 * (i & 3)) % 64) / 64.0;
}
static inline double pat_B(size_t k, size_t j)
{
    return 1.0 + (double)(63 - ((j + 16 * (k & 3)) % 64)) / 64.0;
}

/* O(N^2) 的精确参考：sum_ij C[i][j] = sum_i sum_k A[i][k]*rowsumB[k] */
static double ref_sum_fast(uint32_t N)
{
    double *rsB = (double *)malloc((size_t)N * sizeof(double));
    if (!rsB) return -1.0;
    for (uint32_t k = 0; k < N; k++) {
        double s = 0.0;
        for (uint32_t j = 0; j < N; j++) s += pat_B(k, j);
        rsB[k] = s;
    }
    double total = 0.0;
    for (uint32_t i = 0; i < N; i++) {
        double row = 0.0;
        for (uint32_t k = 0; k < N; k++) row += pat_A(i, k) * rsB[k];
        total += row;
    }
    free(rsB);
    return total;
}

/* O(N^3) 全量参考（仅小 N 用），同时独立验证上面的恒等式 */
static double ref_sum_full(uint32_t N)
{
    double total = 0.0;
    for (uint32_t i = 0; i < N; i++) {
        for (uint32_t j = 0; j < N; j++) {
            double s = 0.0;
            for (uint32_t k = 0; k < N; k++)
                s += pat_A(i, k) * pat_B(k, j);
            total += s;
        }
    }
    return total;
}

static double sum_lanes_f32(const VResult *r)
{
    double s = 0.0;
    for (int i = 0; i < 16; i++) s += (double)r->lanes[i];
    return s;
}

static double sum_lanes_f64(const VResult *r)
{
    double d[8];
    memcpy(d, r->lanes, sizeof(d));
    double s = 0.0;
    for (int i = 0; i < 8; i++) s += d[i];
    return s;
}

static int run_one(COIPIPELINE pipeline, COIFUNCTION func,
                   uint32_t N, uint32_t threads, uint32_t dtype, int verbose)
{
    const char *tag = (dtype == 0) ? "FP32" : "FP64";

    double ref = ref_sum_fast(N);
    if (ref < 0.0) { printf("  宿主参考计算失败\n"); return 1; }

    if (verbose && N <= 512) {
        double full = ref_sum_full(N);
        double d = fabs(full - ref) / (fabs(full) + 1e-30);
        printf("  宿主参考自检(O(N^3) vs O(N^2)): %.9e 相对差 %.2e\n", full, d);
    }

    VParams p;
    memset(&p, 0, sizeof(p));
    p.N = N; p.threads = threads; p.dtype = dtype;

    VResult r;
    memset(&r, 0, sizeof(r));

    double t0 = walltime();
    COIEVENT ev;
    COIRESULT res = COIPipelineRunFunction(pipeline, func, 0, NULL, NULL, 0, NULL,
                                           &p, (uint16_t)sizeof(p),
                                           &r, (uint16_t)sizeof(r), &ev);
    if (res != COI_SUCCESS) { printf("  COIPipelineRunFunction 失败: %d\n", res); return 1; }
    res = COIEventWait(1, &ev, -1, 0, NULL, NULL);
    if (res != COI_SUCCESS) { printf("  COIEventWait 失败: %d\n", res); return 1; }
    double wall = walltime() - t0;

    if (!r.success) { printf("  [%s] 卡端执行失败（success=0，N 需为 16 与 MR 的整数倍）\n", tag); return 1; }

    double chip = (dtype == 0) ? sum_lanes_f32(&r) : sum_lanes_f64(&r);
    double secs = (double)r.time_us * 1e-6;
    double secs_cold = (double)r.time_us_cold * 1e-6;
    double flops = 2.0 * (double)N * (double)N * (double)N;
    double gflops = (secs > 0.0) ? flops / secs / 1e9 : 0.0;
    double gflops_cold = (secs_cold > 0.0) ? flops / secs_cold / 1e9 : 0.0;
    double rel = fabs(chip - ref) / (fabs(ref) + 1e-30);

    printf("  [%s] N=%u threads=%u tiles=%u\n", tag, N, threads, r.tiles);
    printf("       卡端耗时   : %.6f s  (热；OFFLOAD 往返 %.3f s)\n", secs, wall);
    printf("       卡端算力   : %.2f GFLOPS\n", gflops);
    printf("       冷启动对比 : %.6f s / %.2f GFLOPS  (线程场创建等一次性开销 %.1f ms)\n",
           secs_cold, gflops_cold, (secs_cold - secs) * 1e3);
    printf("       卡端验和   : %.9e\n", chip);
    printf("       宿主参考   : %.9e\n", ref);
    printf("       相对误差   : %.3e\n", rel);

    int pass = (rel < 1e-3);
    printf("       %s %s\n", pass ? "✓" : "✗", pass ? "通过" : "校验和不匹配");
    return pass ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "用法: %s <sink> <coslib> <N> <threads> [dtype:0|1|2]\n", argv[0]);
        fprintf(stderr, "      dtype 0=FP32 1=FP64 2=两种都跑\n");
        return 1;
    }
    const char *sink_path = argv[1];
    const char *coslib    = argv[2];
    uint32_t N            = (uint32_t)strtoul(argv[3], NULL, 10);
    uint32_t threads      = (uint32_t)strtoul(argv[4], NULL, 10);
    int dtype             = (argc > 5) ? atoi(argv[5]) : 2;

    COIENGINE engine;
    uint32_t num_engines = 0;
    COIRESULT res = COIEngineGetCount(COI_ISA_KNC, &num_engines);
    if (res != COI_SUCCESS || num_engines == 0) {
        fprintf(stderr, "COIEngineGetCount 失败或无 KNC 设备: %d\n", res);
        return 1;
    }
    res = COIEngineGetHandle(COI_ISA_KNC, 0, &engine);
    if (res != COI_SUCCESS) { fprintf(stderr, "COIEngineGetHandle 失败: %d\n", res); return 1; }

    COIPROCESS process;
    res = COIProcessCreateFromFile(engine, sink_path, 0, NULL,
                                   false, NULL, true, NULL, 0, coslib, &process);
    if (res != COI_SUCCESS) { fprintf(stderr, "COIProcessCreateFromFile 失败: %d\n", res); return 1; }

    COIPIPELINE pipeline;
    res = COIPipelineCreate(process, NULL, 0, &pipeline);
    if (res != COI_SUCCESS) { fprintf(stderr, "COIPipelineCreate 失败: %d\n", res); return 1; }

    const char *fname = "GemmVecRun";
    COIFUNCTION func;
    res = COIProcessGetFunctionHandles(process, 1, &fname, &func);
    if (res != COI_SUCCESS) { fprintf(stderr, "COIProcessGetFunctionHandles(GemmVecRun) 失败: %d\n", res); return 1; }

    printf("T9-VEC KNC 显式向量化 GEMM: N=%u threads=%u\n", N, threads);

    int rc = 0;
    if (dtype == 0 || dtype == 2) rc |= run_one(pipeline, func, N, threads, 0, 1);
    if (dtype == 1 || dtype == 2) rc |= run_one(pipeline, func, N, threads, 1, 1);
    return rc;
}
