/*
 * offload 演示（宿主侧，运行在龙芯上）—— 用移植版 libcoi_host 把计算交给 KNC 卡。
 *
 * 流程：枚举引擎 → 在卡上创建进程（跑卡端 offload_sink）→ 建管道 →
 *       取卡端函数 CardReduce 的句柄 → 传参数（n）与返回区 → 收结果 → 销毁。
 *
 * 编译（在龙芯上，用移植并安装好的 COI）：
 *   g++ -O2 -I<stage>/usr/include offload_host.cpp -o offload_host \
 *       -L<stage>/usr/lib64 -lcoi_host -Wl,-rpath,<stage>/usr/lib64
 *
 * 运行**不需要 root**：只需要 /dev/mic/scif 对使用者可读写（安装脚本会装好 udev 规则）。
 * 三个参数依次是：卡端程序路径、计算规模 n、k1om 依赖库目录（须与编译卡端时的 -L 目录一致）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <intel-coi/source/COIEngine_source.h>
#include <intel-coi/source/COIProcess_source.h>
#include <intel-coi/source/COIPipeline_source.h>
#include <intel-coi/source/COIEvent_source.h>
#include <intel-coi/common/COIResult_common.h>

struct Result {
    double sum;
    int    threads;
    int    procs;
    double secs;
};

#define CHECK(_expr) do { \
    COIRESULT _r = (_expr); \
    if (_r != COI_SUCCESS) { \
        printf("  失败: %s -> %s(%d)\n", #_expr, COIResultGetName(_r), (int)_r); \
        return 1; \
    } \
} while (0)

int main(int argc, char **argv)
{
    const char *sink_path = (argc > 1) ? argv[1] : "/tmp/offload_sink";
    long n = (argc > 2) ? atol(argv[2]) : 200000000L;
    /* 第四个参数：宿主上的 k1om 依赖库目录。
       宿主侧 COI 会把 sink 的每个依赖读出来校验 ELF Machine（shared_library_finder.cpp:235），
       因此这里必须是装着 k1om 库的宿主目录；不传则回退到 SINK_LD_LIBRARY_PATH。 */
    const char *lib_path = (argc > 3) ? argv[3] : NULL;

    printf("=== 龙芯宿主 -> KNC 卡 offload 演示 ===\n");
    printf("卡端程序: %s   计算规模 n = %ld\n", sink_path, n);
    printf("k1om 依赖库目录: %s\n\n", lib_path ? lib_path : "(用 SINK_LD_LIBRARY_PATH)");

    uint32_t engines = 0;
    CHECK(COIEngineGetCount(COI_DEVICE_MIC, &engines));
    printf("1) 引擎数 = %u\n", engines);
    if (engines < 1) { printf("   没有可用引擎\n"); return 1; }

    COIENGINE engine = NULL;
    CHECK(COIEngineGetHandle(COI_DEVICE_MIC, 0, &engine));
    printf("2) 取到引擎 0 句柄\n");

    COIPROCESS proc = NULL;
    COIRESULT rc = COIProcessCreateFromFile(engine, sink_path, 0, NULL,
                                            false, NULL, true, NULL,
                                            0, lib_path, &proc);
    printf("3) 在卡上创建进程 -> %s(%d)\n", COIResultGetName(rc), (int)rc);
    if (rc != COI_SUCCESS) {
        printf("   卡端二进制是否已放到 %s？是否是链了 libcoi_device 的 COI 程序？\n", sink_path);
        return 1;
    }

    COIPIPELINE pipeline = NULL;
    CHECK(COIPipelineCreate(proc, NULL, 0, &pipeline));
    printf("4) 建立管道\n");

    const char *fname = "CardReduce";
    COIFUNCTION func = NULL;
    CHECK(COIProcessGetFunctionHandles(proc, 1, &fname, &func));
    printf("5) 取到卡端函数句柄: %s\n", fname);

    struct Result result;
    memset(&result, 0, sizeof(result));
    COIEVENT ev;

    printf("6) 提交计算（卡端将用 OpenMP 并行归约）...\n");
    CHECK(COIPipelineRunFunction(pipeline, func,
                                 0, NULL, NULL,            /* 缓冲数、缓冲数组、访问标志 */
                                 0, NULL,                  /* 依赖事件数、依赖数组 */
                                 &n, (uint16_t)sizeof(n),  /* 参数（计算规模 n） */
                                 &result, (uint16_t)sizeof(result),  /* 返回值区 */
                                 &ev));
    CHECK(COIEventWait(1, &ev, -1, 0, NULL, NULL));
    printf("7) 卡上计算完成\n\n");

    double expect = (double)(n - 1) * (double)n * (2.0 * (double)n - 1.0) / (6.0 * (double)n * (double)n);
    printf("=== 结果（全部在卡上算出）===\n");
    printf("  卡上硬件线程数     : %d\n", result.procs);
    printf("  实际参与线程数     : %d\n", result.threads);
    printf("  归约结果 sum       : %.12f\n", result.sum);
    printf("  离散和解析值       : %.12f\n", expect);
    printf("  相对误差           : %.3e\n", (result.sum - expect) / expect);
    printf("  卡上耗时           : %.3f 秒\n", result.secs);

    CHECK(COIPipelineDestroy(pipeline));
    CHECK(COIProcessDestroy(proc, -1, 0, NULL, NULL));
    printf("\n8) 管道与进程已销毁；offload 端到端完成\n");
    return 0;
}
