/*
 * offload 演示（卡端）—— 在 KNC 卡上执行被宿主调用的计算函数，函数内部用 OpenMP。
 *
 * 编译（在龙芯上，用 MPSS 的 k1om 交叉编译器；-rdynamic 必须，否则符号不在 .dynsym）：
 *   k1om-cxx -O2 -fopenmp -rdynamic -I<k1om sysroot>/usr/include offload_sink.cpp \
 *       -L<k1om sysroot>/usr/lib64 -lcoi_device -L<k1om 依赖库目录> -lgomp \
 *       -lpthread -ldl -lrt -Wl,-rpath,/tmp -o offload_sink
 *
 * 优化档说明：本文件实测 -O2 可用（17 项检查全过）；N 体示例（nbody_sink.cpp）在 -O1／-O2 下
 * 会崩，只能用 -O0。优化档是逐个源码的事，换档后务必重跑验收。详见 OFFLOAD_GUIDE_CN.md §5.2。
 *
 * 运行方式：由卡上的 coi_daemon 在宿主调用 COIProcessCreateFromFile 时拉起（无需手工部署）。
 */
#include <stdio.h>
#include <string.h>
#include <omp.h>

#include <intel-coi/sink/COIPipeline_sink.h>
#include <intel-coi/sink/COIProcess_sink.h>
#include <intel-coi/common/COIMacros_common.h>

struct Result {
    double sum;        /* OpenMP 归约结果 */
    int    threads;    /* 实际参与线程数 */
    int    procs;      /* 卡上可用硬件线程数 */
    double secs;       /* 卡上耗时 */
};

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    /* 必须在收到任何 run function 之前调用，完成卡侧初始化 */
    COIPipelineStartExecutingRunFunctions();
    /* 等宿主销毁进程；在此之前所有入队函数都会被调度执行 */
    COIProcessWaitForShutdown();
    return 0;
}

/* 宿主通过 COIProcessGetFunctionHandles("CardReduce") 取到这个函数并调用 */
COINATIVELIBEXPORT
void CardReduce(uint32_t        in_BufferCount,
                void          **in_ppBufferPointers,
                uint64_t       *in_pBufferLengths,
                void           *in_pMiscData,
                uint16_t        in_MiscDataLength,
                void           *in_pReturnValue,
                uint16_t        in_ReturnValueLength)
{
    (void)in_BufferCount; (void)in_ppBufferPointers; (void)in_pBufferLengths;

    struct Result *r = (struct Result *)in_pReturnValue;
    long n = 0;
    if (in_pMiscData != NULL && in_MiscDataLength >= sizeof(long))
        n = *(long *)in_pMiscData;
    if (r == NULL || in_ReturnValueLength < sizeof(*r))
        return;

    memset(r, 0, sizeof(*r));

    double t0 = omp_get_wtime();
    double sum = 0.0;
    #pragma omp parallel for reduction(+:sum)
    for (long i = 0; i < n; i++) {
        double x = (double)i / (double)n;
        sum += x * x;
    }
    double t1 = omp_get_wtime();

    int thr = 0;
    #pragma omp parallel
    {
        #pragma omp master
        thr = omp_get_num_threads();
    }

    r->sum     = sum;
    r->threads = thr;
    r->procs   = omp_get_num_procs();
    r->secs    = t1 - t0;
}
