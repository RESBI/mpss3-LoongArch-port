// T9 GEMM 卡端 sink：FP32/FP64 矩阵乘法 + OpenMP 并行
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <omp.h>
#include <sys/time.h>
#include <intel-coi/sink/COIPipeline_sink.h>
#include <intel-coi/sink/COIProcess_sink.h>

double walltime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

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

// FP32 GEMM: C = A * B
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
    
    // 初始化：确定性模式
    for (size_t i = 0; i < sz; i++) {
        A[i] = (float)(1.0 + sin(i * 0.001));
        B[i] = (float)(1.0 + cos(i * 0.002));
        C[i] = 0.0f;
    }
    
    omp_set_num_threads(threads);
    double t0 = walltime();
    
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
    
    double t1 = walltime();
    *out_time = t1 - t0;
    
    // 校验和：取对角线和 + 四角
    double cs = 0.0;
    for (uint32_t i = 0; i < N; i++) cs += C[i*N + i];
    cs += C[0] + C[N-1] + C[(N-1)*N] + C[(N-1)*N + N-1];
    *out_checksum = cs;
    
    free(A); free(B); free(C);
}

// FP64 GEMM: C = A * B
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
    
    // 初始化：与 FP32 相同的模式
    for (size_t i = 0; i < sz; i++) {
        A[i] = 1.0 + sin(i * 0.001);
        B[i] = 1.0 + cos(i * 0.002);
        C[i] = 0.0;
    }
    
    omp_set_num_threads(threads);
    double t0 = walltime();
    
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
    
    double t1 = walltime();
    *out_time = t1 - t0;
    
    // 校验和
    double cs = 0.0;
    for (uint32_t i = 0; i < N; i++) cs += C[i*N + i];
    cs += C[0] + C[N-1] + C[(N-1)*N] + C[(N-1)*N + N-1];
    *out_checksum = cs;
    
    free(A); free(B); free(C);
}

// COI 入口函数（标准 COI sink 签名）
extern "C" void GemmRun(uint32_t        in_BufferCount,
                        void          **in_ppBufferPointers,
                        uint64_t       *in_pBufferLengths,
                        void           *in_pMiscData,
                        uint16_t        in_MiscDataLength,
                        void           *in_pReturnValue,
                        uint16_t        in_ReturnValueLength)
{
    (void)in_BufferCount;
    (void)in_ppBufferPointers;
    (void)in_pBufferLengths;
    
    if (in_pMiscData == NULL || in_MiscDataLength != sizeof(GemmParams)) {
        fprintf(stderr, "[sink] 参数错误: in_pMiscData=%p, len=%u, expect=%zu\n",
                in_pMiscData, in_MiscDataLength, sizeof(GemmParams));
        return;
    }
    
    if (in_pReturnValue == NULL || in_ReturnValueLength != sizeof(GemmResult)) {
        fprintf(stderr, "[sink] 返回值缓冲区错误: ptr=%p, len=%u, expect=%zu\n",
                in_pReturnValue, in_ReturnValueLength, sizeof(GemmResult));
        return;
    }
    
    GemmParams *p = (GemmParams*)in_pMiscData;
    GemmResult *r = (GemmResult*)in_pReturnValue;
    memset(r, 0, sizeof(*r));
    
    r->N = p->N;
    r->threads = p->threads;
    
    double time_s, checksum;
    
    if (p->dtype == 0) {
        gemm_fp32(p->N, p->threads, &time_s, &checksum);
    } else {
        gemm_fp64(p->N, p->threads, &time_s, &checksum);
    }
    
    if (time_s < 0) {
        r->success = 0;
        return;
    }
    
    r->time_s = time_s;
    r->checksum = checksum;
    uint64_t flops = 2ULL * p->N * p->N * p->N;
    r->gflops = (double)flops / (time_s * 1e9);
    r->success = 1;
}

// COI sink 主循环
int main(int, char **)
{
    COIPipelineStartExecutingRunFunctions();
    COIProcessWaitForShutdown();
    return 0;
}
