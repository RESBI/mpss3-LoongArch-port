# 附录 F　在龙芯上使用这张卡：原生 OpenMP 与手写 COI

　　前面十二章回答的是「驱动与工具能不能移植」。这一节回答之后的一个问题：**卡既然已经在龙芯机器上跑起来了，怎么用它算东西，尤其是怎么用 OpenMP。** 内容以本机实测为准，凡未实测的都明确标出。本附录的结论有一部分是「此路不通」，那部分同样附上了判据 —— 它对后来者比「怎么走」更省时间。

---

## F.1　实测：宿主侧 COI 已经在龙芯上跑通

　　移植版 `libcoi_host.so` 装上之后，另写了一个探针（`coi_offload_probe.c`），在龙芯上编译、以 root 运行，逐项调用 COI 的引擎接口。实测输出（原文照录）：

```text
=== 1. 枚举引擎 ===
  COIEngineGetCount(COI_ISA_MIC) -> COI_SUCCESS(0), engines = 1
  engine 0: COIEngineGetHandle -> COI_SUCCESS(0), handle=valid
      (COIEngineGetInfo failed - struct size mismatch?)
```

　　这两行是本附录的地基：**移植到 LoongArch 的 COI 宿主库，在真机上把这张卡枚举成了一个可用引擎，并成功取到了句柄。** 也就是说「宿主程序用 COI 指挥卡」这条链路的入口是通的。

　　唯一没成功的是 `COIEngineGetInfo` —— 它按尺寸校验入参，探针第一次传的是本机头文件里 `COI_ENGINE_INFO` 的大小，被拒了。第二版探针改成依次尝试 4096／2048／…／64 字节并报出被接受的那个尺寸，用来判断是头文件与库的结构体版本差异还是纯粹的长度校验。结果见[附录 H](H-offload-field-notes_CN.md) H.9.6：传 5200 字节得 `COI_ERROR(1)`、其他尺寸得 `COI_SIZE_MISMATCH(12)`，说明移植出的头文件与库对这个结构体的认知不同，**至今未解释**。**这一项不影响引擎枚举、句柄获取与进程创建** —— 后来的端到端链路（H.13）根本没有用到它。

　　接着尝试在卡上创建进程：

```text
=== 2. create a process on the card (offload smoke test) ===
  launching: /bin/sh -c uname -a; id; echo COI-OFFLOAD-OK > /tmp/coi_offload_probe.out
  COIProcessCreateFromFile -> (error)(22), process=null
```

　　`22` 这个数字要从 COI 自己的枚举表里读。`src/include/common/COIResult_common.h:52` 起的 `COIRESULT` 顺序编号，第 22 项是：

```text
COI_BINARY_AND_HARDWARE_MISMATCH   ///< A specified binary will not run on the specified hardware.
```

　　也就是**「该二进制不能在这块硬件上作为 COI 进程运行」**。这一句把「还缺什么」指得很明确，见 F.4。

## F.2　卡侧运行时：镜像里有什么、没有什么（实测）

　　把发布版里的卡端镜像列开逐项核对，与计算和 offload 有关的都在下表。判据是镜像内的文件路径与实际 ELF 头（`readelf -h` 报 `Machine: Intel K1OM`，即 `EM_K1OM` 181）。

| 项目 | 存在 | 说明 |
|---|:--:|---|
| `coi_daemon` | 有 | 卡侧 COI 守护进程，235,760 字节，k1om ELF，依赖 `libscif.so.0` |
| `libcoi_device.so.0` | 有 | 卡侧 COI 设备库，485,576 字节，k1om ELF，依赖 `libscif.so.0` |
| `libmyo-service.so.0` | 有 | MYO 的卡侧服务，与宿主侧移植过的 `libmyo-client.so` 配对 |
| `libscif.so.0` | 有 | 卡侧 SCIF 库 |
| glibc | 有 | `libc-2.21.so`，动态解释器 `ld-linux-k1om.so.2` —— 卡端二进制的目标 ABI |
| `libgomp` | **无** | 镜像里没有任何 OpenMP 运行时，MPSS 的 RPM 清单里也一个都没有，要自己编 |
| 任何 COI 应用程序 | **无** | 全镜像扫描只有 `coi_daemon` 与库，没有示例、没有 sink 程序 |
| 编译器 | **无** | 卡上不能编译，只能送二进制上去 |

　　另有一处实测修正了早先的结论：**`coi_daemon` 现在随开机自动启动。** MicDir 生成的镜像里有 `/etc/init.d/coi` 与 `rc?.d` 下的 `S95coi` 链接，卡进入 `online` 后 `pidof coi_daemon` 有值（实测 5591）。我们最初手工改制的那份镜像没有这个启动脚本 —— 这才是本报告早期 COI 探针报「0 引擎」的真正原因，与移植质量无关。

## F.3　能走的路与走不通的路

### F.3.1　先说不通的那条：`#pragma omp target` 从 GCC offload 到 KNC

　　结论很硬：**上游 GCC 从来没有 KNC 的代码生成能力，它的 MIC offload 目标是 KNL。** 依据有两条，相互独立：

1. Intel 负责 GCC 该特性的工程师 2014 年在 GCC 邮件列表上明确写过：KNC 这块当前硬件「不会被 GCC 支持」，当时的主干并不支持 KNC 代码生成。
2. 版本沿革与之吻合：MIC offload 自 GCC 5 引入、GCC 12 标记废弃、**GCC 13 整个移除**（删掉 `liboffloadmic/` 共 114 个文件与 `intelmic-mkoffload` 等）。它一路产出的都是 x86-64 加 AVX-512 的 KNL 目标码，而不是 `EM_K1OM` 的卡端代码。

　　在龙芯上这条路还叠了另外两道墙：`liboffloadmic/configure.tgt` 直接把非 x86 宿主判为不支持；而且 offload 要求宿主编译器与目标编译器**同源同版本**（二者要互相传递 LTO 中间码），唯一带 k1om 代码生成的树是 GCC 5.1.1，而 **LoongArch 直到 GCC 12 才进入 GCC 主干** —— 一棵树满足不了这两个条件。

　　还可以补一条旁证：MPSS 自带的 `mpss-offload` 与 `mpss-offload-dev` 两个包是**空壳**（各两三千字节、零个文件），Intel 面向 ICC 的 offload 运行时其实是随 Composer XE 而非 MPSS 发布的。公开资料里也**查不到任何**「用 GCC 在 KNC 上跑通 OpenMP」的报告 —— 无论原生还是 offload，这与上面两条判据互相印证。

　　唯一值得记下的正面发现：`liboffloadmic` 在宿主侧是 `dlopen("libcoi_host.so.0")` 加 `dlvsym(..., "COI_1.0")` 去调 COI 的，**正是我们移植时保留的那套 COI 1.0 符号版本**。也就是说如果哪天真有人把 offload 运行时搬到龙芯，我们的库在 ABI 这一层是正好对上的；卡住的不是这一层，而是编译器。

### F.3.2　可行的两条

| 路线 | 机制 | 现在具备 | 还缺 |
|---|---|---|---|
| **A　原生 OpenMP**（推荐起步） | 用 k1om 编译器把 `-fopenmp` 程序编成卡端二进制，连同自建的 k1om `libgomp` 一起送上去，直接在卡上跑 | 卡侧运行时齐全（glibc 2.21、libscif、libcoi_device、libmyo-service）／卡上有 sshd，`scp` 上去即可，不必依赖 `micnativeloadex` | ① 能产出 k1om 代码的编译器（见 F.3.3）② **k1om 版 `libgomp`**，必须自己编 —— MPSS 的编译器配置里写的就是 `--disable-libgomp`，镜像与 RPM 清单里都没有它 ③ k1om binutils（`--march=k1om` 的 as 与 `-m elf_k1om` 的 ld） |
| **C　手写 COI／MYO** | 宿主程序直接调 COI 或 MYO 的 API 指挥卡，数据用缓冲或 SCIF 搬 | **宿主侧 COI 已移植并在真机验证**（F.1）；MYO 宿主库也已移植并跑通 | 卡端要跑的东西必须是「COI 程序」（F.4），这仍然回到路线 A 的工具链前置 |

### F.3.3　路线 A 里最实际的一步：不要让工具链去迁就龙芯

　　这里有一个容易踩的预期错误：**不要把「在龙芯上构建 k1om 交叉编译器」当成第一步。** 那棵 k1om 树是 GCC 5.1.1，它既不能在 loongarch64 上运行（LoongArch 支持从 GCC 12 才开始），也不能与 GCC 12 的 offload 机制拼在一起。真正低成本的做法是**让现成的 x86_64 版 k1om 工具链在龙芯上跑起来**：

- 用 **qemu-user**（或 box64／FEX 之类的 x86-64 用户态模拟）直接运行 MPSS 自带的 `k1om-mpss-linux-gcc` 与它的 `as`／`ld`。它们都是普通的 x86-64 Linux 用户态程序，没有任何特权要求，也不需要内核支持；
- 同一棵树上重编 `libgomp` 与目标库 —— 去掉 `--disable-libgomp`，保留 `--target=k1om-mpss-linux` 与 MPSS 的 k1om sysroot，产出卡端 `libgomp.so` 与 `omp.h`；
- 之后编出来的 k1om 二进制由卡直接执行，龙芯这边只是「用模拟器跑编译器」，运行时零开销。

　　若追求「不依赖模拟器」，另一条路是把 Intel 的 k1om 后端前向移植到 GCC 12 以上再以龙芯为宿主构建。那是编译器移植级别的项目，工作量与 G.6 里评估的卡端内核移植同量级，不建议作为起点。

## F.4　为什么卡端程序必须是「COI 程序」

　　错误码 22 说的「二进制与硬件不匹配」，指的并不是 ELF 机器类型不对 —— 卡上的 `/bin/sh` 本来就是 k1om ELF。COI 检查的是另一件事：**目标程序是否带有 COI 的进程引导与设备运行时**，也就是它是否链接了卡侧 `libcoi_device`（COI 的头文件里把这套卡端接口叫 sink 与 source 两套）。COI 创建进程时要完成设备端初始化，让该进程能与宿主建立缓冲与流水线通道，普通程序没有这套东西。

　　这与卡镜像的扫描结果一致：镜像里只有 `coi_daemon` 与库，**没有任何链接 `libcoi_device` 的应用程序**。所以「用 COI 在卡上跑点什么」卡在缺少一个用 k1om 编译器加 `-lcoi_device` 编出来的**卡端 sink 程序**上，而不是卡在协议、权限或移植缺陷上。

　　部署上还有一条省事的路：把编好的 k1om 二进制与自建的 `libgomp.so` 一起 `scp` 上卡、直接用卡上的 ssh 运行，全程不需要宿主侧 COI，也不需要 `micnativeloadex`。

　　顺带一句：MPSS 的 `micnativeloadex` 是宿主侧工具，源码就在我们已经移植过的 `mpss-coi` 树里（`src/tools/micnativeloadex/`），它内部同样是走 COI 建进程。若要用它来做「原生」程序的投送，也要先解决 F.4 这一关，或者干脆用 `scp` 加 `ssh` 绕开它 —— 对原生 OpenMP 来说，后者的信息量完全够用。

　　本附录讲的是路线与判据；**实际动手的结果（k1om 工具链、自建 libgomp、卡上第一次 OpenMP 实测、手写 COI offload 打通）记在[附录 H](H-offload-field-notes_CN.md)**。其中已经完成的部分是：k1om 交叉编译器在龙芯上可用、卡上原生 OpenMP 跑通（61 线程 36 倍加速）、卡端 worker 与宿主侧 COI 运行时的端到端链路跑通（H.13：宿主建进程 → 按名取函数 → 卡上 OpenMP 计算 → 结果取回，校验和与宿主参考逐位一致）；仍未完成的是**编译器自动生成型 offload**（路线 B），而它卡在编译器上、不在运行时上（F.3.1）。把上述能力变成可重复执行的验收步骤，见[附录 J](J-acceptance-tests_CN.md)。

## F.5　当时列出的下一步（现已全部走完）

　　本节保留原始清单，并标注每一项后来落在哪里 —— 它同时也是「从读到做」的路线图：

| # | 原定步骤 | 结果 |
|---|---|---|
| 1 | 再跑一次引擎链路探针，记下 `COIEngineGetInfo` 接受的尺寸 | 已跑：该接口的结构体尺寸仍不匹配（见 F.1 与 H.9.6），**至今未解释**，但不影响任何后续步骤，故未再纠缠 |
| 2 | 让 k1om 工具链在龙芯上可用 | **已完成**（H.2：用 box64 跑 MPSS 自带的 x86_64 k1om 编译器，产物 `readelf -h` 报 `Intel K1OM`） |
| 3 | 建 k1om sysroot | **已完成**（H.1：MPSS SDK 的 k1om RPM，解出 1366 个头文件与 237 个库） |
| 4 | 编 k1om 版 `libgomp` | **已完成**（H.4：`libgomp.so.1.0.0`，720,422 字节，`Machine: Intel K1OM`） |
| 5 | 第一次真算（`-fopenmp` 程序送上卡运行） | **已完成**（H.5：2 亿项归约，61 线程 36.3 倍加速；后续在宿主发起的 offload 里用满 240 个硬件线程） |
| 6 | 再考虑手写 offload | **已完成**（H.7 立起骨架，H.13 打通端到端：三处缺陷、卡上 N 体实测与仍存在的两条限制都在 H.13；可重复执行的判据见[附录 J](J-acceptance-tests_CN.md)） |

　　换句话说：本附录列出的两条可行路线（A 卡上原生 OpenMP、C 手写 COI）**都已经走通并留下了可复现的记录**；唯一没有走通的是路线 B（编译器自动生成 offload），而它卡在编译器一侧（F.3.1），不是移植问题。

## F.6　与报告其余部分的关系

　　本报告前十二章与附录 E 证明的是「主机侧工具端能在龙芯上完整工作」，包括经 SCIF 在卡上建用户、注入密钥、读写卡状态。本附录补上的是**卡的另一种用法：把它当计算设备**。两者的交界处是 COI —— 它既是管理面的近邻，也是 offload 运行时的落地接口。宿主侧已经验证到引擎与句柄这一层，剩下的缺口是**编译器与运行时**，不再是移植。

　　也因此，附录 G 的结论在这里提前出现了一半：**卡本身（内核 + 用户态）不需要更新，也能被龙芯宿主充分利用**；真正需要新建的是那套能产出 k1om 二进制的工具链，而它只影响「卡上跑什么」，不影响「宿主怎么指挥卡」。

## F.9　编写 offload / COI 程序前的约束清单（细则见附录 K、L）

　　本节只列**必须遵守的条目**，每条细则与实测依据在[附录 K](K-offload-memory-rules_CN.md)（内存搬运）与[附录 L](L-api-determinism-rules_CN.md)（逐 API 确定性）里，避免两处维护同一份内容。

| # | 约束 | 一句话理由 | 细则 |
|---|---|---|---|
| 1 | 注册窗口**每窗口 ≤ 1 MiB**，段数 ≤ 512 | 卡端段表只传一页（512 项）；1 MiB 窗口实测三档全绿 | [K.1/K.2](K-offload-memory-rules_CN.md) |
| 2 | 大数组**按窗口切片**搬运，不要整块注册 | 窗口大小与总量解耦；4 GiB 走 1 MiB 窗口已实测通过 | [K.2](K-offload-memory-rules_CN.md) |
| 3 | 单次 `scif_writeto` **≤ 1 MiB**（已验证到 1 MiB） | 阶梯实测 26496 B → 1 MiB 全部 checksum 一致 | [L.2.4](L-api-determinism-rules_CN.md) |
| 4 | 任何"写完→读"的依赖都要**显式同步**（`SCIF_RMA_SYNC` 或 fence） | 头文件原话：不加同步则"non-deterministic" | [L.1](L-api-determinism-rules_CN.md) |
| 5 | 注册地址按**宿主页**对齐、长度按 4 KiB 对齐；注意 `RLIMIT_MEMLOCK` | 驱动的参数校验；pin 受 ulimit 限制 | [L.2.3](L-api-determinism-rules_CN.md) |
| 6 | 注销窗口前确认**在途 RMA 已完成** | 否则与在途 DMA 竞争（早期版本在此卡死） | [L.2.3](L-api-determinism-rules_CN.md) |
| 7 | 优先用 **COI 的 API** 而不是自己拼 SCIF | COI 内部已按尺寸档注册窗口并做握手与 fence | [L.3.1](L-api-determinism-rules_CN.md) |
| 8 | 任何新写法都要过 **T4 + T8**，并检查 dmesg 无 `DESC-*`、无 `kernel BUG` | 判据现成 | [J](J-acceptance-tests_CN.md) / `tests/extra` |
