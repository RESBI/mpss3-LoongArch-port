# 附录 L　API 确定性编写规范（逐 API）

　　本附录回答一个问题：**调用这些 API 时，怎么写才能得到确定的结果**。每条规则后面都标了出处：`［头］`＝头文件原话，`［码］`＝驱动/库源码，`［测］`＝本移植的真机实测。API 面来自两侧公开头文件：SCIF 27 个函数（`release/08-mic-module/include/scif.h`）、COI 约 60 个函数（k1om SDK `usr/include/intel-coi/`）。

## L.1　确定性的分界线：同步与异步

　　SCIF 头文件把这条界线写得很直白：

> "…the transfer is complete. Otherwise, the transfer may be performed asynchronously and … **is non-deterministic**. The synchronization functions, `scif_fence_mark()`/`scif_fence_wait()` and `scif_fence_signal()`, can be used to synchronize to the completion of asynchronous RMA operations."`［头］`

　　所以**第一条总规则**：**任何"我写完 → 你去读"的依赖，都必须显式同步**，三种做法任选其一：

| 做法 | 写法 | 代价 |
|---|---|---|
| 同步 RMA | `scif_writeto(..., SCIF_RMA_SYNC)` | 每次调用等完成，吞吐最低、语义最直白 |
| 栅栏 | 批量写 → `scif_fence_signal()` → 对端 `scif_fence_wait()` | 一次同步覆盖一批传输，推荐 |
| 顺序保证 | `SCIF_RMA_ORDERED`（保证最后一个缓存行的可见顺序）`［头］` | 只保证顺序，不保证"全部完成" |

　　实测对照（T8）：同一份 4 GiB 数据，用"逐窗口写入 + 窗口级握手"的方式两端 checksum 逐位一致；不握手而只靠时序假设的写法在早期版本里得到过两端不一致的结果。

## L.2　SCIF：27 个函数的确定性规则

### L.2.1　连接组

| API | 确定性规则 | 出处 |
|---|---|---|
| `scif_open` | 只创建端点的本地表示；失败返回 NULL | `［头］` |
| `scif_bind` | 端口 0 表示由系统分配；**绑定后再取端口要用同一回归值** | `［头］` |
| `scif_listen(epd, backlog)` | `backlog` 是**未完成连接队列**上限，不是并发连接数 | `［头］` |
| `scif_accept` | 非阻塞模式返回 `EAGAIN` 时必须轮询重试，不能当致命错误 | `［头］` |
| `scif_connect` | **首次调用可能返回非 0 而已连接**：本移植实测重试时会拿到 `EISCONN`，必须把它当作"已连接"处理 | `［测］` |
| `scif_close` | 对端必须能观察到断开；先关再发会被丢弃 | `［头］` |

### L.2.2　消息组

| API | 确定性规则 | 出处 |
|---|---|---|
| `scif_send` | 返回**实际发送字节数**，可能小于请求；加 `SCIF_SEND_BLOCK` 才等待 | `［头］` |
| `scif_recv` | 同上，返回值必须参与判断（短读要续读）；加 `SCIF_RECV_BLOCK` 才等待 | `［头］` |
| `scif_poll` | 超时参数是毫秒；返回就绪端点数，需自己再分发 | `［头］` |

### L.2.3　注册组（最容易出问题的一组）

| API | 确定性规则 | 出处 |
|---|---|---|
| `scif_register` | **`addr` 必须宿主页对齐**；`len` 按协议页（4 KiB）对齐且非 0，否则 `EINVAL` | `［码］` `micscif_api.c` 注册校验；`［测］`（4 KiB 对齐的 `memalign` 在 16 KiB 页宿主上曾直接 `EINVAL`） |
| 同上 | **窗口段数 ≤ 512**（卡端 4 KiB 页时 `NR_PHYS_ADDR_IN_PAGE`）；**建议单窗口 ≤ 1 MiB** | `［测］` 32 MiB 窗口 = 527 段必失败；1 MiB = 256 段三档全绿。详见[附录 K](K-offload-memory-rules_CN.md) |
| 同上 | 单段页数不得超 4095（线上 12 位字段），超限由驱动直接拒绝 | `［码］` 守卫（防御性；分段规则本身封顶 512 页/段） |
| 同上 | 注册会 pin 住内存，受 `RLIMIT_MEMLOCK` 限制（`SCIF_MAP_ULIMIT`） | `［码］` `__scif_check_inc_pinned_vm()` |
| `scif_unregister` | **注销前必须确保对该窗口的 RMA 都已完成**（用 `SCIF_RMA_SYNC` 或栅栏）；否则会与在途 DMA 竞争 | `［头］`（"While a window is in this state…"）+ `［测］`（早期版本在此处卡死） |
| `scif_pin_pages`/`scif_unpin_pages`/`scif_register_pinned_pages` | `addr`/`len` 同样按页对齐；同一组页可被多个窗口复用，引用计数由库维护 | `［头］` |

### L.2.4　数据搬运组

| API | 确定性规则 | 出处 |
|---|---|---|
| `scif_writeto` / `scif_readfrom` | **源与目的范围都必须完整落在双方已注册的窗口内**，否则行为未定义 | `［头］` |
| 同上 | **不加 `SCIF_RMA_SYNC` 时传输可能异步，"is non-deterministic"** | `［头］`（原话见 L.1） |
| 同上 | `loffset`/`roffset` **按 64 字节缓存行对齐**可获得最佳性能；不对齐但相距 64 的整数倍次之 | `［头］` |
| 同上 | 单次长度**已验证到 1 MiB**（窗口 1 MiB、总量 64 MiB 的阶梯：26496 B → 64/128/256/512 KiB → 1 MiB 全部两端 checksum 一致）；26496 字节只是历史保守值 | `［测］` 2026-10-07 标定；受卡端 512 段窗口约束，修复前无法继续放大 |
| `scif_vwriteto` / `scif_vreadfrom` | 用于**不连续的多段**目的/源；地址数组与段数必须与窗口切分一致 | `［头］` |
| 所有 RMA | **写完必须同步再让对端读**（见 L.1）；只靠"写完了"的时序假设会得到不确定结果 | `［头］` `［测］` |

### L.2.5　映射组与事件组

| API | 确定性规则 | 出处 |
|---|---|---|
| `scif_mmap` | 映射范围必须已在本地注册；`SCIF_MAP_FIXED` 时必须给出页对齐地址；每页都必须在某个已注册窗口内 | `［头］` |
| `scif_munmap` | 与 `scif_mmap` 成对；映射期间不得注销对应窗口 | `［头］` |
| `scif_get_pages` / `scif_put_pages` | 取得对端窗口的本地映射后必须成对归还 | `［头］` |
| `scif_event_register` / `scif_event_unregister` | **注销必须在模块卸载前完成**；回调里不得做阻塞操作 | `［头］`（"must be called before the module…"） |

## L.3　COI：按对象分组的确定性规则

### L.3.1　缓冲（`COIBuffer*`）

| API | 确定性规则 | 出处 |
|---|---|---|
| `COIBufferCreate` | 尺寸非页对齐会**向上取整**到页；`COI_SINK_MEMORY` 时引用计数必须为 1 | `［头］` `COIBuffer_source.h` |
| 同上 | **大缓冲在本移植上不可用**：实测 `COIBufferCreate` 对大尺寸返回 `COI_OUT_OF_MEMORY`；改用"宿主只传小参数 + 卡端自行分配"的模式（T5/T7 即如此） | `［测］` |
| `COIBufferCreateFromMemory` | 复用已有内存，**该内存的物理页映射会随写入变化**，不能假设地址稳定；可用 `COI_SINK_MEMORY` 等标志约束 | `［头］` |
| **COI 内部就在分批**（源码事实）：控制消息走一块**固定尺寸**的预注册空间（`COI_MAX_REGISTERED_MESSAGE_SIZE`，按尺寸分流），较大的负载走「按尺寸档注册窗口 → `scif_writeto` → 等完成 → 双向握手」；必要时用信号页 + fence 加速；多处 `scif_register` 调用会**循环**以满足多 DMA 通道 | 所以用户侧不必自己实现「大窗口」策略：**优先用 COI 的 API**，它已经把 L.1 的同步纪律与 K.2 的窗口纪律做在里面了；自己直接写 SCIF 时才需要照 K/L 的规则来 | `［码］` `transport/scif_comm.cpp`（消息分流与 DMA 流程的注释原文）、`mechanism/dma/dma.cpp`（"doesn't need to loop like other scif_register calls"） |
| `COIBufferMap` / `COIBufferUnmap` | `map instance` 必须原样回传；映射期间不得销毁缓冲 | `［头］` |
| `COIBufferCopy*` / `Read*` / `Write*` | 拷贝的源/目的必须落在缓冲范围内；跨缓冲拷贝用 `COIBufferCopy`（同缓冲内用 `Read/Write`） | `［头］` |

### L.3.2　进程与流水线（`COIProcess*` / `COIPipeline*`）

| API | 确定性规则 | 出处 |
|---|---|---|
| `COIProcessCreate*` | 卡端函数**必须名字修饰正确**（C++ 需 `extern "C"` 或按修饰名传入）；实测卡端 sink 还必须 `-rdynamic` 才能在 `.dynsym` 里被找到 | `［头］` + `［测］`（T5/T7） |
| 同上 | 线程亲和性设置**必须在创建线程之前**；每线程的 sticky 数据也必须先设 | `［头］` `COIProcess_source.h` |
| `COIPipelineCreate` | 栈尺寸 **≥ 16384（PTHREAD_STACK_MIN）且为页大小整数倍**；CPU 掩码至少一位；流水线数量上限 `COI_PIPELINE_MAX_PIPELINES` | `［头］` `COIPipeline_source.h` |
| `COIPipelineRunFunction` | 运行前须 `COIPipelineStartExecutingRunFunctions`（或等价的启动流程），否则任务不会被执行 | `［头］` |
| `COIProcessConfigureDMA` | 逻辑通道数**最少 2 条、最多 4 条** | `［头］` |
| `COIProcessSetCacheSize` | 大页缓存与 4K 页缓存**分开设置**，二者互不影响 | `［头］` |
| `COIEngineGetInfo` | 硬件线程上报**最多 1024 条**，超出部分不显示 | `［头］` |
| 设备混用 | **fabric 与 PCIe 连接的设备不可混用** | `［头］` `COIEngine_source.h` |

### L.3.3　事件（`COIEvent*`）

| API | 确定性规则 | 出处 |
|---|---|---|
| `COIEventWait` | `in_WaitForAll = False` 时**至少一个**事件满足即返回，需自行判断是哪一个 | `［头］` |
| `COIEventRegisterCallback` | 回调上下文必须先设好再注册；注销要在退出前完成 | `［头］` |

## L.4　OpenMP / offload 层

| 规则 | 说明 | 出处 |
|---|---|---|
| 一次 offload 的输入变量会**汇入同一个缓冲区**（大小 = 各变量字节数之和） | 所以"整块 in/out 大数组"会直接要一个大 COI 缓冲；应拆成多次 offload 或改用"参数 + 卡端生成"模式 | `［码］` `liboffloadmic/runtime/offload_host.cpp` `gather_copyin_data()` |
| `in`/`out`/`inout` 的语义边界要与显式同步一致 | `out` 只在 offload 结束时回传；中途读取是未定义值 | `［头］`（编译器/运行库契约） |
| 卡端 kernel 的优化档**必须逐源码实测** | 实测：N 体 sink 用 `-O1/-O2` 会崩，归约 sink 用 `-O2` 正常；换档前先跑一遍 | `［测］` 附录 H H.14 / 附录 J 第 2 条 |
| 卡端 sink 需 `-rdynamic` 且符号在 `.dynsym` | 否则卡端 `dlsym` 失败、宿主收到 `COI_DOES_NOT_EXIST` | `［测］` 附录 J 第 1 条 |

## L.5　违规 → 现象对照表（本移植踩过的全部坑）

| 违规写法 | 现象 | 判据/出处 |
|---|---|---|
| 窗口过大（>512 段，如 32 MiB） | 宿主读到的段表缺尾，`scif_writeto` 返回 `EINVAL`；修好前是 `BUG` + 进程 `D` 状态 | `［测］` `DESC-BAD` / `DESC-INCONSISTENT` 探针 |
| 注册地址未按**宿主页**对齐（4 KiB 而非 16 KiB） | `scif_register` 直接 `EINVAL` | `［测］` |
| 不显式同步就依赖"写完即可读" | 两端数据不一致（无任何错误码） | `［头］` "non-deterministic" |
| 单次 RMA 超过已验证长度 | 早期版本直接卡死（真实上限未标定） | `［测］` 用 26496 字节为安全值 |
| 卡端 sink 缺 `-rdynamic` | `COI_DOES_NOT_EXIST(5)` | `［测］` T5 |
| 大缓冲走 `COIBufferCreate` | `COI_OUT_OF_MEMORY(13)` | `［测］` T5/T7 |
| pin 超过 `RLIMIT_MEMLOCK` | `scif_pin_pages` 失败（`ENOMEM`/`EPERM`） | `［码］` |
| 注销窗口时仍有在途 RMA | 早期版本卡死在注销路径（`wchan=micscif_unregister_all_windows`） | `［测］` |
| `scif_connect` 把 `EISCONN` 当失败 | 明明连上了却报错退出 | `［测］` |

## L.6　一页速查

1. **注册**：地址按宿主页对齐、长度按 4 KiB 对齐；窗口 ≤ 1 MiB；注意 `RLIMIT_MEMLOCK`。
2. **搬运**：范围在双方窗口内；加 `SCIF_RMA_SYNC` 或栅栏；单次 26496 字节；偏移尽量 64 字节对齐。
3. **同步**：任何"写完→读"的依赖都要显式同步；不要靠时序。
4. **注销**：先确认在途 RMA 完成，再注销窗口，最后关端点。
5. **COI**：小参数进、卡端自分配；函数名/`-rdynamic`/栈尺寸/CPU 掩码按 L.3 给。
6. **判据**：任何新写法都要过 T4（数据面）+ T8（大数据量）+ dmesg 零 `DESC-*`/零 `kernel BUG`。
