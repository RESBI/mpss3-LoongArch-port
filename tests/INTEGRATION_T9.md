# T9 GEMM 性能基准测试 — 项目集成指南

## 文件清单

T9 测试由以下四个文件组成：

| 文件 | 大小 | 用途 |
|------|------|------|
| `t9_gemm_bench.sh` | 13.4 KB | 主测试脚本（380 行，含内嵌 C++ 代码） |
| `README_T9.md` | 7.3 KB | 完整技术文档（设计、使用、性能参考） |
| `CHANGELOG_T9.md` | 5.3 KB | 版本历史与优化建议 |
| `QUICKSTART_T9.md` | 7.3 KB | 快速上手指南（常见问题、性能解读） |

**总计**：33.3 KB，四个文件。

## 与现有测试体系的集成

### 1. 依赖关系

T9 复用现有基础设施：

```
t9_gemm_bench.sh
├─ lib/common.sh          # 路径探测、部署、记账（T0–T8 共用）
├─ config.sh (可选)        # 用户自定义 K1OM_SDK 路径
├─ COI 库                  # 06-mpss-coi
├─ SCIF 库                 # 02-libscif
└─ k1om 工具链             # mpss-main 包含的 gcc-k1om
```

**不需要新增外部依赖**，只要 T0–T8 能跑，T9 就能跑。

### 2. 加入主测试流程

编辑 `tests/run_tests.sh`，在 T8 之后添加：

```bash
# ============================================================================
# T9: GEMM 浮点性能基准测试
# ============================================================================
if [ "$ONLY" = "" ] || [ "$ONLY" = "t9" ]; then
    banner "T9: GEMM 浮点性能基准"
    if bash t9_gemm_bench.sh; then
        PASSED=$((PASSED + 1))
        echo "[PASS] T9: GEMM 浮点性能基准"
    else
        FAILED=$((FAILED + 1))
        echo "[FAIL] T9: GEMM 浮点性能基准"
    fi
    echo
fi
```

### 3. 单独运行

```bash
# 只运行 T9
cd tests
bash t9_gemm_bench.sh

# 或通过主脚本
bash run_tests.sh t9
```

### 4. 日志位置

与其他测试一致：

```
tests/logs/<时间戳>/
├─ T9 GEMM benchmark.log     # 完整运行日志
├─ build/
│   ├─ gemm_host.cpp         # 宿主源码
│   ├─ gemm_host             # 宿主可执行文件
│   ├─ gemm_sink.cpp         # 卡端源码
│   └─ gemm_sink.so          # 卡端共享库
└─ deploy/
    └─ gemm_sink.so          # 部署到卡端的副本
```

## 测试输出格式

### 标准输出

```
=== Stage: T9: GEMM 等算子浮点性能基准测试 ===

检测依赖:
  [✓] COI 库路径: /usr/include/intel-coi
  [✓] k1om 编译器: /opt/mpss/bin/k1om-mpss-linux-g++
  [✓] 卡状态: online

编译宿主程序...
  [✓] gemm_host

编译卡端程序...
  [✓] gemm_sink.so (k1om)

部署到卡端 /tmp/...
  [✓] gemm_sink.so

运行测试...

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

[... 其他档位 ...]

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

=== Stage finished: T9: GEMM 等算子浮点性能基准测试 ===
```

### 退出码

- **0**：全部成功（10/10 测试完成）
- **1**：编译失败或依赖缺失
- **非零**：部分测试失败或崩溃

## 判定标准

### PASS 条件（全部满足）

1. ✅ 宿主程序编译成功
2. ✅ 卡端程序编译成功（k1om）
3. ✅ 部署到卡端成功
4. ✅ 10 次测试全部返回成功（5 档 × 2 精度）
5. ✅ 大规模 FP32 ≥ 80 GFLOPS（保守下限）
6. ✅ FP32/FP64 比值在 1.5–2.2 范围内

### SKIP 条件（任一满足）

- 卡不在 `online` 状态
- k1om 编译器不可用
- COI 库未安装

### FAIL 条件（任一满足）

- 编译失败
- 任一测试崩溃或返回错误
- 性能异常低（< 10 GFLOPS）
- 精度比值异常（FP32 < FP64）

## 性能基线

### 合格线（必须达到）

| 规模 | FP32 | FP64 | 说明 |
|------|------|------|------|
| 2048×2048 | ≥ 50 GFLOPS | ≥ 25 GFLOPS | 基本可用 |
| 4096×4096 | ≥ 80 GFLOPS | ≥ 40 GFLOPS | 主力规模 |
| 8192×8192 | ≥ 80 GFLOPS | ≥ 40 GFLOPS | 极限规模（可能受带宽限制） |

### 良好线（预期达到）

| 规模 | FP32 | FP64 |
|------|------|------|
| 4096×4096 | 100–120 GFLOPS | 50–60 GFLOPS |
| 8192×8192 | 110–140 GFLOPS | 55–70 GFLOPS |

**理论峰值参考**：
- 7120P FP32: 2.42 TFLOPS (100%)
- 7120P FP64: 1.21 TFLOPS (100%)
- 朴素 GEMM 预期: 3–6% 峰值

## 回归测试集成

### CI/CD 示例

```bash
#!/bin/bash
# 性能回归测试脚本

set -e

# 运行 T9
cd /path/to/release/tests
bash t9_gemm_bench.sh > /tmp/t9_result.log 2>&1

# 提取关键指标
LOG=/tmp/t9_result.log
FP32_4K=$(grep "4096×4096 FP32" "$LOG" | awk '{print $3}')
FP64_4K=$(grep "4096×4096 FP64" "$LOG" | awk '{print $3}')
FP32_8K=$(grep "8192×8192 FP32" "$LOG" | awk '{print $3}')
FP64_8K=$(grep "8192×8192 FP64" "$LOG" | awk '{print $3}')

# 判定
fail=0

if (( $(echo "$FP32_4K < 80" | bc -l) )); then
    echo "❌ FP32 @ 4096×4096 = $FP32_4K GFLOPS (< 80)"
    fail=1
else
    echo "✅ FP32 @ 4096×4096 = $FP32_4K GFLOPS"
fi

if (( $(echo "$FP64_4K < 40" | bc -l) )); then
    echo "❌ FP64 @ 4096×4096 = $FP64_4K GFLOPS (< 40)"
    fail=1
else
    echo "✅ FP64 @ 4096×4096 = $FP64_4K GFLOPS"
fi

# 检查精度比值
ratio=$(echo "$FP32_4K / $FP64_4K" | bc -l)
if (( $(echo "$ratio < 1.5 || $ratio > 2.2" | bc -l) )); then
    echo "❌ FP32/FP64 比值 = $ratio (异常，应在 1.5–2.2)"
    fail=1
else
    echo "✅ FP32/FP64 比值 = $ratio"
fi

if [ $fail -eq 0 ]; then
    echo ""
    echo "🎉 T9 性能回归测试通过"
    exit 0
else
    echo ""
    echo "💔 T9 性能回归测试失败"
    exit 1
fi
```

### 监控指标

建议跟踪以下指标的历史趋势：

1. **FP32 @ 4096×4096**：主力规模性能
2. **FP64 @ 4096×4096**：双精度性能
3. **FP32/FP64 比值**：硬件特性验证
4. **8192×8192 vs 4096×4096 倍率**：扩展性
5. **运行时间**：是否有性能退化

### Grafana Dashboard 示例

```json
{
  "panels": [
    {
      "title": "T9 FP32 Performance",
      "targets": [
        {
          "metric": "t9_fp32_4k_gflops",
          "refId": "A"
        }
      ],
      "yaxis": {
        "label": "GFLOPS",
        "min": 0,
        "max": 150
      }
    },
    {
      "title": "T9 FP32/FP64 Ratio",
      "targets": [
        {
          "expr": "t9_fp32_4k_gflops / t9_fp64_4k_gflops"
        }
      ],
      "yaxis": {
        "min": 1.0,
        "max": 2.5
      }
    }
  ]
}
```

## 已知限制与改进路线图

### 当前版本 (v1.0)

✅ **已实现**：
- 五档负载规模（512–8192）
- FP32/FP64 分别测试
- OpenMP 240 线程并行
- 自动性能汇总
- 与宿主参考性能对比

❌ **未实现**：
- 结果正确性验证（需要切片传输，见 T8）
- 手工向量化优化
- 分块优化
- 其他算子（AXPY、点积、转置）

### 改进路线图

#### v1.1 — 结果验证（预计 +200 行代码）

```cpp
// 在 sink 端添加 SCIF 传回
scif_epd_t ep = scif_accept(...);
off_t local_off, remote_off;
scif_register(ep, C, N*N*elem_size, local_off, ...);
// 宿主侧循环读取，每次 ≤ 1 MiB
for (off_t off = 0; off < total; off += CHUNK) {
    scif_readfrom(ep, buf, min(CHUNK, total-off), remote_off+off, 0);
}
// 逐位比对
```

#### v1.2 — 手工优化版本（预计性能提升 5–10×）

```cpp
// 显式向量化 + 分块
#include <immintrin.h>
const int BLOCK = 64;
for (int ii = 0; ii < N; ii += BLOCK) {
    for (int jj = 0; jj < N; jj += BLOCK) {
        for (int kk = 0; kk < N; kk += BLOCK) {
            // 块内用 _mm512_* 内置函数
            __m512 va = _mm512_load_ps(...);
            __m512 vb = _mm512_load_ps(...);
            __m512 vc = _mm512_fmadd_ps(va, vb, vc);
        }
    }
}
```

#### v2.0 — 多算子套件

新增：
- `t9_axpy.sh`：向量加法，测带宽
- `t9_dot.sh`：点积，测归约
- `t9_transpose.sh`：矩阵转置，测缓存
- `t9_stencil.sh`：模板计算，测邻接访问

## 与 Intel 官方工具对比

| 工具 | 用途 | T9 的定位 |
|------|------|-----------|
| Intel MKL Benchmark | 官方 BLAS 基准 | T9 是**开源验证版本**，朴素实现 |
| LINPACK | HPC Top500 标准 | T9 更轻量，侧重**移植验证** |
| STREAM | 内存带宽测试 | T9 侧重计算，未来可加 AXPY |
| Intel Advisor | 性能分析工具 | T9 是**端到端可用性测试** |

**T9 的价值**：
1. ✅ 完全开源，代码可审计
2. ✅ 与项目现有测试体系一致
3. ✅ 轻量级，无需外部依赖
4. ✅ 适合 CI/CD 集成

**T9 不替代**：
- ❌ 生产环境性能优化（用 MKL）
- ❌ 详细性能分析（用 VTune/Advisor）
- ❌ 标准化 HPC 评测（用 LINPACK）

## 文档索引

| 文档 | 内容 | 适合人群 |
|------|------|----------|
| `README_T9.md` | 完整技术文档 | 开发者、维护者 |
| `QUICKSTART_T9.md` | 快速上手指南 | 测试人员、用户 |
| `CHANGELOG_T9.md` | 版本历史 | 项目管理者 |
| 本文档 | 项目集成指南 | CI/CD 工程师 |

## 联系与反馈

如果遇到问题或有改进建议：

1. **性能异常**：查看 `QUICKSTART_T9.md` 的「常见问题」章节
2. **功能建议**：参考 `CHANGELOG_T9.md` 的「改进路线图」
3. **集成问题**：参考本文档的「判定标准」与「回归测试」章节

---

**最后更新**：2026-10-07  
**版本**：T9 v1.0（初始版本）  
**维护者**：MPSS 3.8.6 LoongArch 移植项目组
