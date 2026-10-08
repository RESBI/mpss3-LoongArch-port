# T9 GEMM 性能基准测试 — 快速开始指南

## 一分钟上手

```bash
# 1. 确认卡已上线
micctrl --status
# 应显示：mic0: online

# 2. 运行 T9 测试
cd /path/to/release/tests
bash t9_gemm_bench.sh

# 3. 查看结果
# 测试会自动输出性能汇总，格式如下：
#   512×512   FP32: XX.XX GFLOPS
#   512×512   FP64: XX.XX GFLOPS
#   ... (共 10 行)
```

## 预期运行时间

| 档位 | FP32 | FP64 | 说明 |
|------|------|------|------|
| 512×512 | ~0.01 秒 | ~0.02 秒 | 启动开销占比大 |
| 1024×1024 | ~0.1 秒 | ~0.15 秒 | 进入稳定状态 |
| 2048×2048 | ~0.5 秒 | ~1.0 秒 | 主要计算时间 |
| 4096×4096 | ~4 秒 | ~8 秒 | 大规模计算 |
| 8192×8192 | ~30 秒 | ~60 秒 | 极大规模（接近卡端内存上限） |

**总运行时间**：约 **2–3 分钟**（含编译、部署、10 次测试）

## 内存需求

### 卡端内存（GDDR5）

7120P 总内存：16 GB

| 档位 | FP32 需求 | FP64 需求 | 是否安全 |
|------|-----------|-----------|----------|
| 512×512 | 3 MB (A+B+C) | 6 MB | ✅ 安全 |
| 1024×1024 | 12 MB | 24 MB | ✅ 安全 |
| 2048×2048 | 48 MB | 96 MB | ✅ 安全 |
| 4096×4096 | 192 MB | 384 MB | ✅ 安全 |
| 8192×8192 | 768 MB | 1.5 GB | ✅ 安全（但接近系统保留边界） |

**注**：实际需求为 3×N²×elem_size（三个矩阵 A、B、C）

### 宿主内存

宿主侧需要同样的内存用于参考计算，但不会同时分配所有档位，每次测试后释放。

**最大单次需求**：8192×8192 FP64 = 1.5 GB（可接受）

## 常见问题

### Q1: 编译失败 "找不到 intel-coi/source/COIEngine_source.h"

**原因**：COI 库未安装或路径不对

**解决**：
```bash
# 检查 COI 安装
ls /usr/include/intel-coi/
# 应该看到 source/ 和 sink/ 目录

# 若缺失，从发布树安装：
cd release/06-mpss-coi
sudo make install
```

### Q2: 卡端编译失败 "找不到 k1om-mpss-linux-g++"

**原因**：k1om 工具链不在 PATH，或未安装

**解决**：
```bash
# 方法 1：设置 K1OM_SDK 环境变量
export K1OM_SDK=/path/to/k1om-sdk
bash t9_gemm_bench.sh

# 方法 2：修改 tests/config.sh
echo 'K1OM_SDK=/opt/mpss/sdk' >> tests/config.sh
```

### Q3: 运行时报 "COIProcessCreateFromFile failed: 11"

**错误码 11** = `COI_DOES_NOT_EXIST`

**原因**：卡端 `libgomp.so` 缺失或路径不对

**解决**：
```bash
# 检查卡端依赖库
ssh mic0 ls -lh /lib64/libgomp.so*

# 若缺失，部署自建的 k1om libgomp：
scp /path/to/k1om/libgomp.so.1 mic0:/lib64/
ssh mic0 ldconfig
```

### Q4: 极大规模（8192×8192）运行时卡端 OOM

**症状**：`dmesg` 显示 `Out of memory: Kill process ...`

**原因**：卡端可用内存不足（系统保留 + 其他进程占用）

**解决**：
1. 确认卡端没有其他大内存进程：
   ```bash
   ssh mic0 ps aux --sort=-rss | head -n 10
   ```
2. 如果是 `coi_daemon` 或 `mpssd` 的子进程占用过多，重启 MPSS：
   ```bash
   sudo systemctl restart mpss
   micctrl --wait
   ```
3. 如果仍然不够，可以在脚本中临时注释掉 8192 档：
   ```bash
   # 编辑 t9_gemm_bench.sh
   int sizes[] = {512, 1024, 2048, 4096};  # 去掉 8192
   ```

### Q5: 性能异常低（< 10 GFLOPS）

**可能原因**：

1. **编译器优化未开启**：
   ```bash
   # 检查 sink 编译命令（应该有 -O2）
   grep "k1om.*-O2" tests/t9_gemm_bench.sh
   ```

2. **OpenMP 线程数不对**：
   ```bash
   # 卡端查看
   ssh mic0 "cat /proc/cpuinfo | grep processor | wc -l"
   # 应该是 244（61 核 × 4 线程）
   ```

3. **卡端 CPU 频率被限制**：
   ```bash
   ssh mic0 cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq
   # 7120P 应该是 1238000 kHz
   ```

### Q6: 想只测某一档或某种精度

**修改方法**：

临时改范围：
```bash
# 只测 FP32
for (int fp64 = 0; fp64 <= 0; fp64++)  # 改成 <= 0

# 只测大规模
int sizes[] = {4096, 8192};
for (int si = 0; si < 2; si++)
```

或者干脆手工编译运行：
```bash
cd tests/logs/<最新时间戳>/build
./gemm_host /tmp/gemm_sink.so
```

## 性能解读

### 正常范围

| 指标 | 合格 | 良好 | 优秀 |
|------|------|------|------|
| FP32 @ 8192 | ≥ 80 GFLOPS | ≥ 120 GFLOPS | ≥ 150 GFLOPS |
| FP64 @ 8192 | ≥ 40 GFLOPS | ≥ 60 GFLOPS | ≥ 75 GFLOPS |
| FP32/FP64 比值 | 1.5–2.2 | 1.8–2.1 | 1.9–2.0 |

**效率对照**：
- 80 GFLOPS (FP32) = 3.3% 峰值
- 120 GFLOPS (FP32) = 5.0% 峰值
- 150 GFLOPS (FP32) = 6.2% 峰值

对于**朴素三重循环**，5–6% 是合理上限。

### 异常模式

| 症状 | 可能原因 | 排查 |
|------|----------|------|
| 所有档位 < 10 GFLOPS | OpenMP 未工作 | `ssh mic0 ldd /tmp/gemm_sink.so` 检查 `libgomp.so` |
| 极小规模反而最快 | 启动开销未排除 | 忽略 512×512，看 2048+ |
| FP32 ≈ FP64 性能 | 编译器未向量化 | 检查 `-O2` 与 `-fopenmp` 是否生效 |
| 性能随规模下降 | 内存带宽瓶颈 | 正常（8192 时 cache miss 率高） |
| 某档突然崩溃 | 内存不足 | `dmesg` 查 OOM |

## 与 T7 nbody 对比

| 项目 | T7 | T9 |
|------|----|----|
| 算法 | N 体引力（O(N²)） | GEMM（O(N³)） |
| 计算密度 | 低（每次访存做少量运算） | 高（三重循环） |
| 预期效率 | 0.2–0.5%（T7 实测 ~0.1%） | 3–6% |
| 验证方式 | 逐位 checksum | 当前版本未验证 |
| 典型性能 | 2.68 GFLOPS (FP64) | 预期 50–70 GFLOPS (FP64) |

**为什么 T9 效率更高？** 因为 GEMM 的计算访存比（O(N³) / O(N²) = O(N)）远高于 N 体（O(N²) / O(N²) = O(1)），更适合向量单元。

## 后续优化建议

如果要进一步榨取性能：

1. **手工向量化**（预期提升 5–10×）：
   ```cpp
   #include <immintrin.h>
   __m512 va = _mm512_load_ps(&A[...]);
   __m512 vb = _mm512_load_ps(&B[...]);
   __m512 vc = _mm512_fmadd_ps(va, vb, vc);  // FMA
   ```

2. **分块优化**（预期额外提升 1.5–2×）：
   ```cpp
   const int BLOCK = 64;  // 优化 L1 cache
   for (int ii = 0; ii < N; ii += BLOCK)
       for (int jj = 0; jj < N; jj += BLOCK)
           for (int kk = 0; kk < N; kk += BLOCK)
               // 块内三重循环
   ```

3. **使用 Intel MKL**（若可获得 k1om 版本）：
   ```bash
   # 替换整个 GEMM 为一行
   cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
               N, N, N, 1.0, A, N, B, N, 0.0, C, N);
   ```

## 集成到 CI/CD

```bash
#!/bin/bash
# 自动化性能回归测试
bash tests/t9_gemm_bench.sh > /tmp/t9.log 2>&1

# 提取关键指标
FP32_LARGE=$(grep "4096×4096 FP32" /tmp/t9.log | awk '{print $3}')
FP64_LARGE=$(grep "4096×4096 FP64" /tmp/t9.log | awk '{print $3}')

# 判定
if (( $(echo "$FP32_LARGE < 80" | bc -l) )); then
    echo "FAIL: FP32 性能低于阈值"
    exit 1
fi

if (( $(echo "$FP64_LARGE < 40" | bc -l) )); then
    echo "FAIL: FP64 性能低于阈值"
    exit 1
fi

echo "PASS: T9 性能正常"
```

---

**最后提醒**：这是一个**合成基准测试**，目的是验证移植版的基本浮点能力。真实应用的性能会受到很多其他因素影响（数据传输、任务调度、内存分配等）。把 T9 当作**最低性能基线**，而不是应用性能的上限。
