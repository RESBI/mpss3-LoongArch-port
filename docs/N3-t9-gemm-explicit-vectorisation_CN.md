# 附录 N3 — T9 GEMM 步骤 1：显式 IMCI 微内核

**实测效果：FP32 6.39 → 57.4 GFLOPS，FP64 3.89 → 45.4 GFLOPS**
（N=2048，244 线程）。这一步把内核从"分了块"变成"真的向量化了"。

## 1. 这一步的起点

中间版从 T9 基线（朴素三重循环，`-O0`，0.33 GFLOPS）出发，施加了教科书式的
缓存修正：

- 64×64 分块；
- 循环由 `i-j-k` 重排为 `i-k-j`，使内层 `j` 沿 B 的行方向推进而非列方向
  —— 仅此一项就消除了基线上最差的访存行为；
- `__restrict__` 指针、64 字节对齐分配、OpenMP 并行 `(i,j)` 分块；
- 内层加 `#pragma omp simd`。

它做到 6.39 / 3.89 GFLOPS。其反汇编里 **`vfmadd` 为 0** —— 1199 条指令、
8 处 zmm 提及、没有任何向量算术。按
[N2 约束 1](N2-t9-gemm-isa-constraints_CN.md)，再加多少 pragma 都改变不了这一点。
所以那 6.39 GFLOPS 全部来自缓存局部性，向量单元一直闲着。

## 2. 微内核

形状遵循标准的寄存器分块 GEMM：把 C 的 `MR × NR` 块放在向量寄存器里，
让两个操作数从旁边流过，每个 k 步对每个累加器做一次 FMA。

- `NR32 = 16` 个 float / `NR64 = 8` 个 double —— 正好一个 zmm。
- `MR` 行 C 占用 `MR` 个 zmm 寄存器。
- 每个 k 步：**一次** 64 字节 B 载入、`MR` 次 A 标量广播、`MR` 次 FMA。

```c
/* N3：内层循环，FP32 */
for (uint32_t k = 0; k < N; k++) {
    __m512 b = _mm512_load_ps(pb + (size_t)k * NR32);
    UnrollPS<MR32>::fma(c, pa, lda, b, k);
}
```

两个实现细节比看起来重要得多：

**2.1 累加器必须是编译期下标。** `__m512 c[MR32]` 若用运行时下标就会被溢出到栈上，
整个设计就白费了。GCC 5 没有 `#pragma GCC unroll`（那是 GCC 8 才有的），
所以展开靠模板递归强制完成 —— `UnrollPS<R>::fma` 调用 `UnrollPS<R-1>::fma`
并访问 `c[R-1]`，使每个下标都是常量：

```c
template <int R> struct UnrollPS {
    static inline void fma(__m512 *c, const float *pa, uint32_t lda, __m512 b, uint32_t k) {
        UnrollPS<R - 1>::fma(c, pa, lda, b, k);
        c[R - 1] = _mm512_fmadd_ps(_mm512_set1_ps(pa[(size_t)(R - 1) * lda + k]), b, c[R - 1]);
    }
    /* zero() 与 store() 同型 */
};
```

**2.2 微内核只写 C，不累加进 C。** 它一趟算完全部 k 范围，所以 C 只存一次、
从不读回。这正是让重复一趟具备幂等性的原因，而步骤 2
（[N4](N4-t9-gemm-fixed-overhead_CN.md)）依赖这一点。

## 3. 编译器实际生成了什么

用 SDK objdump 从产物里抽出 FP32 微内核的 k 循环体，`MR32 = 8`
（含循环开销共 43 条指令）：

```
400d08:  vmovaps (%rcx),%zmm0              # B，每次 k 取满一整条 cache line
400d0e:  mov   -0x60(%rbp),%rax
400d12:  vbroadcastss (%rdx),%zmm17        # A[i0+0][k]
400d18:  add   -0x40(%rbp),%rcx
400d1c:  vfmadd231ps %zmm0,%zmm17,%zmm16
400d22:  vbroadcastss (%rdx,%rsi,4),%zmm17
400d29:  vfmadd231ps %zmm0,%zmm17,%zmm15
         ... 另有 6 组 broadcast/FMA ...
400deb:  add   $0x4,%rdx
400def:  cmp   %rdx,-0x38(%rbp)
400df3:  vfmadd231ps %zmm0,%zmm17,%zmm1
400df9:  jne   400d08
```

每 8 次 FMA 配 16 条向量指令 —— 即 `MR32` 次广播加 `MR32` 次 FMA，
KNC 上的教科书内层循环。那五条 `mov -0xNN(%rbp),%rax` 是寄存器分配器搁在栈上的
A 行基址指针（`MR` 个基址寄存器加循环状态超出了预算）；它们是 L1 命中、代价很小，
但它们暗示这个块已经到边了 —— 见 [N7](N7-t9-gemm-jblocking-and-tuning_CN.md)，
那里**缩小**块反而获胜。

整个目标文件的指令构成：`vfmadd` 24（按出厂 MR 值拆成 12 FP32 + 12 FP64）、
`vmovaps` 54、`vmovapd` 30、`vbroadcastss` 16、`vbroadcastsd` 8、`zmm` 140、
**`xmm`/`ymm` 0**。

## 4. 为什么只到 57 GFLOPS，而预期约 1900

上面这个循环按发射受限估算，约 21 个周期做 16 次 FMA，61 核即约 1900 GFLOPS。
实测 57.4。差距不在指令 —— 从测量值反推得到 **每次 k 迭代约 1462 个周期，
而理想约 21**。

三个叠加的原因，各自在后续步骤解决：

1. **约 0.15 s 的固定并行开销**在这个量级上压过一切
   （[N4](N4-t9-gemm-fixed-overhead_CN.md)）—— N=2048 时整轮只有 0.30 s。
2. **B 在 16 MB 地址空间上跳步**：每次 k 取一条 64 字节 line，间隔 8 KB，
   每两次迭代就换页（[N5](N5-t9-gemm-b-panel-packing_CN.md)）。
3. **A 的 16 行全部落进同一个 L1 组**，因为行距 8192 是 2 的幂
   （[N6](N6-t9-gemm-row-stride-padding_CN.md)）—— 三者中最大的一项。

这一步值得留下的教训是**诊断的形状**：一个看起来正确的内层循环配上糟糕的实测
速率，说明问题在循环之外 —— 在存储系统或测量装置里。数 `vfmadd` 只能证明循环
存在；只有周期预算才能说明它是否在跑。

## 5. 规则

> 内层循环写成 `MR` 个累加器的寄存器块，每行一条 `vbroadcastss` 加一条
> `vfmadd231ps`；用模板递归在编译期强制展开（GCC 5）；检查 `xmm/ymm == 0`。
> 然后量每次 k 迭代的周期数 —— 如果不在发射上界的几倍之内，
> 就别再调循环了，去看地址。
