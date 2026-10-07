// T9 GEMM 宿主端：浮点性能基准测试
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <sys/time.h>

#include <intel-coi/source/COIEngine_source.h>
#include <intel-coi/source/COIProcess_source.h>
#include <intel-coi/source/COIPipeline_source.h>
#include <intel-coi/source/COIEvent_source.h>
#include <intel-coi/common/COIResult_common.h>

struct GemmParams {
    uint32_t N;
    uint32_t threads;
    uint32_t dtype;  // 0=fp32, 1=fp64
};

struct GemmResult {
    uint32_t N;
    uint32_t threads;
    double time_s;
    double gflops;
    double checksum;
    int success;
};

double walltime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

// 宿主参考实现 FP32
double host_gemm_fp32(uint32_t N, double *out_checksum) {
    size_t sz = (size_t)N * N;
    float *A = (float*)malloc(sz * sizeof(float));
    float *B = (float*)malloc(sz * sizeof(float));
    float *C = (float*)malloc(sz * sizeof(float));
    
    if (!A || !B || !C) {
        free(A); free(B); free(C);
        return -1.0;
    }
    
    for (size_t i = 0; i < sz; i++) {
        A[i] = (float)(1.0 + sin(i * 0.001));
        B[i] = (float)(1.0 + cos(i * 0.002));
        C[i] = 0.0f;
    }
    
    double t0 = walltime();
    for (uint32_t i = 0; i < N; i++) {
        for (uint32_t j = 0; j < N; j++) {
            float sum = 0.0f;
            for (uint32_t k = 0; k < N; k++) {
                sum += A[i*N + k] * B[k*N + j];
            }
            C[i*N + j] = sum;
        }
    }
    double t1 = walltime();
    
    double cs = 0.0;
    for (uint32_t i = 0; i < N; i++) cs += C[i*N + i];
    cs += C[0] + C[N-1] + C[(N-1)*N] + C[(N-1)*N + N-1];
    *out_checksum = cs;
    
    free(A); free(B); free(C);
    return t1 - t0;
}

// 宿主参考实现 FP64
double host_gemm_fp64(uint32_t N, double *out_checksum) {
    size_t sz = (size_t)N * N;
    double *A = (double*)malloc(sz * sizeof(double));
    double *B = (double*)malloc(sz * sizeof(double));
    double *C = (double*)malloc(sz * sizeof(double));
    
    if (!A || !B || !C) {
        free(A); free(B); free(C);
        return -1.0;
    }
    
    for (size_t i = 0; i < sz; i++) {
        A[i] = 1.0 + sin(i * 0.001);
        B[i] = 1.0 + cos(i * 0.002);
        C[i] = 0.0;
    }
    
    double t0 = walltime();
    for (uint32_t i = 0; i < N; i++) {
        for (uint32_t j = 0; j < N; j++) {
            double sum = 0.0;
            for (uint32_t k = 0; k < N; k++) {
                sum += A[i*N + k] * B[k*N + j];
            }
            C[i*N + j] = sum;
        }
    }
    double t1 = walltime();
    
    double cs = 0.0;
    for (uint32_t i = 0; i < N; i++) cs += C[i*N + i];
    cs += C[0] + C[N-1] + C[(N-1)*N] + C[(N-1)*N + N-1];
    *out_checksum = cs;
    
    free(A); free(B); free(C);
    return t1 - t0;
}

// FP32 测试
bool test_fp32(COIPIPELINE pipeline, COIFUNCTION func, int N, int threads) {
    printf("\n[FP32 测试]\n");
    
    // 宿主参考
    printf("  宿主参考计算中...\n");
    double host_cs, host_time;
    host_time = host_gemm_fp32(N, &host_cs);
    if (host_time < 0) {
        printf("  宿主内存分配失败\n");
        return false;
    }
    printf("  宿主用时: %.3f s\n", host_time);
    printf("  宿主校验和: %.9f\n", host_cs);
    
    // 卡端
    GemmParams p;
    p.N = N;
    p.threads = threads;
    p.dtype = 0;  // FP32
    
    GemmResult r;
    memset(&r, 0, sizeof(r));
    
    COIEVENT ev;
    COIRESULT res = COIPipelineRunFunction(pipeline, func, 0, NULL, NULL, 0, NULL,
                                           &p, (uint16_t)sizeof(p), &r, (uint16_t)sizeof(r), &ev);
    if (res != COI_SUCCESS) {
        printf("  COIPipelineRunFunction 失败: %d\n", res);
        return false;
    }
    
    res = COIEventWait(1, &ev, -1, 0, NULL, NULL);
    if (res != COI_SUCCESS) {
        printf("  COIEventWait 失败: %d\n", res);
        return false;
    }
    
    if (!r.success) {
        printf("  卡端执行失败（可能内存不足）\n");
        return false;
    }
    
    printf("  卡端用时: %.3f s\n", r.time_s);
    printf("  卡端校验和: %.9f\n", r.checksum);
    printf("  卡端算力: %.2f GFLOPS\n", r.gflops);
    printf("  卡端线程数: %u\n", r.threads);
    
    // 校验
    double rel_err = fabs(r.checksum - host_cs) / (fabs(host_cs) + 1e-12);
    printf("  校验和相对误差: %.3e\n", rel_err);
    
    bool pass = (rel_err < 1e-4);
    if (pass) {
        printf("  ✓ FP32 通过\n");
    } else {
        printf("  ✗ FP32 失败（校验和不匹配）\n");
    }
    
    return pass;
}

// FP64 测试
bool test_fp64(COIPIPELINE pipeline, COIFUNCTION func, int N, int threads) {
    printf("\n[FP64 测试]\n");
    
    // 宿主参考
    printf("  宿主参考计算中...\n");
    double host_cs, host_time;
    host_time = host_gemm_fp64(N, &host_cs);
    if (host_time < 0) {
        printf("  宿主内存分配失败\n");
        return false;
    }
    printf("  宿主用时: %.3f s\n", host_time);
    printf("  宿主校验和: %.9f\n", host_cs);
    
    // 卡端
    GemmParams p;
    p.N = N;
    p.threads = threads;
    p.dtype = 1;  // FP64
    
    GemmResult r;
    memset(&r, 0, sizeof(r));
    
    COIEVENT ev;
    COIRESULT res = COIPipelineRunFunction(pipeline, func, 0, NULL, NULL, 0, NULL,
                                           &p, (uint16_t)sizeof(p), &r, (uint16_t)sizeof(r), &ev);
    if (res != COI_SUCCESS) {
        printf("  COIPipelineRunFunction 失败: %d\n", res);
        return false;
    }
    
    res = COIEventWait(1, &ev, -1, 0, NULL, NULL);
    if (res != COI_SUCCESS) {
        printf("  COIEventWait 失败: %d\n", res);
        return false;
    }
    
    if (!r.success) {
        printf("  卡端执行失败（可能内存不足）\n");
        return false;
    }
    
    printf("  卡端用时: %.3f s\n", r.time_s);
    printf("  卡端校验和: %.9f\n", r.checksum);
    printf("  卡端算力: %.2f GFLOPS\n", r.gflops);
    printf("  卡端线程数: %u\n", r.threads);
    
    // 校验
    double rel_err = fabs(r.checksum - host_cs) / (fabs(host_cs) + 1e-12);
    printf("  校验和相对误差: %.3e\n", rel_err);
    
    bool pass = (rel_err < 1e-10);
    if (pass) {
        printf("  ✓ FP64 通过\n");
    } else {
        printf("  ✗ FP64 失败（校验和不匹配）\n");
    }
    
    return pass;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "用法: %s <sink> <coslib> <档位 1|2|3>\n", argv[0]);
        return 1;
    }
    
    const char *sink_path = argv[1];
    const char *coslib = argv[2];
    int tier = atoi(argv[3]);
    
    const int Ns[] = {256, 1024, 2048};
    const int threads = 60;
    
    if (tier < 1 || tier > 3) tier = 1;
    int N = Ns[tier - 1];
    
    printf("T9 GEMM 档位 %d: N=%d, threads=%d\n", tier, N, threads);

    // 初始化 COI
    COIENGINE engine;
    uint32_t num_engines;
    COIRESULT res = COIEngineGetCount(COI_ISA_KNC, &num_engines);
    if (res != COI_SUCCESS || num_engines == 0) {
        fprintf(stderr, "COIEngineGetCount 失败或无 KNC 设备: %d\n", res);
        return 1;
    }
    res = COIEngineGetHandle(COI_ISA_KNC, 0, &engine);
    if (res != COI_SUCCESS) {
        fprintf(stderr, "COIEngineGetHandle 失败: %d\n", res);
        return 1;
    }

    // 创建进程
    COIPROCESS process;
    res = COIProcessCreateFromFile(engine, sink_path, 0, NULL,
                                    false, NULL, true, NULL, 0, coslib, &process);
    if (res != COI_SUCCESS) {
        fprintf(stderr, "COIProcessCreateFromFile 失败: %d\n", res);
        return 1;
    }

    // 创建管线
    COIPIPELINE pipeline;
    res = COIPipelineCreate(process, NULL, 0, &pipeline);
    if (res != COI_SUCCESS) {
        fprintf(stderr, "COIPipelineCreate 失败: %d\n", res);
        return 1;
    }

    // 获取函数
    const char *fname = "GemmRun";
    COIFUNCTION func;
    res = COIProcessGetFunctionHandles(process, 1, &fname, &func);
    if (res != COI_SUCCESS) {
        fprintf(stderr, "COIProcessGetFunctionHandles 失败: %d\n", res);
        return 1;
    }

    // 运行测试
    bool fp32_pass = test_fp32(pipeline, func, N, threads);
    bool fp64_pass = test_fp64(pipeline, func, N, threads);

    // 汇总
    printf("\n=== 档位 %d 汇总: %d/2 通过 ===\n", tier, (fp32_pass ? 1 : 0) + (fp64_pass ? 1 : 0));

    // 清理
    COIPipelineDestroy(pipeline);
    COIProcessDestroy(process, -1, 0, NULL, NULL);

    return (fp32_pass && fp64_pass) ? 0 : 1;
}
