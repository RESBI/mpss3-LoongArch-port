# CHANGELOG — Acceptance test suite (tests/)

 This file records **functional updates** to this sub-project: stage tests T0–T8, the convenience entry points, the pre-check and permission probes, and the site-configuration mechanism. Each entry is one functional update. Entries run **oldest first — the newest update is at the bottom**.

---

## 2026-10-06 — Suite created and green on real hardware

- **Added** Eight stage scripts: T0 build, T1 install and start-up, T2 card access and deployment, T3 SCIF message channel, T4 DMA/RMA seven-length sweep, T5 COI end to end, T6 higher-pressure offload, T7 heavy compute with large data. Plus `precheck.sh` (compile only, never touches the card, so that "compile problems" and "run problems" can be told apart) and `perm_probe.sh` (proves the "usable without root" property).
- **Added** `lib/common.sh`: path resolution, PASS/FAIL/SKIP accounting and summary, card-side deployment (with md5 verification), waiting for the card to reach `online`, waiting for `coi_daemon`. Logs go into a per-run directory so stale root-owned artefacts cannot cause false failures.
- **Verified** All green on real hardware: T2 8/0/1, T3 11/0, T4 15/0, T5 17/0, T6 12/0, T7 9/0 — **72 passed, 0 failed, 1 skipped**. Key numbers: T5 uses 240 threads with a relative error of −3.35e-16; T7's three problem sizes match the host reference bit for bit, at 0.62 / 2.10 / 2.68 GFLOPS; T6's three rounds take 0.137 / 0.369 / 0.294 s with the card-side daemon alive throughout.

## 2026-10-06 — Fixes and tightening

- **Fixed** Script naming and wiring unified as `t<N>_<description>.sh`. In `run_tests.sh`, T7 had been appended after the closing banner and was moved back after T6; `run_all.sh` called `build.sh` / `install.sh`, which do not exist, and now calls `t0_build.sh` / `t1_install.sh`.
- **Fixed** In `t7_nbody.sh` the k1om objdump path was one directory level short, and a missing tool fell back to the host objdump — which made the instruction count always zero, a **false pass**. The path is now derived from the SDK location, and a missing tool is reported as skipped.
- **Changed** T7's "no masked compressed stores in the disassembly" check was demoted from an **assertion** to **informational**: the `-O0` binary that runs fine contains 12 of them, while the crashing `-O1` / `-O2` builds contain only 11 each — the count is not the criterion. The criterion is whether all three problem sizes run.
- **Fixed** `perm_probe.sh` opened the SCIF character device with Python's `r+b` (the device is not seekable, so this always failed); it now uses a shell redirection, matching the check in the shared library.
- **Changed** The card-side dependency directory is now an independent configuration item, with a clear hint when it is missing (the host-side COI validates the ELF machine type of every dependency).

## 2026-10-06 — Decoupled from the environment, shipped with the release tree

- **Changed** The test project is delivered as a top-level directory of the release tree (`tests/`, alongside `docs/`) and thus ships with the release. Because it lives inside the tree it tests, that tree is the default target.
- **Added** **Environment decoupling**: `tests/config.example.sh` is the site-configuration template; after copying it to `tests/config.sh` you fill in your own values. The shared library resolves every path in the order "environment variable → `tests/config.sh` → autodetection", and stage scripts contain no machine-specific IP, user name or absolute path.
- **Added** Autodetection covers: card address (readable from `micip=` in MPSS's `/etc/mpss/mic0.conf`), release tree (recognised as a *complete* tree by the `PKGS` list in the top-level `Makefile` plus per-package `Makefile.mpss`), COI headers and libraries, the user-space SCIF library, the k1om SDK and sysroot, the k1om compiler (PATH → SDK → in-project wrapper script), the card-side dependency directory, the k1om objdump, kernel sources and the module install directory.
- **Changed** When something cannot be detected the suite does not guess and does not run with a wrong value: the stage is reported as `SKIP` together with the variable to set (for example, "the dependency directory has no `libgomp.so`: put the self-built k1om `libgomp` into the directory `SINK_LIBS` points at"). Skipped items are listed separately in the summary.
- **Changed** Card-side compilation goes through the `k1om_cc` / `k1om_cxx` wrapper functions (the shared library decides whether to add `-B` and `--sysroot`); the install prefix is the configurable `PREFIX` (default `/usr`), and installation checks no longer hard-code `/usr/...`.
- **Added** `config.sh` and `logs/` are listed in `tests/.gitignore`.
- **Verified** On real hardware, filling in a single line of configuration (`SINK_LIBS`) was enough for autodetection to do the rest: T2 8 passed / 0 failed / 1 skipped, T5 17 / 0, T7 9 / 0.

---

## 2026-10-06 — Bilingual README

- **Added** `tests/README_CN.md` (Chinese) and `tests/README.md` (English) ship as a pair with identical content; both cover the environment-decoupled configuration mechanism, the autodetection coverage, the stage criteria and how to read the results.

## 2026-10-06 — No more dying on an unopenable device (fix)

- **Fixed** `scif_ok()` used to read `if exec 3<>"$scif_dev" 2>/dev/null; then`. `exec` is a POSIX special builtin, and **a failed redirection makes the whole shell exit** — so with wrong device permissions the script died on the spot with a bare error (`common.sh: line 307: /dev/mic/scif: Permission denied`) instead of reaching the "skip and explain" path. The open is now attempted inside a subshell, where failure merely returns 1.
- **Added** `scif_hint()`: when the device cannot be opened it prints two actionable fixes (`sudo bash tests/t1_install.sh`, or a temporary `chmod`). It is wired into the pre-flight status block of `run_tests.sh` and into stages T3–T7.
- **Fixed** The `need_scif` guard **was never defined** (T3/T4 kept hitting "command not found → treat as missing library → skip"). It is now defined, so those stages skip only when the user-space SCIF library really is missing.
- **Fixed** T1 no longer overwrites upstream's `50-udev-mic.rules`; it writes `55-mic-perms.rules` instead (which takes effect later in filename order) and skips if it is already present. One stale hint was corrected as well (`install.sh` → `t1_install.sh`).

## 2026-10-06 — New stage T8: bulk transfer (4 GiB, both-side checksums, bandwidth)

- **Added** `t8_bigxfer.sh` with `src/bigxfer_srv.c` (card side) and `src/bigxfer_cli.c` (host side): **4 GiB** by default, pushed to the card. The card mallocs the whole region and touches every page first (so a failed large allocation shows up immediately), then probes the **largest span it can register** and exposes it to the host. The host fills its source with a deterministic PRNG, checksums it, pushes it in chunks with `scif_writeto`, and finally the two checksums are compared bit for bit while the transfer time yields a bandwidth figure. If the card cannot register the whole region it falls back to a sliding window (one sync per chunk, the card moves the data itself), and the log states the mode and the span, so a partial transfer can never be reported as a full one.
- **Added** Twelve criteria: card-side large malloc succeeded, registration succeeded, the transfer covered the full size, both checksums match, a bandwidth was measured, the card finished cleanly, and no unaligned or kernel exceptions. Sizes come from `TEST_BIG` / `TEST_BIG_WINDOW`; `--quick` drops to 256 MiB.
- **Measured (key finding)** A 4 GiB card-side `malloc` and a whole-region registration both work (256 MiB registered in 0.03 s, direct mode). But **a single 64 MiB `scif_writeto` fails and wedges the process**: the host ends up in `micscif_unregister_all_windows` (`D` state) and the same-named card-side process cannot be killed with `kill -9` either. Afterwards the host's whole RMA path is out of action (T4 starts failing too) and only `sudo rmmod mic && sudo modprobe mic` plus a card reboot clears it. A single RMA is therefore hard-clamped to 1 MiB.
- **Added** Host-side buffers are aligned to the **actual page size** (`sysconf(_SC_PAGESIZE)`, 16 KiB here): 4 KiB alignment makes `scif_register` answer `EINVAL` outright.
- **Changed** `deploy_card_file` removes the target file before copying: with the same-named program still running on the card the copy fails with `Text file busy` (ETXTBSY), so T8 also clears the process and retries once.
- **Changed** The client's output is line-buffered with per-chunk progress, and the script no longer waits through `timeout` (a process in `D` state cannot receive signals and would hang the stage); it runs it in the background and polls with a deadline.

## 2026-10-06 — T8 green: 4 GiB bit for bit on both sides, and two driver defects found by it

- **Added** T8 is verified on real hardware: all three sizes pass, 12 criteria each. 64 MiB: 359.0 MB/s wire-side, 103.8 MB/s end-to-end; 256 MiB: 338.6 / 101.2 MB/s; **4096 MiB: 325.9 / 98.8 MB/s with bit-for-bit identical checksums on both sides (`0x76ac888ab487e57b`)**; no kernel exceptions and no wedged processes throughout.
- **Added** **Per-window verification**: the host sends a checksum for every 1 MiB window, the card compares it on arrival before copying, so any error can be reported as "which window, which offset, which two values"; the whole buffer is compared at the end as well. This is the real handle on the requirement "a bulk transfer must not corrupt data".
- **Fixed** The card's **staging area overlapped the destination of window 0**: with the window registered at the start of the big buffer, every later window's inbound data overwrote what window 0 had just placed — the symptom being "the first MiB is wrong, the rest is right". The card now registers a separate staging buffer and copies every window into the big buffer.
- **Fixed** The card's **whole-buffer checksum length was clamped wrongly**: `if (done == 0 || done > span) done = span;` pulled the accumulated length back to the window size in windowed mode, so only the first window was checksummed (the host computed the full amount and the card 1 MiB, hence the inevitable mismatch). The ceiling is now the requested total.
- **Changed** The card-side test program is compiled with `-O2 -fno-tree-vectorize` (vectorisation off to avoid the known card-side crash; the checksum loop is the hot spot, taking 33.4 s for 4 GiB at `-O0` against 18.3 s this way). Override with `TEST_BIG_OPT`.
- **Changed** Both bandwidths are reported: **wire-side** (only the `scif_writeto` calls), which is the real PCIe throughput, and **end-to-end**, which also includes the card's per-window verification and copy. The criterion uses the end-to-end figure while the wire-side number is printed to locate the bottleneck.
- **Corrected** The earlier conclusion "a single RMA must not exceed 26496 bytes" is **retracted**: it was measured against an already-broken RMA path (see the same-day `tests/CHANGELOG` entry and `docs/I` I.12). The real limit is not yet calibrated, so T8 keeps 26496 bytes, the value T4 had verified, as a conservative default.
- **Added** Per-piece progress on the client (first 16 pieces plus every 128th) and per-window progress on the card (first four plus every 256th), together with the `D`-state check (`stat`/`wchan`), so a stall can be located straight from the logs.

## 2026-10-06 (addendum) — Corrected bandwidth figures, a quiet-build re-measurement, and the 12-bit ceiling

- **Corrected** The earlier "4 GiB at 325.9 MB/s wire-side" was a **measurement artefact**: the driver's diagnostics (`pr_info`) sat in the RMA copy path, so every 26496-byte piece wrote dozens of log lines and logging dominated the elapsed time. With the prints behind the `MIC_DEBUG` switch, the same hardware on a quiet build gives **118.3 / 123.7 / 119.3 MB/s end-to-end and 2978.8 / 3835.3 / 3478.1 MB/s inside `scif_writeto`**, checksums still bit-for-bit identical at all three sizes.
- **Corrected** The link is not x1 but **PCIe Gen2 x8** (`lspci`: `LnkCap x16`, `LnkSta Width x8 (downgraded)`, about 4 GB/s theoretical); 3478 MB/s sits in its practical range, and the earlier "matches an x1 link" claim is withdrawn.
- **Consequence** Any timing figure that logging could affect must be re-measured on a quiet build. T8's criterion uses the end-to-end number (bounded by the card's per-window verification and copy); the `scif_writeto` figure only locates the bottleneck.
- **Added** The pitfall list grew to six: a single contiguous chunk may not exceed 4095 pages (the twelve-bit wire field; about 63 MiB with 16 KiB host pages, 15 MiB with 4 KiB card pages) and `scif_register` now answers `-EINVAL` instead (`docs/I` I.12.6).

## See also

- Suite description and usage: `tests/README.md`
- Acceptance results and per-check criteria: `docs/J-acceptance-tests.md`
- Changes to the objects under test: each sub-project's `CHANGELOG.md`
