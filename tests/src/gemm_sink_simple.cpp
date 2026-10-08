#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <omp.h>
#include <sys/time.h>
#include <intel-coi/sink/COIPipeline_sink.h>
#include <intel-coi/sink/COIProcess_sink.h>

// 时间戳（微秒）
static inline long long walltime_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + (long long)tv.tv_usec;
}

// ============ FP32 GEMM（标量版，-O0 编译避免标量浮点指令）============
void gemm_fp32(uint32_t N, uint32_t threads, double *out_time, double *out_checksum) {
    size_t sz = (size_t)N * N;
    float *A = (float*)malloc(sz * sizeof(float));
    float *B = (float*)malloc(sz * sizeof(float));
    float *C = (float*)malloc(sz * sizeof(float));
    
    if (!A || !B || !C) {
        *out_time = -1.0;
        *out_checksum = 0.0;
        free(A); free(B); free(C);
        return;
    }
    
    // 初始化：用库函数（-O0 不会内联）
    for (size_t i = 0; i < sz; i++) {
        float fi = (float)i * 0.001f;
        A[i] = 1.0f + sinf(fi);
        B[i] = 1.0f + cosf(fi * 2.0f);
        C[i] = 0.0f;
    }
    
    omp_set_num_threads(threads);
    long long t0 = walltime_us();
    
    // GEMM 核心
    #pragma omp parallel for collapse(2)
    for (uint32_t i = 0; i < N; i++) {
        for (uint32_t j = 0; j < N; j++) {
            float sum = 0.0f;
            for (uint32_t k = 0; k < N; k++) {
                sum += A[i*N + k] * B[k*N + j];
            }
            C[i*N + j] = sum;
        }
    }
    
    long long t1 = walltime_us();
    *out_time = (double)(t1 - t0) * 1e-6;
    
    // 校验和
    float cs = 0.0f;
    for (uint32_t i = 0; i < N; i++) cs += C[i*N + i];
    cs += C[0] + C[N-1] + C[(N-1)*N] + C[(N-1)*N + N-1];
    *out_checksum = (double)cs;
    
    free(A); free(B); free(C);
}

// ============ FP64 GEMM ============
void gemm_fp64(uint32_t N, uint32_t threads, double *out_time, double *out_checksum) {
    size_t sz = (size_t)N * N;
    double *A = (double*)malloc(sz * sizeof(double));
    double *B = (double*)malloc(sz * sizeof(double));
    double *C = (double*)malloc(sz * sizeof(double));
    
    if (!A || !B || !C) {
        *out_time = -1.0;
        *out_checksum = 0.0;
        free(A); free(B); free(C);
        return;
    }
    
    for (size_t i = 0; i < sz; i++) {
        double di = (double)i * 0.001;
        A[i] = 1.0 + sin(di);
        B[i] = 1.0 + cos(di * 2.0);
        C[i] = 0.0;
    }
    
    omp_set_num_threads(threads);
    long long t0 = walltime_us();
    
    #pragma omp parallel for collapse(2)
    for (uint32_t i = 0; i < N; i++) {
        for (uint32_t j = 0; j < N; j++) {
            double sum = 0.0;
            for (uint32_t k = 0; k < N; k++) {
                sum += A[i*N + k] * B[k*N + j];
            }
            C[i*N + j] = sum;
        }
    }
    
    long long t1 = walltime_us();
    *out_time = (double)(t1 - t0) * 1e-6;
    
    double cs = 0.0;
    for (uint32_t i = 0; i < N; i++) cs += C[i*N + i];
    cs += C[0] + C[N-1] + C[(N-1)*N] + C[(N-1)*N + N-1];
    *out_checksum = cs;
    
    free(A); free(B); free(C);
}

// ============ COI 入口 ============
extern "C" void GemmRun(uint32_t        in_BufferCount,
                        void          **in_ppBufferPointers,
                        uint64_t       *in_pBufferLengths,
                        void           *in_pMiscData,
                        uint16_t        in_MiscDataLength,
                        void           *in_pReturnValue,
                        uint16_t        in_ReturnValueLength)
{
    struct Params {
        uint32_t N;
        uint32_t threads;
        uint32_t dtype;  // 0=fp32, 1=fp64
    };
    
    struct Result {
        uint32_t N;
        uint32_t threads;
        double time_s;
        double gflops;
        double checksum;
        int success;
    };
    
    if (in_MiscDataLength < sizeof(Params) || in_ReturnValueLength < sizeof(Result)) {
        return;
    }
    
    Params *p = (Params*)in_pMiscData;
    Result *r = (Result*)in_pReturnValue;
    memset(r, 0, sizeof(Result));
    
    r->N = p->N;
    r->threads = omp_get_max_threads();
    
    double time_s, checksum;
    if (p->dtype == 1) {
        gemm_fp64(p->N, p->threads, &time_s, &checksum);
    } else {
        gemm_fp32(p->N, p->threads, &time_s, &checksum);
    }
    
    r->time_s = time_s;
    r->checksum = checksum;
    
    // 计算 GFLOPS: 2*N^3 flops
    double flops = 2.0 * (double)p->N * (double)p->N * (double)p->N;
    r->gflops = (time_s > 0) ? (flops / time_s / 1e9) : 0.0;
    r->success = (time_s > 0) ? 1 : 0;
}

// ============ COI Sink 入口点 ============
int main(int, char **)
{
    COIPipelineStartExecutingRunFunctions();
    COIProcessWaitForShutdown();
    return 0;
}
