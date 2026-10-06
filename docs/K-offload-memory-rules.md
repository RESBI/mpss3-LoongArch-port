# Appendix K User-side constraints for offload memory movement (measured)

This appendix covers one thing only: **how to slice buffers and size windows when offloading to a KNC card**. Every rule has a measurement behind it rather than a guess, and the evidence is cited with each rule.

## K.1 First, one invariant: a window's chunk count must stay at or below 512

When SCIF registers a window the driver splits it into a series of *contiguous chunks* (`struct scif_pinned_pages.num_pages[]`), one page count per chunk, and that chunk table has to reach the peer. Measured: **the table arriving from the card carries only its first 512 entries**; everything past that stays zero (the raw value is `0x0`, not a bare address), and 512 is exactly `NR_PHYS_ADDR_IN_PAGE` on the card, whose pages are 4 KiB (one page holds 512 eight-byte addresses).

```text
RAW-SCAN:      type=2 nr_pages=8192 chunks=527 raw_sum=8177 raw_zeros=15
RAW-HEAD[0]:   raw=0x100003bc830000  pages=1  addr=0x3bc830000
RAW-TAIL[511]: raw=0x100003ba820000  pages=1  addr=0x3ba820000
RAW-TAIL[512]: raw=0x0               pages=0  addr=0x0        <- zero from here on
```

The consequence: as far as the host is concerned those trailing chunks do not exist, so a write that reaches them cannot resolve an address. **Before the fix** that tripped `BUG_ON` and wedged the whole SCIF path (process in `D` state, reboot required); **after the fix** the host refuses the copy at the DMA boundary, prints `DESC-BAD ... refusing the copy`, and user space gets `EINVAL` from `scif_writeto`.

Note that the chunk count is **not** the size divided by the page size: a normal page is its own chunk, while a huge page contributes a chunk running to the next 2 MiB boundary. A 32 MiB card window therefore has **527 chunks** (512 single-page ones plus 15 of 2 MiB), not 8192. **The criterion is the chunk count, not the size.**

## K.2 Four rules to code against

| # | Rule | Why | Evidence |
|---|---|---|---|
| 1 | **Keep registered windows small**: 1 MiB or less per window (64 chunks with 16 KiB host pages, 256 with 4 KiB card pages) | Keeps the chunk count well away from 512; T8 transfers 64 MiB / 256 MiB / 4 GiB at 12/12 with a 1 MiB window | Measured: three sizes green at a 1 MiB window; a 32 MiB window (527 chunks) always fails |
| 2 | **Do not "register the whole array and move it in one go"**: slice by window, loop `scif_writeto` on the host and loop receiving on the card | Decouples window size from total volume; the total can far exceed the window | Measured: 4 GiB total through a 1 MiB window, 12/12, bit-for-bit identical checksums |
| 3 | **A single `scif_writeto` may go up to 1 MiB** (matching this list's window ceiling) | Measured 2026-10-07 as a ladder: 26496 B, then 64/128/256/512 KiB and 1 MiB, all with matching checksums; 26496 bytes was only a historical conservative value | `tests/extra/x03_rma_ladder.sh`; `MAX_DMA_XFER_SIZE` (512 KiB on the host) is a descriptor-splitting unit, not a safety boundary |
| 4 | **Pinning is subject to `RLIMIT_MEMLOCK`**: for large buffers either register in pieces or raise the limit (`ulimit -l`) | `__scif_check_inc_pinned_vm()` refuses a pin beyond the limit | Source: `micscif_api.c`; failures surface as `-ENOMEM`/`EPERM` |

## K.3 How to check before running (three options, cheapest first)

1. **Read dmesg** (these probes are always on, even in a default build):
   - `MIC scif DESC-INCONSISTENT ...` — the peer's description is incomplete and this transfer will fail;
   - `MIC scif DESC-BAD (...) ... refusing the copy` — the host already refused, user space will see `EINVAL`;
   - `kernel BUG at` or a process in `D` state — the module predates these fixes and must be updated.
2. **Read the window geometry**: a diagnostic build (`make MIC_DEBUG=1`) prints `MIC scif WND: ... nr_pages=... nr_contig_chunks=...`, so the chunk count is visible. Recommended when calibrating a new window size.
3. **Run the calibration**: `TEST_BIG=<total> TEST_BIG_WINDOW=<window> bash tests/t8_bigxfer.sh`, then confirm with the checksums on both sides and the dmesg criteria.

## K.4 Two things easily misread

- **The "12-bit page count field" is not the limit here.** A chunk's page count does occupy only twelve bits (4095 pages), but the driver's chunking rule makes a normal page its own chunk and lets a huge page run to the next 2 MiB boundary, so a chunk holds at most 512 pages with 4 KiB pages — eight times below 4095, and the field is never reached. The 512 in K.1 is a limit on the **number of table entries**, unrelated to that field. (The field is still used defensively: `scif_register` refuses a chunk above 4095 pages outright.)
- **This is not a LoongArch-only problem.** The card's code is identical on x86 and so is the threshold; MPSS's own tools never registered windows this large, which is why it stayed hidden. What the host page size does affect is a *different* family of problems (converting between the 4 KiB protocol page and the 16 KiB host page; see [Appendix I](I-page-size-alignment.md)).

## K.5 Current state and what remains

| Item | Status |
|---|---|
| 1 MiB window (the recommended usage) | ✓ Verified: 64 MiB / 256 MiB / 4 GiB all 12/12, 4 GiB bit-for-bit identical checksums (`0x76ac888ab487e57b`), about 3.3 GB/s wire-side (Gen2 x8) and about 119 MB/s end-to-end |
| Large windows (> 512 chunks) | ✗ The card's chunk table is incomplete; the host now refuses cleanly with a full picture instead of panicking or wedging |
| Root fix | Requires updating the **card-side module** (the second page of the chunk-table transfer); the host cannot reconstruct the missing entries. The card currently runs an unpatched upstream build |
| User side | Follow the four rules in K.2; nothing about the card's implementation needs to be known |
