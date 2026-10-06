# patches/ — files to merge into the kernel module source tree at the next release

This directory is the **complete set of changes this port makes to the host-side module sources of upstream `mpss-modules-3.8.6`** (10 files). Place them as the table says and build as usual; the module comes out ported. **The card side needs no change at all** — every effect of these patches is on the host.

## Where each file goes

| File here | Destination (relative to the upstream module root) | What it changes |
|---|---|---|
| `Kbuild` | `Kbuild` | Build switch for the diagnostic prints: adds `-DMIC_SCIF_DEBUG_PRINT` when `MIC_DEBUG=1` (tested with `ifneq`, see below) |
| `mic_debug.h` | `include/mic/mic_debug.h` | **New file**: the `mic_dbg()` macro — `pr_debug` by default, `pr_info` when enabled |
| `micscif_rma.c` | `micscif/micscif_rma.c` | Protocol-page validation and host-page pinning, pointerised wait queues, **page counts kept in host pages**, RMA stride, diagnostic prints |
| `micscif_rma.h` | `include/mic/micscif_rma.h` | `struct reg_range_t` pointerised with equal-size padding, protocol-page macros, **chunk spans sized per side in both branches**, window geometry printed before the `BUG` |
| `micscif_rma_list.c` | `micscif/micscif_rma_list.c` | Window registration/unregistration path (with diagnostics) |
| `micscif_rma_dma.c` | `micscif/micscif_rma_dma.c` | RMA copy path converting between protocol and host pages (with diagnostics) |
| `micscif_nodeqp.c` | `micscif/micscif_nodeqp.c` | Node message path (wait-queue pointerisation follow-ups and diagnostics) |
| `micscif_nodeqp.h` | `include/mic/micscif_nodeqp.h` | Same as above |
| `micscif_api.c` | `micscif/micscif_api.c` | Protocol-page validation and rejection logs in `scif_register`/pinning |
| `mic_dma_lib.c` | `dma/mic_dma_lib.c` | DMA engine API migrations (`dma_mapping_error`/`pde_data`/`proc_ops`) and diagnostics |

## Build switch for the diagnostic prints

`mic_dbg()` compiles to `pr_debug` by default, so it is **completely silent unless dynamic debug is enabled** and never floods dmesg. When needed:

```bash
make MIC_DEBUG=1            # enable at build time (compiles to pr_info, goes straight to dmesg)
make install MIC_DEBUG=1    # build and install
```

Why the switch exists: these probes were the tool that located the page-size and window-bookkeeping defects, but with all of them on, **a single 4 GiB transfer writes more than twenty thousand lines** (23694 lines in one boot, measured). A release should not do that. In Kbuild the test must be `ifneq ($(MIC_DEBUG),)` — writing `subdir-ccflags-$(MIC_DEBUG)` expands to `subdir-ccflags-1`, which Kbuild ignores (measured: the switch appeared to be added while both builds produced byte-identical modules).

## How to verify

| Criterion | Command | Expected |
|---|---|---|
| Registration and data path | `tests/t4_rma_dma.sh` | 15/0: seven lengths byte-exact, a 26496-byte transfer with 0 mismatches |
| **The key criterion for the page-size fixes** | check dmesg **30 seconds after** T4 | **0 lines** matching `ref_count < 0`, `kernel BUG` or `Oops` (guaranteed before the fix, and the accident fires from a workqueue a few seconds later, invisible to T4's own check) |
| Bulk transfer and integrity | `tests/t8_bigxfer.sh` | 64 MiB / 256 MiB / 4 GiB all pass, bit-for-bit identical checksums on both sides, about 330 MB/s wire-side |
| The print switch | run a transfer after a default build | dmesg contains no `MIC scif ...` probes |
| **The 12-bit page-count ceiling** | try to register one contiguous run longer than 4095 pages (> 63 MiB with 16 KiB host pages) | `scif_register` returns `-EINVAL` and dmesg names the offending chunk, **instead of** the `micscif_get_dma_addr` `BUG` and the wedge that follows |

Details: `docs/I-page-size-alignment.md` sections I.11 (the first round: protocol pages, struct layout, RMA stride) and I.12 (the second round: a page count scaled twice and chunk spans in the wrong unit); field numbers in `docs/H-offload-field-notes.md` H.13.

## Dates

- 2026-10-05: the first round of page-size fixes (protocol-page validation, wait-queue pointerisation, RMA stride).
- 2026-10-06: the second round (page-count units, chunk-span page size); the payload completed to the full change set; diagnostics moved behind the `MIC_DEBUG` build switch.
