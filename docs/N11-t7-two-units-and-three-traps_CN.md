# 附录 N11 — T7：两个编译单元，以及三个只有硬件才能暴露的坑

[N10](N10-t7-gemm-fp64-no-transcendentals_CN.md) 的向量内核算术上是对的。
让它**跑起来**则需要两个结构性决定，和三次靠读源码永远得不出的诊断。

## 1. 为什么 sink 现在由两个编译单元组成

卡端程序同时需要两件互相冲突的东西：

| T7 的哪一部分 | 需要 | 为什么 |
|---|---|---|
| 初始条件（`sin`、`cos`）、动能、校验和、时序 | **标量浮点** | 它们本质是标量的，且必须与宿主参考一致 |
| `accel()` 与势能求和 | **`-mavx512f`** | 那是拿到 512-bit 指令的唯一途径（[N2](N2-t9-gemm-isa-constraints_CN.md)） |

两者无法共存于一个文件。`-mavx512f` 会把整个编译单元切到通用 AVX-512F，
于是其中任何标量浮点都变成 xmm 指令，而 KNC 没有 xmm。反过来，把标量代码留在
`-mavx512f` 之外用 `-O2` 编译，会生成在卡上崩的 `vpackstorelpd` 形式（§3）。

于是：

```
nbody_vec.cpp    -O2 -mavx512f -fopenmp -c     -> nbody_vec.o
nbody_sink.cpp   -O0          -fopenmp -c     -> nbody_sink.o
k1om_cxx -fopenmp -rdynamic nbody_vec.o nbody_sink.o -lcoi_device -lgomp ... -> nbody_sink
```

两者之间的接口刻意收得很窄，且**完全不含浮点**——只有 `int` 和 `double *`：

```c
extern "C" void nbody_accel(int n, const double *x, const double *y,
                            const double *z, const double *m,
                            const double *soft2p,
                            double *ax, double *ay, double *az);
```

`soft2` 用指针而不是值传递，这样就没有任何 `double` 以寄存器形式跨越 ABI 边界。
考虑到两个单元编译目标不同，多这一个参数是值得的。

**这套拆法可以复用。** 任何同时需要标量超越函数与向量数学的卡端程序都应该这样
构建，而不是去寻找能调和二者的编译标志；那样的标志并不存在。

## 2. 坑 1 —— `calloc` 的对齐不够，而 KNC 没有非对齐加载

第一版向量化构建：卡端进程死于 `COI_PROCESS_DIED(23)`。卡端内核日志：

```
nbody_sink[...] general protection ip:40142f
```

反汇编该地址：

```
40142f:  62 31 f9 08 28 0c 19    vmovapd (%rcx,%r11,1),%zmm9
```

`vmovapd` 要求 **64 字节对齐**；`(%rcx,%r11)` 是 `x + i0·8`，`i0` 是 8 的倍数，
所以下标贡献的偏移一定是 64 的倍数——问题出在基址指针上。`x` 来自 `calloc`，
而它只保证 16 字节。

KNC **没有任何非对齐向量访存**——没有 `vmovupd`、没有 `vmovups`；唯一的非对齐
形式是 `VLOADUNPACK`/`VPACKSTORE` 这一对，而它们恰恰是别处会崩的那两条（§3）。
所以在非对齐地址上发一条对齐加载，不是性能问题，是异常。

修法：把所有会传进向量内核的数组都按 64 字节对齐分配。

```c
static double *alloc64(int n)
{
    void *p = NULL;
    if (posix_memalign(&p, 64, (size_t)n * sizeof(double)) != 0) return NULL;
    if (p) memset(p, 0, (size_t)n * sizeof(double));
    return (double *)p;
}
```

给读者提一句这个失效模式：*症状*只是「进程死了」，卡端一言不发。诊断完全来自
卡端内核日志加故障地址——这正是 `t7_nbody.sh` 要抓那份日志、并在命中时判失败
的原因。

## 3. 坑 2 —— `VCMPPD` 的谓词是 3 位，而 GCC 编了 5 位

对齐修好后，故障换了性质：

```
nbody_sink[...] trap invalid opcode ip:4014ec
```

```
4014ec:  62 f1 e9 08 c2 cf 0d    vcmppd $0xd,%zmm7,%zmm2,%k1
```

立即数 `0xd` = 13。那是 `_CMP_GE_OS` 的标准 AVX-512 编码，也正是源码所要求的
（`_mm512_cmp_pd_mask(k, threshold, _CMP_GE_OS)`）。而 ISA 手册 §6.3 写着：

> **Immediate Format** —— Comparison Type `I2 I1 I0`：eq 000、lt 001、le 010、
> unord 011、neq 100、nlt 101、nle 110、ord 111
> …`{gt}` A > B —— **Swap operands, use LT**；`{ge}` A >= B —— **Swap operands, use LE**

KNC 的 `VCMPPD` 只使用 `IMM8[2:0]`。它没有 `ge` 谓词；手册自己给出的替代方案是
交换操作数、改用 `le`。GCC 照样发出了 5 位的 AVX-512 形式，硬件便把它报成非法
指令。

修法——按手册规定的形式书写比较：

```c
/* "k >= 阈值"  等价于  "阈值 <= k" */
mk = _mm512_cmp_pd_mask(NK(0), k, _CMP_LE_OS);
```

编码为立即数 2。四处缩放全部改掉，整个阶段随即通过。

**这是一个普遍性隐患，不是一次性事故。** 凡是 KNC 的 MVEX 编码与 AVX-512 不同的
地方——§1 与 [N10](N10-t7-gemm-fp64-no-transcendentals_CN.md) §2 已经列出好几处
——GCC 都会理直气壮地发出 AVX-512 形式，因为它编译时对着的 `avx512fintrin.h`
描述的就是 AVX-512。汇编器能拦住「指令根本不存在」的情形；它**拦不住**「指令存在
但立即数或操作数编码含义不同」的情形。带越界谓词的 `VCMPPD` 属于后者，
只有硬件能抓到。

实用规则：凡是行为由立即数选择的指令（比较、舍入、转换、KNC 上的 shuffle），
都去查手册的取值表，别信 intrinsic 的名字。

## 4. 坑 3 —— 一直在崩 T7 的那条 `vpackstorelpd`

这个坑早于本轮优化，而这次工作终于把它解释清楚了。T7 长期被钉在 `-O0`，
因为 `-O1`/`-O2` 会崩。一个 `-O2 -fno-tree-vectorize` 构建的卡端日志：

```
segfault at 0 ip 00000000004017b1 error 6
```

而该地址处是：

```
4017b1:  62 d2 f9 0a d1 44 24 02    vpackstorelpd %zmm0,0x10(%r12){%k2}
```

`vpackstorelpd` 是 KNC 的 *pack and store unaligned* ——掩码压缩存储。把能跑与
崩溃两份产物逐条对比：

| 构建 | `vpackstore`/`vscatter` 条数 | 寻址形式 |
|---|---|---|
| `-O0`（能跑） | 12 | **全部** `-0xNN(%rbp)` |
| `-O2 -fno-tree-vectorize`（崩） | 11 | 十条 `(%rsp)`，**一条 `0x10(%r12)`** |

崩掉的那条是**单个 double 的存储**——紧邻它之前有 `kmov $1, %k2`，所以只写 8
字节——而且基址寄存器不是 `rbp`/`rsp`。用 `rbp`/`rsp` 时编码被迫使用 32 位位移；
用 `r12` 时汇编器用了 KNC 的压缩 `disp8*N` 形式。手册恰好把这一族标了出来：

> 注意有些指令工作在**元素粒度**而非整向量粒度，因此应使用 "element level"
> 那一列……（即 VLOADUNPACK、VPACKSTORE、VGATHER、VSCATTER 指令）

也就是对 `VPACKSTORE` 而言，位移按**元素**大小（double 是 8 字节）缩放，而不是
按向量大小。无论确切机理如何，观测是扎实且可复现的：`rbp`/`rsp` 形式能跑，
`r12` + `disp8*N` 形式崩。

两个实际结论：

- **条数确实不是判据**——12 条能跑、11 条崩溃——这印证了套件长期以来的注记。
  真正不同的是**寻址形式**。
- **出路不是修 `vpackstorelpd`，而是不生成它。** 向量内核产出**零**条，因为它
  从不把标量 double 从向量寄存器存出：结果以整 8 车道的 `_mm512_store_pd` 写入
  64 字节对齐数组，其余交给标量单元。这就是 `nbody_vec.cpp` 能用
  `-O2 -mavx512f` 构建并运行、而 `nbody_sink.cpp` 必须保持 `-O0` 的原因。

## 5. 让这一切变得可解的那套排查流程

三个坑都是同一种方法找出来的，这个顺序值得复用：

1. 跑阶段，它失败并只给出 `COI_PROCESS_DIED(23)`，没有别的信息。
2. 读**卡端**内核日志（卡上的 `/var/log/messages`；卡上的 `dmesg` 会在两台主机
   之间交替、并在 240 线程下把行撕碎）。它会给出故障类别与指令指针。
3. 反汇编**构建出来的 sink**，不是目标文件——日志里的地址是最终链接地址。
4. 认出那条指令，然后问手册它到底要求什么。
5. 改**源码**让那条指令不再被生成，再复查反汇编里没有 `%xmm`/`%ymm`，
   以及（相关时）没有 `vpackstore`/`vscatter`。

第 2 步是最容易被跳过、也最无法替代的一步。

## 6. 规则

> 当卡端程序同时需要标量超越函数与向量数学时，拆成两个编译单元，各给各的标志
> ——它们之间的接口只传指针和 int，绝不传 `double`。
> 然后把卡端内核日志当作调试器：`general protection` 意味着需要对齐的指令碰上
> 了非对齐地址（按 64 字节分配），`trap invalid opcode` 意味着硬件不实现该编码
> ——很可能因为 GCC 发出的是某条指令的 AVX-512 形式，而 KNC 形式不同。
> 查手册的立即数取值表，别信 intrinsic 的名字。
