/*
 * MIC/SCIF 诊断打印开关。
 *
 * 这些探针是移植期用来定位页大小与窗口簿记问题的（见 docs/I-页大小对齐调查）。
 * 默认编译成 pr_debug：不打开 dynamic debug 就不会输出，也不污染 dmesg ——
 * 一次 4 GiB 传输在全部打开时会写两万多行日志。
 *
 * 需要时在构建时打开：
 *     make MIC_DEBUG=1            # 构建
 *     make install MIC_DEBUG=1    # 构建并安装
 * 打开后它们编译成 pr_info，直接进 dmesg（也可用 dynamic debug 单独打开某些点）。
 */
#ifndef _MIC_DEBUG_H_
#define _MIC_DEBUG_H_

#ifdef MIC_SCIF_DEBUG_PRINT
#define mic_dbg(fmt, ...)	pr_info(fmt, ##__VA_ARGS__)
#else
#define mic_dbg(fmt, ...)	pr_debug(fmt, ##__VA_ARGS__)
#endif

#endif /* _MIC_DEBUG_H_ */
