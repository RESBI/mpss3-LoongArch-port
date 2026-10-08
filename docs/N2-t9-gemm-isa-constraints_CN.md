# 附录 N2 — T9 GEMM 前置：挡住一切的四个约束

本系列后面五个优化步骤能跑起来，前提是先知道 k1om 工具链与 Knights Corner
ISA 的四个事实。这轮工作里**每一次失败**都是违反了其中一条，而症状极具误导性，
以至于本套件一度记录下了错误的结论。这一篇应当最先读。

依据：*Phi ISA Reference Manual*（725 页，已抽为文本并逐条检索指令），
以及在本移植工具链上的实测 —— k1om GCC 5.1.1，用 `tests/lib/common.sh`
里的 `k1om_cxx` 包装器。

## 约束 1 —— k1om 后端在任何优化档都不自动向量化

没有任何 `-O` 档能从 C 生成 512-bit 代码。实测方法：用八组标志编译一个朴素的
`i-k-j` GEMM 循环，再用 SDK 自带的 `k1om-mpss-linux-objdump` 数 `vfmadd`：

| 标志 | `vfmadd` |
|---|---|
| `-O2`（默认 arch） | 0 |
| `-O3` | 0 |
| `-O3 -march=knc` | 0 |
| `-O3 -march=knc -mno-avx512vl` | 0 |
| `-O2 -fopenmp` / `-O3 -fopenmp` / `-O3 -march=knc -fopenmp` | 0 |
| `-O3 -mavx512f` | *汇编报错* |
| `-O3 -march=knc -mavx512f` | *汇编报错* |

`-march=knc` 只是把 `-march` 设成 `knc`，特性位仍然关着 ——
`k1om_cxx -march=knc -Q --help=target` 报告 `-mavx512f [disabled]`，
而 `-march=knc` 只定义 `__KNC__`、`__k1om`、`__k1om__`。向量化器没有目标可用，
于是直接放弃。

**后果。** 拿到 512-bit 代码的唯一途径是手写 `_mm512_*` intrinsics。这也解释了
[N3](N3-t9-gemm-explicit-vectorisation_CN.md) 里那个 6.39 GFLOPS 的中间版为什么
看着像样却不是：它有分块、`restrict` 指针、对齐分配和 `#pragma omp simd`，
但依然一次只做一个标量运算。

## 约束 2 —— intrinsics 需要 `-mavx512f`，而它会把标量浮点毒化

intrinsics 定义在 `avx512fintrin.h`，由 `#pragma GCC target("avx512f")` 保护并
以 `__AVX512F__` 为开关。不加 `-mavx512f`，每个调用都失败于
`inlining failed in call to always_inline ... target specific option mismatch`。
加上它，**整个编译单元**被切到通用 AVX-512F —— 而那个目标是有 xmm/ymm 的，
KNC 没有。

于是只要文件里还剩**任何标量**浮点运算，GCC 就会自然而然地生成 xmm 形式，
k1om 汇编器随即拒绝：

```
Error: `vcvtsi2ss' is not supported on `k1om'
Error: `vaddss'    is not supported on `k1om'
Error: `vmulsd'    is not supported on `k1om'
Error: bad register name `%xmm3'
```

这不是边角情况。`sin()`/`cos()`、用作校验和的 `double` 累加器、
`double t = walltime()`、以及面向宿主的 GFLOPS 计算，都会触发它。第一版把
`_mm512_fmadd_ps` 和 `float` 校验和混在一起，光转换指令就编不过。

**后果 —— 本内核的铁律：卡端文件里不得出现任何标量浮点。** 在
`gemm_vec_sink.cpp` 里具体是：

| 平常会写的 | 替换成 |
|---|---|
| `A[i] = 1.0f + sinf(i * 0.001f)` | 从 `.rodata` 表取 64 字节对齐切片 |
| `double dt = (t1 - t0) * 1e-6` | 回传 `int64_t` 微秒，由宿主换算 |
| `float cs = 0; for (...) cs += C[i];` | `acc = _mm512_add_ps(acc, ...)`，store 16 车道，宿主求和 |
| `(float)(i % 64) * 0.015625f` | 不需要 —— 见上一行 |

## 约束 3 —— 普通 C 会直接踩中的 ISA 缺口

对照手册指令索引逐条核对：

| KNC 没有 | 最接近的存在 | 会踩在哪里 |
|---|---|---|
| `VCVTDQ2PS`（int32→float32） | `VCVTFXPNTDQ2PS` —— *定点*转换，属于另一族 intrinsic | 任何 `(float)i` |
| `VPADDQ`、`VPSLLQ`、`VPSUBQ` | 无 —— KNC 的整数单元是 32-bit 通道 | 64 位索引/位运算；早先想用它构造 `double` 位模式，卡在 `vpaddq` |
| `VMOVUPS`、`VMOVUPD` —— **完全没有非对齐向量访存** | `VLOADUNPACK{L,H}P{S,D}` / `VPACKSTORE*` | 任何 GCC 无法证明 64 字节对齐的向量载入/存储 |
| `CMOVA` 及其余 `cmov` | 无 —— 改用分支 | **任何 `? :` 运算符**（约束 4） |

非对齐访问这条产生的报错最不直观，因为 GCC 会从**看起来完全正常**的代码里
生成 `vmovups`：比如存进一个被 GCC 放在 `-120(%rbp)` 的栈上临时变量，
或者表载入的下标它只能界定到 `0..63`、证不出是 16 的倍数。对应的两种修法分别是
"把向量字段放在 `__attribute__((aligned(64)))` 结构体的首位，让存储目标可证对齐"
和"用 `__builtin_assume_aligned` 对表切片断言对齐"。两者都能在
`gemm_vec_sink.cpp` 里看到（`VResult::lanes` 在首位；`TABP` 宏）。

## 约束 4 —— `? :` 不可用

最小的一条，也是代价最大的一条：

```c
/* 报错：Error: `cmova' is not supported on `k1om' */
const uint32_t js1 = (js0 + JSB < njs) ? (js0 + JSB) : njs;
```

GCC 把三元运算符编译成条件搬移，KNC 没有，汇编器直接停。既无警告也无回退。
本内核里每一个 clamp、每一个 `min`、每一处"取较小者"都改写成了 `if`/`else`，
并且循环边界被重构到根本不需要 clamp —— 内层上界写成 `js0 + JSB`，
靠 N 的整除性保证不越界（见 [N7](N7-t9-gemm-jblocking-and-tuning_CN.md)）。

这也解释了一切开始的那次历史性失败：第一次尝试用 `-O2 -mavx512f` 编译 sink 时
死于 `` Error: `cmovbe' is not supported on `k1om' `` 加三条 `cmova`，
而出错代码正是用三元运算符夹取分块边界的部分。

## 这对套件早先那条结论意味着什么

验收套件 README 此前写着*"卡端 sink 只能用 `-O0` 编译"*，依据是 `t7` 的 N 体程序
在 `-O1`/`-O2` 下崩溃（卡端 `segfault`，出错 ip 落在 `vpackstorelpd`）。
观察是对的，推广是错的。

真实规则更窄、也可检查：

> 这套工具链上的 `-O2` 会生成标量浮点指令与 `cmov`，KNC 两者都没有。
> 汇编器拒掉一部分；它接受的那部分（掩码压缩存储）在运行时崩。
> 只要卡端源码把所有浮点运算关进 512-bit intrinsic、且不含 `? :`，
> 用 `-O2 -mavx512f` 就能正确构建运行 —— 而且比 `-O0` 基线快 38 倍。

`t7` 仍然用 `-O0`，因为它的源码**本身是标量的**。这现在是源码的性质，
不再是工具链的性质。

有一点**没有**改变：反汇编不是可用的判据。数 `vpackstore`/`vscatter`
在能跑的 `-O0` N 体产物里是 12 条，在崩溃的 `-O1`/`-O2` 产物里各是 11 条。
对**向量化内核**真正可用的是 [N1 §3](N1-t9-gemm-optimization-overview_CN.md)
那两道门禁 —— `vfmadd > 0` 且 `xmm/ymm == 0` —— 因为该内核的失效模式是
"悄悄退回标量"，而这两个计数正好直接检出它。

## 新写向量化卡端程序的检查表

1. 用 `-O2 -mavx512f -fopenmp -rdynamic` 构建。
2. 不出现 `float`/`double` 标量变量、字面量运算或 `math.h` 调用。
3. 任何地方都不出现 `? :`。
4. 每个向量载入/存储都能证明 64 字节对齐
   （`_mm_malloc(...,64)` + `__builtin_assume_aligned`，表切片走 `TABP`）。
5. 数据取自 `.rodata` 表；计时用整数微秒；归约用向量累加器后整块 store。
6. 以 `vfmadd > 0` **且** `xmm/ymm == 0` 作为构建门禁。
