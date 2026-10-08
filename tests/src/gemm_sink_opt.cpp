/*
 * 卡端 GEMM：-O2 自动向量化版本
 * 
 * 策略：
 *   1. 写简单的循环，让编译器用 -march=knc -O2 自动向量化
 *   2. 分块改善缓存局部性
 *   3. OpenMP 并行
 *   4. 避免任何显式 intrinsics
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <omp.h>
#include <sys/time.h>
#include <math.h>
#include <intel-coi/sink/COIPipeline_sink.h>
#include <intel-coi/sink/COIProcess_sink.h>

static inline long long walltime_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + (long long)tv.tv_usec;
}

// ============ FP32 GEMM（分块 + 自动向量化）============
void gemm_fp32_opt(uint32_t N, uint32_t threads, double *out_time, double *out_checksum) {
    const size_t sz = (size_t)N * N;
    float *A = (float*)aligned_alloc(64, sz * sizeof(float));
    float *B = (float*)aligned_alloc(64, sz * sizeof(float));
    float *C = (float*)aligned_alloc(64, sz * sizeof(float));
    
    if (!A || !B || !C) {
        *out_time = -1.0;
        *out_checksum = 0.0;
        free(A); free(B); free(C);
        return;
    }
    
    // 初始化（与标量版保持一致）
    #pragma omp parallel for
    for (size_t i = 0; i < sz; i++) {
        A[i] = 1.0f + sinf((float)i * 0.001f);
        B[i] = 1.0f + cosf((float)i * 0.002f);
        C[i] = 0.0f;
    }
    
    omp_set_num_threads(threads);
    long long t0 = walltime_us();
    
    // GEMM 核心：分块优化
    const uint32_t BLOCK = 64;
    
    #pragma omp parallel for collapse(2)
    for (uint32_t ii = 0; ii < N; ii += BLOCK) {
        for (uint32_t jj = 0; jj < N; jj += BLOCK) {
            uint32_t imax = (ii + BLOCK < N) ? ii + BLOCK : N;
            uint32_t jmax = (jj + BLOCK < N) ? jj + BLOCK : N;
            
            for (uint32_t kk = 0; kk < N; kk += BLOCK) {
                uint32_t kmax = (kk + BLOCK < N) ? kk + BLOCK : N;
                
                for (uint32_t i = ii; i < imax; i++) {
                    for (uint32_t k = kk; k < kmax; k++) {
                        float a_ik = A[i * N + k];
                        
                        // 内层循环：让编译器自动向量化
                        #pragma omp simd
                        for (uint32_t j = jj; j < jmax; j++) {
                            C[i * N + j] += a_ik * B[k * N + j];
                        }
                    }
                }
            }
        }
    }
    
    long long t1 = walltime_us();
    *out_time = (double)(t1 - t0) * 1e-6;
    
    // 校验和
    double cs_local = 0.0;
    #pragma omp parallel for reduction(+:cs_local)
    for (uint32_t i = 0; i < N; i++) {
        cs_local += (double)C[i * N + i];
    }
    cs_local += (double)(C[0] + C[N-1] + C[(N-1)*N] + C[(N-1)*N + N-1]);
    *out_checksum = cs_local;
    
    free(A); free(B); free(C);
}

// ============ FP64 GEMM（分块 + 自动向量化）============
void gemm_fp64_opt(uint32_t N, uint32_t threads, double *out_time, double *out_checksum) {
    const size_t sz = (size_t)N * N;
    double *A = (double*)aligned_alloc(64, sz * sizeof(double));
    double *B = (double*)aligned_alloc(64, sz * sizeof(double));
    double *C = (double*)aligned_alloc(64, sz * sizeof(double));
    
    if (!A || !B || !C) {
        *out_time = -1.0;
        *out_checksum = 0.0;
        free(A); free(B); free(C);
        return;
    }
    
    #pragma omp parallel for
    for (size_t i = 0; i < sz; i++) {
        A[i] = 1.0 + sin((double)i * 0.001);
        B[i] = 1.0 + cos((double)i * 0.002);
        C[i] = 0.0;
    }
    
    omp_set_num_threads(threads);
    long long t0 = walltime_us();
    
    const uint32_t BLOCK = 64;
    
    #pragma omp parallel for collapse(2)
    for (uint32_t ii = 0; ii < N; ii += BLOCK) {
        for (uint32_t jj = 0; jj < N; jj += BLOCK) {
            uint32_t imax = (ii + BLOCK < N) ? ii + BLOCK : N;
            uint32_t jmax = (jj + BLOCK < N) ? jj + BLOCK : N;
            
            for (uint32_t kk = 0; kk < N; kk += BLOCK) {
                uint32_t kmax = (kk + BLOCK < N) ? kk + BLOCK : N;
                
                for (uint32_t i = ii; i < imax; i++) {
                    for (uint32_t k = kk; k < kmax; k++) {
                        double a_ik = A[i * N + k];
                        
                        #pragma omp simd
                        for (uint32_t j = jj; j < jmax; j++) {
                            C[i * N + j] += a_ik * B[k * N + j];
                        }
                    }
                }
            }
        }
    }
    
    long long t1 = walltime_us();
    *out_time = (double)(t1 - t0) * 1e-6;
    
    double cs_local = 0.0;
    #pragma omp parallel for reduction(+:cs_local)
    for (uint32_t i = 0; i < N; i++) {
        cs_local += C[i * N + i];
    }
    cs_local += C[0] + C[N-1] + C[(N-1)*N] + C[(N-1)*N + N-1];
    *out_checksum = cs_local;
    
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
        gemm_fp64_opt(p->N, p->threads, &time_s, &checksum);
    } else {
        gemm_fp32_opt(p->N, p->threads, &time_s, &checksum);
    }
    
    r->time_s = time_s;
    r->checksum = checksum;
    
    // 计算 GFLOPS: 2*N^3 flops
    double flops = 2.0 * (double)p->N * (double)p->N * (double)p->N;
    r->gflops = (time_s > 0) ? (flops / time_s / 1e9) : 0.0;
    r->success = (time_s > 0) ? 1 : 0;
}

int main(int, char **)
{
    COIPipelineStartExecutingRunFunctions();
    COIProcessWaitForShutdown();
    return 0;
}
