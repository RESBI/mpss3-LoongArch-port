# CHANGELOG — Host kernel module (08-mic-module)

 This file records **functional updates** to this package: the port of the `mic.ko` kernel module, the modprobe / udev configuration and card-interface auto-configuration shipped with it, and the page-size related driver patches. Each entry is one functional update. Entries run **oldest first — the newest update is at the bottom**.

---

## 2026-10-02 — Static audit: interface debt and architecture coupling

- **Added** A family-by-family review of kernel API drift, concluding that **86 lines** need changes: 64 purely mechanical replacements and 22 that require semantic judgement. Two areas (`host/vhost/` and `host/linvnet.c` / `vnet/`) were only reviewed at family level.
- **Added** The finding that only two places are genuinely tied to the host instruction set: F1 (host CPU model lookup) and F6 (three lines of x86-64 inline assembly in `micscif_ports.c`).
- **Added** Confirmation of the "host does exactly three things" model: write the kernel image into the card memory window, ring a doorbell register, receive one interrupt. No card-side code needs porting.

## 2026-10-04 — First successful `mic.ko` build on LoongArch, and a successful probe

- **Added** The module builds on real hardware: `make -k -j8 MIC_CARD_ARCH=k1om KERNWARNFLAGS=-Wno-error` produces a 23,254,080-byte LoongArch ELF whose `modinfo` is readable (`vermagic: 7.1.13-aosc-main-16k SMP preempt mod_unload LOONGARCH 64BIT`).
- **Added** The probe succeeds on real hardware: BAR0 receives **16 GiB** above the 4 GiB boundary, both DMA masks are 64-bit, the card reaches `online` **26 seconds** after the boot command is written, and host-to-card ICMP shows 0% loss (including 60 KB packets).
- **Fixed** A batch of kernel API drift (batch 1, 19 changes across 7 files): `MAX_ORDER` → `MAX_PAGE_ORDER`, `mm->mmap_sem` → `mmap_lock`, `pinned_vm` to `atomic64`, `pci_map_*` / `PCI_DMA_*` → `dma_*` and `DMA_*`, `PDE_DATA` → `pde_data`, proc files to `proc_ops`, `timespec` → `timespec64`, `const` on `bin_attribute` callbacks, and F7 (drop the `slow_virt_to_phys` branch, keep only `vmalloc_to_page`).
- **Changed** `host/vmcore.c` is excluded from the build: the file uses a `struct vmcore` that is defined nowhere in the tree (the source is itself incomplete) and it collides with a same-named kernel symbol prototype. A `vmcore_create()` stub returning `-EOPNOTSUPP` was added in `host/uos_download.c`; the only call site already branches on the return code.
- **Verified** Full-build error count went from 564 to 0; the real change set is 28 files, +318 / −309 lines, 185 hunks (the static audit had estimated 86 lines; the difference is documented in `docs/08-migration-roadmap.md`).

## 2026-10-05 — Shipped configuration and device permissions

- **Added** `mic.modules-load` writes `mic` into `/etc/modules-load.d/` so systemd loads the module early. The driver also declares `MODULE_DEVICE_TABLE(pci, …)`, so udev will `modprobe` it when the device appears.
- **Added** A udev rule relaxing `/dev/mic/scif` and `/dev/mic/ctrl` to mode 0666. The original rule's legacy `NAME=` form is ignored by systemd-udev, leaving the devices root-only — which had been misread as a porting defect that stopped ordinary users from working.
- **Added** Three pieces that configure the card interface automatically: `mic0-net.service`, `/usr/libexec/mpss/mic0-up.sh` and `90-mic0-net.rules`. The module creates the `mic0` interface (as soon as the virtio-net device appears); the address and MTU come from the `Network` line of `/etc/mpss/mic0.conf` via those three files.
- **Verified** An ordinary user can open the SCIF device; `mic0` comes up automatically with the expected address. Covered by `tests/t1_install.sh`.

## 2026-10-06 — Page-size and cross-side struct-layout fixes

- **Fixed** On a 16 KiB-page host, `scif_register` was rejected silently (user space registers in 4096-byte units while the driver validated alignment against the host page). Length is now validated in **protocol pages** (4096 bytes), while memory is still pinned rounded up to host pages. When the host page is 4096 the behaviour is identical to upstream.
- **Fixed** The `packed` attribute on `struct reg_range_t` placed the spinlock inside an embedded wait queue at a non-4-byte-aligned address; a LoongArch kernel terminates the process on unaligned atomic access (observed as `ALE`, faulting address = base + 150, where 150 is a compile-time member offset). The four wait queues (`allocwq`, `regwq`, `unregwq`, `gttmapwq`) became "pointer plus equally-sized padding": the lock sits on an aligned address while the struct layout stays byte-identical to the card side.
- **Fixed** The RMA copy path computed the per-page stride incorrectly, so a 26,496-byte bulk write delivered only the first page correctly (this is exactly the size of COI's process-creation command).
- **Fixed** Cross-side structs keep `packed` in all cases. Removing it on the host alone makes field offsets differ between the two sides, whose symptom is "the field read back is garbage" (an `ADEM` fault with the bad address inside the physical/uncached window).
- **Changed** The page count sent to the card is converted to peer pages ($P_h/P_c$) before transmission; the host keeps host-page semantics internally, and there is deliberately no global `PAGE_SHIFT` substitution, which would break offset alignment and mmap semantics.
- **Verified** The card verifies 4096 bytes byte for byte with 0 mismatches; the kernel log shows `nr_pages=4` (one 16 KiB host page = four 4 KiB peer pages); the `unaligned` counter drops to zero; the descriptor area's `magic` equals `SCIFEP_MAGIC`. Covered by `tests/t4_rma_dma.sh` (15 checks).
- **Note** The changed files live in `patches/`: `micscif_rma.c`, `micscif_rma.h`, `micscif_rma_dma.c`, `micscif_rma_list.c` and `micscif_nodeqp.c`. They are merged into the module source tree for the next release (see `patches/README.md` for placement).

---

## 2026-10-06 — Device permissions: the modern udev rule now ships with the package (correction)

- **Added** `55-mic-perms.rules` installs into `/usr/lib/udev/rules.d/` and relaxes `/dev/mic/scif` and `/dev/mic/ctrl` to 0666; immediately after `make install` the rule is applied with `udevadm trigger` plus a `chmod` on the existing nodes, with no need to reload the module.
- **Corrected** The earlier claim that "device permissions have been relaxed on the machine" held only for the test environment: `t1_install.sh` writes a modern rule to `/etc/udev/rules.d/50-udev-mic.rules`, whereas a plain `make install` installs **upstream's legacy rule** (`NAME="mic/%k"`, a form systemd-udev ignores, so the `MODE="0666"` in the same rule never reaches the node). After a reinstall or a reboot the devices therefore fall back to `crw------- root:root`, an ordinary user cannot open them, and it looks as if the driver is broken.
- **Verified** `ls -l /dev/mic/scif` shows mode `crw-rw-rw-`; test T1 opens the device as an ordinary user and uses that as its criterion.

## 2026-10-06 — Second batch of page-size fixes: a page count scaled twice, and chunk spans in the wrong unit

- **Fixed** **The host's own window had its page count scaled twice.** `micscif_map_window_pages()` packed the page count of *every* window into `dma_addr[]` in peer units (x4), while the reader `micscif_set_nr_pages()` converted back only for `RMA_WINDOW_PEER` and took a host window as it stood — so `num_pages[]` came out four times the true value. The chain: unmapping removed four times the pages, `mic_smpt[i].ref_count < 0` was reported, eleven seconds later `micscif_get_dma_addr` could not find the address and hit `BUG()`, and from then on a window unregistration never received the peer's answer (`wait_event_timeout` times out and executes `goto retry`, retrying forever while the peer is alive — visible in dmesg as a small DMA every 15 seconds). The process ends up in `D` state where `kill -9` does nothing and `rmmod` reports the module in use, so only a reboot clears it. The fix keeps the local array in host pages and applies the wire conversion only to **the copy sent to the peer**.
- **Fixed** **The two branches of `micscif_get_dma_addr()` disagreed about the page size.** With one page per chunk it picked 4 KiB or 16 KiB according to which side owns the window (correct all along); with multi-page chunks it always used the host `PAGE_SHIFT`. Combined with `micscif_set_nr_pages()` dividing peer counts by four, a card chunk of the common "one page = 4 KiB" shape became **0**, its span was computed as zero bytes, the range test could never succeed, and the scan fell through to `BUG_ON(1)`. This is exactly why T4 never tripped it: its windows all have one page per chunk and take the correct branch.
- **Added** Before the `BUG`, print the window geometry (`type`, `offset`, `nr_pages`, `nr_contig_chunks`), the first eight chunks' `num_pages`/span/address, and the page unit in use, so a unit error is obvious at a glance.
- **Measured** 30 seconds after T4, dmesg shows **0** accidents (it was guaranteed before the fix); T8 transfers 64 MiB / 256 MiB / 4 GiB in full with **bit-for-bit identical** checksums on both sides (4 GiB = `0x76ac888ab487e57b`); 4 GiB runs at **325.9 MB/s** wire-side and **98.8 MB/s** end-to-end; no kernel exceptions and no wedged processes. The full chain is in `docs/I-page-size-alignment.md` I.12.

## 2026-10-06 — Diagnostics moved behind a MAKE-time option; repository line endings normalised to LF

- **Changed** The 14 SCIF `pr_info` probes now go through `mic_dbg()` (new `include/mic/mic_debug.h`): they compile to `pr_debug` by default — completely silent unless dynamic debug is enabled — and to `pr_info`, straight into dmesg, with `make MIC_DEBUG=1`. The reason is that with all of them on, **a single 4 GiB transfer writes more than twenty thousand dmesg lines** (23694 lines accumulated in one boot, measured), which a release should not do; yet they remain genuinely useful when hunting a defect, hence a switch rather than deletion.
- **Added** The switch in `Kbuild`. It **must** be written as `ifneq ($(MIC_DEBUG),)`: `subdir-ccflags-$(MIC_DEBUG)` expands to `subdir-ccflags-1`, which Kbuild ignores — measured, the switch looked present while two builds produced byte-identical modules (23405184 bytes, same md5); with `ifneq` the two builds differ (23405184 versus 23367704 bytes, different md5). The wrapper `Makefile` forwards `MIC_DEBUG` and documents its use.
- **Fixed** **Repository line endings**: `micscif_nodeqp.c`, `include/mic/micscif_rma.h` and `dma/mic_dma_lib.c` had been committed as CRLF, so their diff against the working tree became a whole-file rewrite (2902 / 960 / 1792 lines) that buried the semantic changes. This is now split into two commits — `d8125be` "line endings normalised to LF, no logic change" and `11a16e8` "semantic changes" — and `.gitattributes` gained `* text=auto eol=lf` to prevent a relapse. After normalisation the real change to `mic_dma_lib.c` is **16 lines** (was shown as 1807), to `micscif_rma.h` **85 lines** (was 1032) and to `micscif_nodeqp.c` 17 lines (was 2911).
- **Completed** `patches/` grew from five files to the **full change set of ten**: `micscif_api.c`, `micscif_rma_dma.c`, `micscif_rma_list.c`, `micscif_nodeqp.h`, `mic_dma_lib.c`, `Kbuild` and `mic_debug.h` were added, and the README now lists every destination together with the build switch and the acceptance criteria.
- **Checked** Every file in the patch directory is **md5-identical** to the source being compiled on the Loongson machine, and the 132 files the local release tree shares with it match byte for byte.

## 2026-10-06 — A guard for the 12-bit page-count ceiling in `scif_register` (a guaranteed wedge becomes a clean failure)

- **Fixed** The wire window description stores "pages in this contiguous chunk" in only **twelve bits** (the `& 0xFFF` inside `RMA_SET_NR_PAGES`), so a chunk may hold at most 4095 pages. Beyond that `RMA_SET_NR_PAGES` **truncates silently**, the reader `micscif_set_nr_pages()` sees 0 and breaks out early, the chunk's span becomes zero bytes, `micscif_get_dma_addr()` scans every chunk without finding the address and hits `BUG_ON(1)` — followed by the "unregistration retries forever, process stuck in `D` state, reboot required" chain. Measured trigger: the card registering a whole gigabyte (262144 four-kibibyte pages) in one call in direct mode, crashing at 74% (scene in `docs/I-page-size-alignment.md` I.12.6).
- **Added** `__scif_register()` walks the chunks after a successful `scif_pin_pages()` and before `micscif_prep_remote_window()`: a chunk above 4095 pages releases the window and the pinned pages, returns `-EINVAL` and logs which chunk, how many pages, and the ceiling. The ceiling is about 63 MiB per chunk with 16 KiB host pages and about 15 MiB with 4 KiB card pages, and the criterion is a **single chunk's** page count, not the total length.
- **Effect** "Guaranteed wedge, reboot required" becomes "a clean registration failure", on both sides at once since the card builds the same source (the card's registration probe halves its span on failure and settles on something workable).
- **Measured** `make` passes with the guard in place (`mic.ko` 23416776 bytes); the failure path reuses the pin-failure cleanup sequence (`micscif_destroy_incomplete_window`, `dec_node_refcnt`, `scif_unpin_pages`, `__scif_release_mm`).
- **Pitfall recorded** The guard's first version treated `RMA_HUGE_NR_PAGE_MASK` (the shifted mask `0xFFF<<52`) as a page ceiling and cast it to `int`: the low 32 bits are zero, so `1 > 0` was always true and **every registration was refused** (T4 fell to 8/15, every T8 size failed, the client printed `scif_register: Invalid argument`) while the kernel logged nothing. It now uses `RMA_MAX_NR_PAGES_PER_CHUNK` (the shifted-out 4095, see `include/mic/micscif_rma.h`), with the boundary verified by a standalone program (1 page passes, 4096 pages rejected). The guard's message is ASCII as well — non-ASCII reaches dmesg escaped, so grepping for Chinese wording yields a false "zero rejections".

## 2026-10-06 — No more panic on an incomplete peer description: `BUG_ON` becomes `RMA_ERROR_CODE`

- **Fixed** `micscif_get_dma_addr()` used to `BUG_ON(1)` when it could not find an address (active in host builds), turning `RMA_ERROR_CODE` — an error path that already existed and is **read in ten places in `micscif_rma_dma.c`** — into dead code. The cost of that panic: a window unregistration never receives its answer, the process lands in `D` state, and only a reboot helps. It now prints the full scene and returns `RMA_ERROR_CODE`; the caller propagates the error and user-space `scif_writeto` fails normally.
- **Background (measured)** The card runs the **unpatched upstream module** (zero port probes in `card-modules/micscif.ko`, upstream timestamps on its sources) and has no 12-bit page guard. A 32 MiB card window arrives with its last 15 chunks carrying a page count of zero: the host probes measured `nr_pages=8192 nr_contig_chunks=527 loop_stopped_at=512` and a span total of 33492992 bytes against an expected 33554432 — a difference of **61440 = 15 x 4096**, exactly matching the 15 zero-count chunks.
- **Added probes (always on)** `MIC scif DESC-INCONSISTENT` (on read-back: chunk count, total pages, first zero chunk, first four packed values) and, at the failure site, the per-chunk span total against the bytes the window should hold plus the number of zero-count chunks — these distinguish "the peer's description is short" from "the lookup logic is wrong", and they are what settled this round.
- **Impact** The "no card-side change" conclusion still holds: the host can detect and refuse an incomplete description instead of being dragged down by it.

## See also

- Full investigation of page-size and struct layout: `docs/I-page-size-alignment.md`
- Family-by-family API drift list: `docs/06-kernel-api-drift.md`
- Migration roadmap and hardware measurements: `docs/08-migration-roadmap.md`
- Acceptance tests (T0 build, T1 install, T4 data path): `tests/`
