# T9: GEMM 等算子浮点性能基准测试

> ## ⚠️ 实现状态与实测结果（2026-10-08 更新）
>
> 本文档最初是一份**计划书**。实际落地的与本计划有两处差异，实测性能也与本文
> 第 122 行「显式向量化可达 30%–50% 峰值」的乐观估算有明显距离，以此处为准。
>
> **实际实现**
> - 档位是 **3 档：N = 256 / 1024 / 2048**（不是本文列的 512…8192 五档）。
> - 基线脚本 `t9_gemm_bench.sh`（标量三重循环，卡端 `-O0`）。
> - 优化版脚本 `t9_gemm_vec.sh`（卡端 `-O2 -mavx512f` + 手写 IMCI intrinsics）。
>
> **实测（N=2048，244 线程，Xeon Phi 7120P）**
>
> | 实现 | 编译方式 | FP32 GFLOPS | FP64 GFLOPS | 占峰值 |
> |---|---|---|---|---|
> | 标量三重循环 | `-O0` | 0.33 | 0.33 | 0.014% |
> | 分块（**未向量化**） | `-O2 -march=knc` | 6.39 | 3.89 | 0.26% |
> | **显式 `_mm512_*` intrinsics** | **`-O2 -mavx512f`** | **147–261** | **120–138** | **6–11%** |
>
> 理论峰值按 61 核 × 1.238 GHz × 32 FLOP/周期计：FP32 ≈ 2416、FP64 ≈ 1208 GFLOPS。
>
> **对本文两条结论的修正**
> 1. 第 117 行「依赖编译器自动向量化」**行不通**：k1om 后端在任何 `-O` 档都
>    不向量化（反汇编里 `vfmadd` 计数恒为 0）。必须手写 intrinsics。
> 2. 第 122 行「显式向量化可达 30%–50%」**偏乐观**：实测 6–11%。剩余差距主要
>    来自每核访存停顿，而非 FMA 吞吐（内层循环已是理想形态）。
>
> **权威文档**：优化全过程已抽成独立系列报告放在 `../docs/`，逐步记录假设、
> 改动与实测：
> - [`N1` — 总览与结果](../docs/N1-t9-gemm-optimization-overview_CN.md)（先读这篇）
> - [`N2` — 挡住一切的四个约束](../docs/N2-t9-gemm-isa-constraints_CN.md)
> - [`N3` — 显式 IMCI 微内核](../docs/N3-t9-gemm-explicit-vectorisation_CN.md) ·
>   [`N4` — 固定并行开销](../docs/N4-t9-gemm-fixed-overhead_CN.md) ·
>   [`N5` — B 面板打包](../docs/N5-t9-gemm-b-panel-packing_CN.md) ·
>   [`N6` — A 行距填充](../docs/N6-t9-gemm-row-stride-padding_CN.md) ·
>   [`N7` — j 分块与调参](../docs/N7-t9-gemm-jblocking-and-tuning_CN.md) ·
>   [`N8` — 剩余空间](../docs/N8-t9-gemm-headroom_CN.md)
>
> 英文版把文件名里的 `_CN` 去掉即可。交付清单与结论汇总见仓库根目录
> `T9_FINAL_REPORT.md`。

## 测试目标

测试 Intel Xeon Phi X100 (KNC) 在 LoongArch 移植版 MPSS 上的浮点算力，通过 GEMM（通用矩阵乘法）基准测试来评估：
- **FP32 单精度**浮点性能
- **FP64 双精度**浮点性能
- **三档负载**下的性能曲线：小/中/大规模
- **OpenMP 并行效率**（240 硬件线程）

## 负载设计

| 档位 | 矩阵规模 | FP32 理论 FLOPs | FP64 理论 FLOPs | 内存占用/矩阵 |
|------|----------|-----------------|-----------------|---------------|
| 极小 | 512×512  | 268.4 MFLOPS    | 268.4 MFLOPS    | 1 MB (FP32) / 2 MB (FP64) |
| 小   | 1024×1024| 2.15 GFLOPS     | 2.15 GFLOPS     | 4 MB (FP32) / 8 MB (FP64) |
| 中   | 2048×2048| 17.2 GFLOPS     | 17.2 GFLOPS     | 16 MB (FP32) / 32 MB (FP64) |
| 大   | 4096×4096| 137.4 GFLOPS    | 137.4 GFLOPS    | 64 MB (FP32) / 128 MB (FP64) |
| 极大 | 8192×8192| 1.1 TFLOPS      | 1.1 TFLOPS      | 256 MB (FP32) / 512 MB (FP64) |

**注**：理论 FLOPs = 2×N³（GEMM 算法复杂度）

## 测试流程

```
1. 编译宿主程序（gemm_host）
   └─ 调用 COI API，传参数到卡端
2. 编译卡端程序（gemm_sink.so）
   └─ 注册两个函数：gemm_fp32、gemm_fp64
3. 部署到卡端 /tmp/
4. 宿主循环调用：
   ├─ 五档规模 × 两种精度 = 10 次调用
   ├─ 每次：卡端 malloc → OpenMP 并行 GEMM → 计时
   └─ 输出 GFLOPS = (2×N³) / 时间
5. 汇总性能数据
```

## 使用方法

### 基本用法

```bash
cd tests
bash t9_gemm_bench.sh
```

### 前置条件

- 卡已 `online`（`micctrl --status`）
- COI + SCIF 库已安装
- k1om 编译器可用
- 卡端依赖库目录含 `libgomp.so`（自建的 k1om OpenMP 运行时）

### 输出示例

```
=== T9 GEMM 浮点性能基准 ===

负载档位: 极小 (512×512)
  精度: FP32 (1.00 MB/矩阵)
    宿主参考: 45.23 ms, 5.93 GFLOPS
    卡端执行: 5.67 ms, 47.34 GFLOPS
    状态: 完成 (结果验证略)

  精度: FP64 (2.00 MB/矩阵)
    宿主参考: 67.89 ms, 3.95 GFLOPS
    卡端执行: 11.24 ms, 23.87 GFLOPS
    状态: 完成 (结果验证略)

负载档位: 小 (1024×1024)
  精度: FP32 (4.00 MB/矩阵)
    宿主参考: 1234.56 ms, 3.48 GFLOPS
    卡端执行: 89.23 ms, 48.12 GFLOPS
    ...

负载档位: 极大 (8192×8192)
  精度: FP32 (256.00 MB/矩阵)
    宿主参考: 98765.43 ms, 11.12 GFLOPS
    卡端执行: 9876.54 ms, 111.23 GFLOPS
    状态: 完成 (结果验证略)

  精度: FP64 (512.00 MB/矩阵)
    宿主参考: 187654.32 ms, 5.85 GFLOPS
    卡端执行: 18765.43 ms, 58.51 GFLOPS
    状态: 完成 (结果验证略)

=== T9 性能汇总 ===
  512×512   FP32: 47.34 GFLOPS
  512×512   FP64: 23.87 GFLOPS
  1024×1024 FP32: 48.12 GFLOPS
  1024×1024 FP64: 28.67 GFLOPS
  2048×2048 FP32: 62.45 GFLOPS
  2048×2048 FP64: 35.89 GFLOPS
  4096×4096 FP32: 95.67 GFLOPS
  4096×4096 FP64: 52.34 GFLOPS
  8192×8192 FP32: 111.23 GFLOPS
  8192×8192 FP64: 58.51 GFLOPS
```

## 性能参考值

### Intel 官方 Xeon Phi 5110P 规格（理论峰值）

| 精度 | 理论峰值 | 备注 |
|------|----------|------|
| FP64 | **1.01 TFLOPS** | 60 核 × 8 DP 向量 × 2 FMA × 1.053 GHz |
| FP32 | **2.02 TFLOPS** | 60 核 × 16 SP 向量 × 2 FMA × 1.053 GHz |

### 7120P 规格（测试用卡）

| 精度 | 理论峰值 | 备注 |
|------|----------|------|
| FP64 | **1.21 TFLOPS** | 61 核 × 8 DP 向量 × 2 FMA × 1.238 GHz |
| FP32 | **2.42 TFLOPS** | 61 核 × 16 SP 向量 × 2 FMA × 1.238 GHz |

**预期效率**：朴素 GEMM（三重循环）通常能达到理论峰值的 **5%–15%**，因为：
1. 没有使用向量化内置函数（依赖编译器自动向量化）
2. 没有做分块优化（cache locality）
3. OpenMP 并行开销

**优化空间**：
- 使用 Intel 内置函数（`_mm512_*`）显式向量化 → 可达 30%–50%
- 分块优化（tiling）+ 数据预取 → 可达 60%–80%
- 调用 Intel MKL（如果移植了）→ 可达 85%–95%

## 当前实现的限制

### 1. 未实现结果验证

当前版本**没有将卡端计算的结果传回宿主**进行验证，原因：
- 2048×2048 FP64 矩阵 = 32 MB，超过单窗口 1 MiB 限制
- 需要实现切片传输（见 T8 的 `bigxfer` 方式）

**改进方向**：参考 T8 的实现，用 SCIF 窗口循环传输：
```cpp
// 在 sink 端添加 SCIF 传回逻辑
scif_epd_t ep = scif_accept(...);
off_t local_off, remote_off;
scif_register(ep, C, N*N*elem_size, local_off, SCIF_PROT_READ, 0);
// 宿主侧循环 scif_readfrom，每次 ≤ 1 MiB
```

### 2. 单一算子

当前只实现了 GEMM，可扩展为：
- **向量加法**（AXPY: `Y = a*X + Y`）→ 测带宽瓶颈
- **点积**（Dot Product）→ 测归约性能
- **矩阵转置**→ 测缓存行为

### 3. 固定线程数

当前硬编码 240 线程（= 60 核 × 4 线程），可改为：
```cpp
BenchParam param = {N, fp64, omp_get_max_threads()};
```

## 与现有测试的关系

| 测试 | 目的 | T9 的定位 |
|------|------|-----------|
| T5 (offload) | COI 端到端可用性 | T9 是 T5 的**性能版本** |
| T6 (stress) | 较高压力下的稳定性 | T9 关注**算力上限** |
| T7 (nbody) | 真实物理模拟 + 逐位正确性 | T9 是**合成基准**，纯算力 |
| T8 (bigxfer) | 大数据传输（4 GiB） | T9 算力为主、传输为辅 |

## 判定标准（建议）

| 项目 | PASS 标准 | 说明 |
|------|-----------|------|
| **编译** | 宿主与卡端程序都编译成功 | 基本可用性 |
| **执行** | 10 次调用全部返回成功 | 卡端没崩溃 |
| **性能合理性** | 大规模 FP32 ≥ 80 GFLOPS | 约为理论峰值的 3.3%（保守下限） |
| **精度关系** | 同规模下 FP32 > FP64（约 1.5–2×） | 符合硬件特性（SP 吞吐是 DP 的两倍） |
| **规模关系** | 性能随规模增大而提升（直到饱和） | 并行效率随规模提升，极大规模可能受内存带宽限制 |
| **峰值性能** | 极大规模 FP32 接近或超过 100 GFLOPS | 表明 OpenMP 并行和向量化有效工作 |

## 调试选项

### 查看 OpenMP 线程分配

在卡端代码开头加：
```cpp
#pragma omp parallel
{
    if (omp_get_thread_num() == 0) {
        printf("[CARD] Running with %d threads\n", omp_get_num_threads());
    }
}
```

查看 `/var/log/mpssd`（卡端日志）或 SSH 登录卡查看 stdout。

### 降低优化等级排查崩溃

如果卡端程序崩溃（参考附录 J 实测坑 2），尝试：
```bash
k1om-mpss-linux-g++ ... -O0  # 而不是 -O2
```

### 开启诊断打印

如果怀疑 DMA/SCIF 问题，重新编译模块：
```bash
cd release/08-mic-module
make clean
sudo make install MIC_DEBUG=1
dmesg | grep 'MIC scif'
```

## 接入 run_tests.sh

要将 T9 加入主测试流程，编辑 `run_tests.sh`：

```bash
# 在 T8 之后加入
if [ "$ONLY" = "" ] || [ "$ONLY" = "t9" ]; then
    bash t9_gemm_bench.sh
fi
```

## 许可证

本测试脚本遵循 MPSS 3.8.6 LoongArch 移植版的许可证（与上游 MPSS 相同）。

---

**作者备注**：这是一个最小可用的性能基准实现。生产环境建议使用优化后的 BLAS 库（如 Intel MKL 的 k1om 版本，若可获得），或实现分块 + 向量化的手工优化版本。
