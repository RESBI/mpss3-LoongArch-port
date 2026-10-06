# Appendix L Per-API determinism rules

This appendix answers one question: **how must these APIs be called to get deterministic results?** Every rule carries its source: `[hdr]` for the header's own words, `[src]` for driver or library source, `[meas]` for measurements taken on this port. The API surface comes from the public headers on both sides: 27 SCIF functions (`release/08-mic-module/include/scif.h`) and roughly 60 COI functions (k1om SDK `usr/include/intel-coi/`).

## L.1 Where determinism is decided: synchronous versus asynchronous

The SCIF header draws the line bluntly:

> "...the transfer is complete. Otherwise, the transfer may be performed asynchronously and ... **is non-deterministic**. The synchronization functions, `scif_fence_mark()`/`scif_fence_wait()` and `scif_fence_signal()`, can be used to synchronize to the completion of asynchronous RMA operations." `[hdr]`

Hence the **first overall rule**: **every "I wrote, now you read" dependency needs an explicit synchronisation**, and there are three ways to get one:

| Approach | How | Cost |
|---|---|---|
| Synchronous RMA | `scif_writeto(..., SCIF_RMA_SYNC)` | Waits for completion on every call: lowest throughput, simplest semantics |
| Fences | batch writes, then `scif_fence_signal()`; the peer does `scif_fence_wait()` | One synchronisation covers a batch of transfers — recommended |
| Ordering only | `SCIF_RMA_ORDERED` (orders the visibility of the last cacheline) `[hdr]` | Guarantees ordering, not completion |

Measured contrast (T8): the same 4 GiB moved as "window-at-a-time writes plus a window-level handshake" produced bit-for-bit identical checksums on both sides, while an earlier version that assumed ordering without a handshake produced mismatching checksums.

## L.2 SCIF: determinism rules for its 27 functions

### L.2.1 Connections

| API | Rule | Source |
|---|---|---|
| `scif_open` | Creates only the local endpoint representation; NULL on failure | `[hdr]` |
| `scif_bind` | Port 0 lets the system choose; read the assigned port back through the same call's out value | `[hdr]` |
| `scif_listen(epd, backlog)` | `backlog` bounds the **pending-connection queue**, not concurrent connections | `[hdr]` |
| `scif_accept` | In non-blocking mode `EAGAIN` must be polled and retried, never treated as fatal | `[hdr]` |
| `scif_connect` | **The first call can return non-zero while the connection is in fact established**: measured on this port, a retry reports `EISCONN`, which must be treated as connected | `[meas]` |
| `scif_close` | The peer must be able to observe the disconnect; anything sent after closing is dropped | `[hdr]` |

### L.2.2 Messages

| API | Rule | Source |
|---|---|---|
| `scif_send` | Returns the **number of bytes actually sent**, which may be short; `SCIF_SEND_BLOCK` makes it wait | `[hdr]` |
| `scif_recv` | Same; the return value must drive a continuation read on a short receive; `SCIF_RECV_BLOCK` makes it wait | `[hdr]` |
| `scif_poll` | Timeout is in milliseconds; the return value counts ready endpoints and dispatching is the caller's job | `[hdr]` |

### L.2.3 Registration (the group that breaks most easily)

| API | Rule | Source |
|---|---|---|
| `scif_register` | **`addr` must be host-page aligned**; `len` must be a non-zero multiple of the protocol page (4 KiB), otherwise `EINVAL` | `[src]` the checks in `micscif_api.c`; `[meas]` a 4 KiB-aligned `memalign` was rejected outright on a 16 KiB-page host |
| same | **A window may hold at most 512 chunks** (`NR_PHYS_ADDR_IN_PAGE` with 4 KiB card pages); **keep single windows at 1 MiB or less** | `[meas]` a 32 MiB window (527 chunks) always fails; 1 MiB (256 chunks) is green at all three sizes. See [Appendix K](K-offload-memory-rules.md) |
| same | A single chunk may not exceed 4095 pages (the twelve-bit wire field); the driver refuses it outright | `[src]` the guard (defensive only: the chunking rule itself caps a chunk at 512 pages) |
| same | Registration pins memory and is subject to `RLIMIT_MEMLOCK` (`SCIF_MAP_ULIMIT`) | `[src]` `__scif_check_inc_pinned_vm()` |
| `scif_unregister` | **Every RMA into the window must have completed first** (`SCIF_RMA_SYNC` or a fence); otherwise it races DMA in flight | `[hdr]` ("While a window is in this state...") plus `[meas]` an early version wedged exactly here |
| `scif_pin_pages` / `scif_unpin_pages` / `scif_register_pinned_pages` | `addr`/`len` are page-aligned here too; one set of pages may back several windows, with the library keeping the reference count | `[hdr]` |

### L.2.4 Data movement

| API | Rule | Source |
|---|---|---|
| `scif_writeto` / `scif_readfrom` | **The source and destination ranges must lie entirely inside registered windows on both sides**, or behaviour is undefined | `[hdr]` |
| same | **Without `SCIF_RMA_SYNC` the transfer may be asynchronous and "is non-deterministic"** | `[hdr]` (quoted in L.1) |
| same | `loffset`/`roffset` should be **64-byte cacheline aligned** for best performance; unaligned but separated by a multiple of 64 is second best | `[hdr]` |
| same | A single transfer is **verified up to 1 MiB** (ladder at a 1 MiB window and 64 MiB total: 26496 B then 64/128/256/512 KiB and 1 MiB, checksums matching at every step); 26496 bytes was only a historical conservative value | `[meas]` calibrated 2026-10-07; the card's 512-chunk window limit blocks going further until it is fixed |
| `scif_vwriteto` / `scif_vreadfrom` | For **scattered** destinations/sources; the address arrays and counts must match the window's chunking | `[hdr]` |
| all RMA | **Synchronise after writing and before the peer reads** (see L.1); assuming "the write finished" yields non-deterministic results | `[hdr]` `[meas]` |

### L.2.5 Mapping and events

| API | Rule | Source |
|---|---|---|
| `scif_mmap` | The range must already be registered locally; `SCIF_MAP_FIXED` requires a page-aligned address; every page must fall inside some registered window | `[hdr]` |
| `scif_munmap` | Pairs with `scif_mmap`; the corresponding window must not be unregistered while mapped | `[hdr]` |
| `scif_get_pages` / `scif_put_pages` | A local mapping of a peer window must be returned in pairs | `[hdr]` |
| `scif_event_register` / `scif_event_unregister` | **Unregistration must happen before the module is unloaded**, and callbacks must not block | `[hdr]` ("must be called before the module...") |

## L.3 COI: determinism rules by object

### L.3.1 Buffers (`COIBuffer*`)

| API | Rule | Source |
|---|---|---|
| `COIBufferCreate` | A non-page-aligned size is **rounded up** to a page; with `COI_SINK_MEMORY` the reference count must be 1 | `[hdr]` `COIBuffer_source.h` |
| same | **Large buffers are unusable on this port**: measured, `COIBufferCreate` returns `COI_OUT_OF_MEMORY` for large sizes. Use the "host sends small parameters, the card allocates" pattern instead (as T5/T7 do) | `[meas]` |
| `COIBufferCreateFromMemory` | Backs a COI buffer with existing memory whose **physical page mapping changes as it is written**, so addresses must not be assumed stable; flags such as `COI_SINK_MEMORY` constrain this | `[hdr]` |
| **COI batches internally** (source fact): control messages go through a **fixed-size** pre-registered space (`COI_MAX_REGISTERED_MESSAGE_SIZE`, with a size-based split), while larger payloads take the path "register a window for this size class, `scif_writeto`, wait for completion, two-way handshake", with a signal page plus fences when speed matters; several `scif_register` call sites **loop** to cover multiple DMA channels | So an application does not need to invent a large-window strategy: **prefer COI's own API**, which already implements L.1's synchronisation discipline and K.2's window discipline; the K/L rules apply when you drive SCIF directly | `[src]` `transport/scif_comm.cpp` (the comment describing the message split and the DMA flow), `mechanism/dma/dma.cpp` ("doesn't need to loop like other scif_register calls") |
| `COIBufferMap` / `COIBufferUnmap` | The map instance must be handed back unchanged, and the buffer must not be destroyed while mapped | `[hdr]` |
| `COIBufferCopy*` / `Read*` / `Write*` | Source and destination must lie inside the buffers; use `COIBufferCopy` across buffers and `Read`/`Write` within one | `[hdr]` |

### L.3.2 Processes and pipelines (`COIProcess*` / `COIPipeline*`)

| API | Rule | Source |
|---|---|---|
| `COIProcessCreate*` | Card-side functions **must be correctly name-mangled** (C++ needs `extern "C"` or the mangled name), and measured, a card-side sink also needs `-rdynamic` to appear in `.dynsym` | `[hdr]` + `[meas]` (T5/T7) |
| same | Thread affinity **must be set before the threads are created**, and per-thread sticky data must be set first as well | `[hdr]` `COIProcess_source.h` |
| `COIPipelineCreate` | Stack size **≥ 16384 (PTHREAD_STACK_MIN) and a multiple of the page size**; the CPU mask needs at least one bit; the number of pipelines is capped by `COI_PIPELINE_MAX_PIPELINES` | `[hdr]` `COIPipeline_source.h` |
| `COIPipelineRunFunction` | `COIPipelineStartExecutingRunFunctions` (or the equivalent startup sequence) must have run first, or the work never executes | `[hdr]` |
| `COIProcessConfigureDMA` | Logical channels: **at least 2, at most 4** | `[hdr]` |
| `COIProcessSetCacheSize` | The huge-page cache and the 4K-page cache are **configured separately** and do not affect each other | `[hdr]` |
| `COIEngineGetInfo` | Hardware threads are reported **up to 1024**; anything beyond is not shown | `[hdr]` |
| Mixing devices | **Devices attached over fabric and over PCIe must not be mixed** | `[hdr]` `COIEngine_source.h` |

### L.3.3 Events (`COIEvent*`)

| API | Rule | Source |
|---|---|---|
| `COIEventWait` | With `in_WaitForAll = False` it returns as soon as **at least one** event satisfies the wait, so the caller must determine which | `[hdr]` |
| `COIEventRegisterCallback` | The callback context must be set before registering, and unregistration must happen before exit | `[hdr]` |

## L.4 The OpenMP / offload layer

| Rule | Why | Source |
|---|---|---|
| An offload's input variables are **gathered into a single buffer** whose size is the sum of the variables | So "one big `in`/`out` array" demands one large COI buffer; split it across offloads or switch to the "parameters plus card-side generation" pattern | `[src]` `liboffloadmic/runtime/offload_host.cpp` `gather_copyin_data()` |
| The semantics of `in`/`out`/`inout` must line up with explicit synchronisation | `out` is only copied back when the offload ends; reading it earlier yields undefined values | `[hdr]` (compiler/runtime contract) |
| A card-side kernel's optimisation level **must be measured per source file** | Measured: the N-body sink crashes at `-O1`/`-O2` while the reduction sink is fine at `-O2`; re-run before changing the level | `[meas]` Appendix H H.14 and Appendix J, pitfall 2 |
| A card-side sink needs `-rdynamic` with its symbols in `.dynsym` | Otherwise the card's `dlsym` fails and the host sees `COI_DOES_NOT_EXIST` | `[meas]` Appendix J, pitfall 1 |

## L.5 Violation → symptom, for everything this port tripped over

| What was written | What happened | Criterion / source |
|---|---|---|
| A window that is too large (> 512 chunks, e.g. 32 MiB) | The chunk table arrives short and `scif_writeto` returns `EINVAL`; before the fix it was a `BUG` with the process in `D` state | `[meas]` the `DESC-BAD` / `DESC-INCONSISTENT` probes |
| A registration address aligned to 4 KiB instead of the **host** page | `scif_register` returns `EINVAL` outright | `[meas]` |
| Relying on "written, therefore readable" without synchronisation | The two sides disagree, with no error code at all | `[hdr]` "non-deterministic" |
| A single RMA longer than the verified length | Early versions wedged (the real ceiling is uncalibrated) | `[meas]` 26496 bytes is the safe value in use |
| A card-side sink without `-rdynamic` | `COI_DOES_NOT_EXIST(5)` | `[meas]` T5 |
| Large buffers through `COIBufferCreate` | `COI_OUT_OF_MEMORY(13)` | `[meas]` T5/T7 |
| Pinning beyond `RLIMIT_MEMLOCK` | `scif_pin_pages` fails (`ENOMEM`/`EPERM`) | `[src]` |
| Unregistering a window with RMA still in flight | Early versions wedged inside the unregistration path (`wchan=micscif_unregister_all_windows`) | `[meas]` |
| Treating `EISCONN` from `scif_connect` as a failure | The connection was up, yet the program exited with an error | `[meas]` |

## L.6 One-page summary

1. **Register**: host-page-aligned address, 4 KiB-multiple length, windows of 1 MiB or less, mind `RLIMIT_MEMLOCK`.
2. **Move**: ranges inside both windows, `SCIF_RMA_SYNC` or a fence, 26496 bytes per call, 64-byte-aligned offsets where possible.
3. **Synchronise**: every "written, now read" dependency needs an explicit synchronisation, never timing.
4. **Unregister**: confirm RMA completion, then unregister the window, then close the endpoint.
5. **COI**: small parameters in, card-side allocation; function names, `-rdynamic`, stack sizes and CPU masks as in L.3.
6. **Criteria**: any new pattern must pass T4 (data path) and T8 (bulk) with zero `DESC-*` and zero `kernel BUG` in dmesg.
