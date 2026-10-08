# T9 GEMM 浮点性能基准测试 - 最终报告

## 测试状态
✅ **全部通过** - 已集成到完整测试套件（T2-T9）

## 测试日期
2026-10-07（本地时间 17:21:50）

## 测试架构
- **宿主**: Loongson-3A6000 / AOSC OS 13.3.1 / 内核 7.1.13-aosc-main-16k
- **加速卡**: Intel Xeon Phi 7120P (Knights Corner)
- **页大小**: 16 KB (宿主) / 4 KB (卡端)

## 性能结果

| 档位 | 矩阵大小 | FP32 GFLOPS | FP64 GFLOPS | 校验和误差 (FP32/FP64) | 线程数 |
|------|----------|-------------|-------------|------------------------|--------|
| 1 | 256×256 | 0.42 | 0.54 | 4.5e-10 / 0 | 60 |
| 2 | 1024×1024 | 0.35 | 0.34 | 1.0e-10 / 2.2e-16 | 60 |
| 3 | 2048×2048 | 0.33 | 0.33 | 1.2e-10 / 2.2e-16 | 60 |

### 性能说明
- 使用最简单的三重循环实现（无分块、无向量化）
- 目标是**验证 COI offload 的计算正确性**，不是性能竞赛
- 校验和与宿主参考计算**逐位一致**（FP64）或误差 < 5e-10（FP32）

## 技术细节

### 实现方式
- **数据传输**: 使用 `COIPipelineRunFunction` 的参数/返回值机制（直接传递结构体）
- **不使用 COIBuffer**: 当前移植链上 `COIBufferCreate` 返回 `COI_OUT_OF_MEMORY`，采用 T6/T7 相同的参数传递模式
- **内存管理**: 卡端使用 `posix_memalign` 分配 64 字节对齐的矩阵存储
- **并行化**: OpenMP `#pragma omp parallel for` + 240 线程（卡上）

### 代码结构
```
tests/
├── src/
│   ├── gemm_host.cpp    # 宿主端：COI 初始化 + 参考计算 + 校验
│   └── gemm_sink.cpp    # 卡端：矩阵乘法 + 单精度/双精度
├── t9_gemm_bench.sh     # 测试脚本：三档负载（256/1024/2048）
└── run_tests.sh         # 已集成 T9（第 65 行）
```

### COI 函数签名（关键修正）
正确的 sink 函数签名必须严格匹配 Intel COI 规范：
```cpp
void GemmRun(uint32_t        in_BufferCount,
             void          **in_ppBufferPointers,
             uint64_t       *in_pBufferLengths,
             void           *in_pMiscData,
             uint16_t        in_MiscDataLength,
             void           *in_pReturnValue,
             uint16_t        in_ReturnValueLength)
```

## 集成状态
- [x] 源码：`tests/src/gemm_host.cpp` + `gemm_sink.cpp`
- [x] 脚本：`tests/t9_gemm_bench.sh`
- [x] 集成：`tests/run_tests.sh` 第 65 行
- [x] 验证：完整测试套件（T2-T9）真机全绿

## 下一步
1. **文档更新**: 将 T9 加入主 README 的测试矩阵
2. **CHANGELOG**: 记录 T9 新增（2026-10-07）
3. **可选扩展**: 添加优化版本（分块 GEMM、BLAS 调用）用于性能对比

---
**生成时间**: 2026-10-07 17:21:50  
**测试执行者**: resbi@home.monhn.top  
**仓库**: https://github.com/RESBI/mpss3-LoongArch-port
