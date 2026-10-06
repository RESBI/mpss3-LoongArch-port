# Appendix M Portability and compatibility assessment (checked against code)

This appendix answers three questions, each with a code or measurement source rather than a guess. The object of the review is the **full change set** in `08-mic-module/patches/` (10 files).

| Question | Answer |
|---|---|
| 1 Do the host-side "non-4 KiB page support" changes break behavioural compatibility with upstream? | **No.** On a 4 KiB-page host every conversion **degenerates to identity**, so the wire format and the arithmetic are bit-identical to upstream. Three *intentional* behaviour differences remain, all in the safer direction (M.2) |
| 2 Can the current code compile, register and transfer on Linux kernels of **various page sizes**? | **Yes** (4 / 16 / 64 KiB). The key is that a chunk's wire page count is **always at most 512, independent of the host page size** (M.3.1); a modern kernel and the card's 512-chunk constraint are prerequisites |
| 3 Does adding LoongArch support break compilation on x86 or ARM? | **No.** The change set contains **no architecture-specific code at all**; LoongArch appears only in comments (M.4) |

---

## M.2 Question 1: behavioural compatibility with upstream

### M.2.1 Proof that everything degenerates to identity on a 4 KiB host

| Conversion point | Value when `PAGE_SIZE == 4096` | Compared with upstream |
|---|---|---|
| `SCIF_PEER_PAGE_FACTOR = PAGE_SIZE / SCIF_PROTO_PAGE_SIZE` | `4096 / 4096 = **1**` | — |
| `HOST_PAGES_TO_PEER(n) = n x factor` | `n` | **Bit-identical to upstream packing the page count directly** |
| `PEER_PAGES_TO_LOCAL(n)` | identity (and unused in this port) | same |
| `wpage` in branch 1 of `micscif_get_dma_addr` (`type == RMA_WINDOW_PEER ? 4096 : PAGE_SIZE`) | both are `4096` | **Identical to upstream's `PAGE_SHIFT` arithmetic** |
| `num_pages[i] x wpage` in branch 2 | `x 4096` | same |
| `RMA_SET_NR_PAGES(..., nr_pages)` in `micscif_map_window_pages` | the host page count | **This is upstream's own form** (the port's first-round fix restored it; see I.12.1) |
| The re-packing loop in `prep_remote_window` (`HOST_PAGES_TO_PEER(num_pages[i+_m])`) | `num_pages[i+_m]` | **Same result as upstream's single `memcpy_toio`** |

Conclusion: **on a 4 KiB-page host the port's page-size machinery is transparent** — every factor is 1, and the wire format and address arithmetic match upstream bit for bit.

### M.2.2 Page-size-independent changes and their verdicts

| Change | Effect on a 4 KiB host | Verdict |
|---|---|---|
| Pointerising `struct reg_range_t`'s wait queues with **equal-size padding** | Struct **size and field offsets unchanged**; one extra `scif_zalloc` and one dereference per window | **Compatible.** Cross-side safe: the two sides exchange only message payloads (`ep`/`vaddr`/`phys_addr`/window token) and their own address arrays — **never the whole window struct**; `regwq`/`unregwq`/`allocwq` are initialised and used only on each side's own windows (card sources: 4/2/3 occurrences in rma.c, 4/2/1 in nodeqp.c, all on local structures) |
| `BUG_ON(1)` → `return RMA_ERROR_CODE` in `micscif_get_dma_addr` | Cases where upstream panicked now return an error | **Intentional, safer.** Upstream never reached that return (it panicked first), so no "upstream could handle it, we cannot" case is lost |
| DMA-boundary validation `micscif_window_desc_valid()` | An incomplete description yields `-EINVAL` and no DMA is started | **Intentional, safer.** It refuses only inputs upstream would have panicked on; measured, T4 (15/0) and all three T8 sizes (12/12 each) show no false positives |
| The twelve-bit guard in `scif_register` | Refuses a chunk above 4095 pages | **Never fires in practice**: a chunk's wire page count is always at most 512 (M.3.1), so it is an identity operation on upstream and on this port alike |
| Diagnostic probes (`MIC scif ...`) and the `mic_dbg()` switch | `pr_debug` by default (silent); the `DESC-*` anomaly paths are always-on `printk` | **Compatible.** The probes are additions and replace no upstream output; the anomaly paths print only when something is genuinely wrong |
| Kernel API migrations (`dma_mapping_error`/`pde_data`/`proc_ops`/`mmap_lock`/`vm_flags_set`/flags-only `get_user_pages`) | Calls land on equivalent modern APIs | **Compatible (version axis)**, see M.3.3 |
| Non-module tidying (deploy scripts, permissions, build hygiene) | Unrelated to this question | — |

### M.2.3 The three intentional behaviour differences, stated plainly

1. **A bad description no longer panics**; it returns `EINVAL` (measured: the same input went from "`BUG` plus a process in `D` state plus a reboot" to "one diagnosable failure", with the machine still usable);
2. **Upstream's dead `RMA_ERROR_CODE` paths are now live**: the ten checks in `micscif_rma_dma.c` actually take effect (on a 4 KiB host too — a behaviour difference, in the safer direction);
3. **`scif_register` gained one rejection condition** (the twelve-bit guard) which never fires under the current chunking rule: pure defence.

---

## M.3 Question 2: compiling and running across page sizes

### M.3.1 The key invariant: a chunk's wire page count is independent of the host page size

The chunk table is built in one place (`micscif_detect_large_page()` in `micscif_rma.h`) and knows only two shapes:

- an ordinary page → **one page**;
- a huge page → from the current position **to the next 2 MiB boundary** (`ALIGN(addr + 1, PMD_SIZE)`).

So the page count sent to the peer (in 4 KiB wire units) is:

```text
ordinary chunk: 1 x (PAGE_SIZE / 4096)                    = PAGE_SIZE/4096
huge-page chunk: (2 MiB / PAGE_SIZE) x (PAGE_SIZE / 4096) = 2 MiB / 4096 = 512   <- independent of PAGE_SIZE
```

| Host page size | `SCIF_PEER_PAGE_FACTOR` | Max wire pages per chunk | Does it reach the twelve-bit ceiling (4095)? |
|---|---|---|---|
| 4 KiB | 1 | 512 | No |
| 16 KiB (this machine) | 4 | **512** | No |
| 64 KiB | 16 | **512** | No |

**The twelve-bit field therefore never becomes a limit at any page size**, which is exactly why that guard is purely defensive.

### M.3.2 Item-by-item check across three page sizes

| Check | 4 KiB | 16 KiB (measured) | 64 KiB |
|---|---|---|---|
| `addr` alignment | host page (= 4 KiB) | host page (16 KiB); a 4 KiB-aligned buffer was rejected with `EINVAL` as measured | host page (64 KiB) |
| `len` alignment | 4 KiB (protocol page, equivalent) | 4 KiB (relaxed from upstream's "host page"; see M.2.1) | 4 KiB |
| Pinned length | `ALIGN(len, PAGE_SIZE)` | same | same |
| Wire pages per chunk | ≤ 512 | ≤ 512 | ≤ 512 |
| **Host-side** window chunk ceiling | The `prep_remote_window` loop covers every chunk (no 512 limit) | same | same |
| **Card-side** window chunk ceiling | **≤ 512 chunks** (the card's code writes only the first page of the table; measured, see Appendix K) | same | same |
| Recommended window size | ≤ 1 MiB (256 chunks on a 4 KiB host) | ≤ 1 MiB (64 chunks) | ≤ 1 MiB (16 chunks) |

Note that the card's 512-chunk limit is **independent of the host page size** (it follows from `NR_PHYS_ADDR_IN_PAGE` on the card, whose pages are 4 KiB). On any host, therefore, a large window registered *by the card* fails, while a large window registered *by the host* is not subject to that limit in the code (today's T8 client sizes both windows identically, so this has not been measured on its own; it is listed as an open item).

### M.3.3 The compilation axis raises the **minimum kernel version**, not an architecture requirement

| Migration point | Kernel needed | Note |
|---|---|---|
| `get_user_pages()` taking gup flags only | ≥ 4.9 (argument-free by 5.x) | older kernels need a compatibility shim |
| `proc_ops`, `pde_data()` | ≥ 5.6 | `file_operations` → `proc_ops` |
| `mmap_lock` (instead of `mmap_sem`) | ≥ 5.8 | |
| `vm_flags_set()` | ≥ 6.3 | older kernels assign `vma->vm_flags` |
| `dma_mapping_error()` | long-standing | architecture-independent |

**Conclusion**: the ported module targets **modern kernels** (verified on 7.1.13 here) and will compile on any of the architectures above; going back to the 3.x/4.x kernels MPSS originally targeted would need version guards around those few call sites (small, not yet done).

### M.3.4 Short answer

**Yes** — it compiles, registers and transfers on modern Linux with 4, 16 or 64 KiB pages, subject to three conditions: (i) the kernel version satisfies M.3.3; (ii) windows registered by the card stay at or below 512 chunks (the Appendix K invariant); (iii) transfers are explicitly synchronised (Appendix L, L.1).

---

## M.4 Question 3: does this break compilation on other architectures?

### M.4.1 Architecture-specific code in the change set: **none**

A full scan of the ten changed files finds **not one** occurrence of `__loongarch__`, `CONFIG_LOONGARCH`, `__aarch64__`, `__arm__`, `__x86_64__` or `CONFIG_X86`. The word "LoongArch" appears in exactly **four comments**, explaining *why* the wait queues had to be pointerised (the structure's unaligned atomic accesses cannot be emulated in LoongArch kernel mode).

### M.4.2 The nature of the changes (harmless or safer on other architectures)

| Change | Effect elsewhere |
|---|---|
| Wait-queue pointerisation with equal-size padding | Struct size and offsets unchanged; it fixes a cross-architecture hazard (a wait queue embedded in a packed struct) and is equally safer on x86 without changing behaviour |
| Page-size arithmetic via factor macros | Identity on 4 KiB (M.2.1); satisfies the invariant on 64 KiB (M.3.1) |
| Kernel API migrations | Architecture-independent, version-dependent only |
| `BUG_ON` → error return, DMA-boundary validation | The same "safer" behaviour difference on every architecture |
| Diagnostic probes and the `MIC_DEBUG` switch | Pure additions, silent by default |

### M.4.3 Conclusion

**Compilation on x86 and ARM is not broken**: no architecture conditionals, no architecture headers and no architecture-specific inline assembly were introduced. The only threshold is the kernel version (M.3.3). To *demonstrate* compilation on x86 or ARM, the direct route is one `make -C <kernel> M=08-mic-module` against that kernel tree — not done here, because only a LoongArch tree and the card's k1om tree are at hand.

---

## M.5 Open items surfaced by this review

| Item | Why it matters | How to close it |
|---|---|---|
| **Is a large host-side window really free of the 512-chunk limit?** | The code reads that way (the `prep_remote_window` loop covers every chunk), but today's T8 client sizes both windows identically, so it cannot be isolated | Add a "the two windows differ" knob to the T8 client (large host window, 1 MiB card window) and run 8/16/32 MiB |
| x86 and ARM compilation | This report only settles it statically | Compile once against each kernel tree |
| End-to-end measurement on a 64 KiB-page kernel | The arithmetic holds, but it has not been run | Run T4 and T8 on a 64 KiB-page LoongArch or ARM64 machine |
