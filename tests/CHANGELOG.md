# CHANGELOG — Acceptance test suite (tests/)

 This file records **functional updates** to this sub-project: stage tests T0–T9, the convenience entry points, the pre-check and permission probes, and the site-configuration mechanism. Each entry is one functional update. Entries run **oldest first — the newest update is at the bottom**.

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

## 2026-10-08 — T9-VEC: GEMM from 0.33 to 242.5 GFLOPS, and why `-O2` was never really the problem

- **Added** `t9_gemm_vec.sh` with `src/gemm_vec_sink.cpp` + `src/gemm_vec_host.cpp`: a GEMM whose card-side kernel is written with explicit IMCI (`_mm512_*`) intrinsics and built with **`-O2 -mavx512f`**. Measured at N=2048 on the 7120P with 244 threads: **FP32 147–261 GFLOPS (best-of-3 peak 260.9), FP64 120–138 GFLOPS (peak 133.9)** — roughly 6–11 % of the card's 2416 / 1208 GFLOPS peaks, against the T9 scalar baseline's 0.33 GFLOPS and 38× the previous best (`-O2 -march=knc`, which did not vectorise at all). **The run-to-run spread matters**: FP32 varies by up to 1.8× between sessions, so the best-of-3 figure is the one to quote and a single fast run is not evidence of a stable rate.
- **Added** Two gates in that script beyond "it ran": the disassembly must contain `vfmadd` (> 0) **and zero `xmm`/`ymm` operands** (KNC has no 128-/256-bit registers, so any xmm in the output means the card will fault), and the card-side full-matrix sum is compared against the host's `O(N²)` reference, which for N≤512 the host additionally cross-checks against a full `O(N³)` computation.
- **Corrected** The suite's long-standing conclusion "card-side programs must be `-O0`" is **too broad**. The real cause is that `-O2` at `-march=knc` emits *scalar floating-point* code (`vaddss`/`vcvtsi2ss`) and `cmov`, neither of which KNC has; the k1om assembler rejects some outright, and the masked compress stores it does accept fault at run time. `-O2 -mavx512f` is fine — and fast — as long as every floating-point operation in the file sits inside a 512-bit intrinsic. T7 keeps `-O0` because its source is scalar; that is now a property of the source, not of the toolchain.
- **Added** §VIII of `tests/README.md`: the four hard constraints (no auto-vectorisation at any `-O` level; `-mavx512f` is required for intrinsics but retargets scalar FP to unavailable xmm forms; the ISA gaps — no `vcvtdq2ps`, no 64-bit integer vector ops, no unaligned vector access, no `cmov`; and the corollary that **`? :` is unusable** in such a file because it becomes `cmova`), the recipe that works, and the measured optimisation ladder.
- **Added** The optimisation ladder with measured deltas at N=2048: implicit-vs-explicit vectorisation 6.39 → 57.4; untimed warm-up pass plus `schedule(static)` instead of `dynamic,1` → 104.1 (the first parallel region costs ≈0.15 s, and `dynamic,1` took 16384 shared-counter atomics across 244 threads); packing B into `[j-strip][k][NR]` → 113.9; **padding A's row stride by 64 B → 149.2**; j-blocking plus tuned register-tile sizes → 242.5.
- **Added** The padded-stride finding, which is the single biggest win and is easy to miss: with N=2048 the A row stride is exactly 8192 = 2¹³ bytes while a 32 KB / 8-way / 64 B L1 indexes sets with address bits 6–11, so **all 16 A rows land in the same L1 set** and 8 ways cannot hold them — every access conflicted. Adding 64 B per row tripled per-core throughput (single core, 4 hardware threads: 5.40 → 16.34 GFLOPS).
- **Added** A tuning table showing that a **larger register tile is worse** on this part: MR32=8 / MR64=4 beats MR32=16 / MR64=8 on both precisions (MR64 8→4 alone nearly doubles FP64, 70 → 134 GFLOPS), because a bigger accumulator set spills. Defaults are JSB32=32, MR32=8, JSB64=16, MR64=4, all `-D`-overridable, with `jsb` chosen at run time so smaller N still works.
- **Changed** `run_tests.sh` now also runs T9 and T9-VEC.

## 2026-10-08 (addendum) — T7 N-body: 2.7 → 52.4 GFLOPS, and why the card has no FP64 square root

- **Added** A vectorised card-side kernel for T7 (`src/nbody_vec.cpp`, built `-O2 -mavx512f`) with the scalar remainder left at `-O0` in `src/nbody_sink.cpp`, linked into one sink. Five runs per tier on the 7120P, min / median / max: tier 1 (N=1024) 0.52 → 0.79 / **2.01** / 2.84 GFLOPS; tier 2 (N=8192) 2.12 → 20.3 / **29.0** / 30.7; tier 3 (N=16384) 2.70 → 41.3 / **51.6** / 54.5 (best single observation 56.2). **15×–21×**, median ≈19×; tier 3's median is 4.3 % of the ≈1208 GFLOPS FP64 peak (from 0.22 %). All 9 checks pass at every tier, and the tier-1 checksum stays bit-exact.
- **Added** `t7_nbody.sh` now compiles both units, and `T7_OPT` overrides the scalar unit's level for comparison. The per-tier logs keep `nbody_vec.o` and `nbody_sink.o` so the two optimisation levels can be checked independently.
- **Correctness preserved exactly**: the tier-1 checksum still matches the host reference **bit for bit (0.000e+00)** after the reciprocal-square-root was replaced by a completely different algorithm. That is a consequence of vectorising **over `i`** with `j` broadcast — each body's `j` accumulation keeps the reference's order and roundings, so only `1/sqrt` itself differs, and that is accurate to ~1 ulp. Energy conservation is unchanged at 1.957e-04 / 8.468e-05 / 4.016e-05 (< 1e-3).
- **Found** KNC has **no FP64 square root, divide, reciprocal or reciprocal-square-root** — `VSQRTPD`/`VDIVPD`/`VRSQRTPD`/`VRCPPD` are absent from the ISA manual's instruction index, and every intrinsic that would reach them is rejected by the k1om assembler (`vsqrtpd`, `vdivpd`, `vrsqrt28pd`, `vrsqrt14pd`, and GCC's AVX-512 spellings `vcvtpd2dq`/`vrndscalepd`). `sqrt()` and `/` therefore call libm, at roughly **285 cycles per particle pair** — the whole of the old 2.7 GFLOPS.
- **Found** KNC's `VCMPPD` uses only `IMM8[2:0]`; the manual's table has eq/lt/le/unord/neq/nlt/nle/ord and says to swap operands and use `LE` for `ge`. GCC encodes `_CMP_GE_OS` as immediate 13 (the AVX-512 form) and the card reports it as an invalid opcode. Writing `_mm512_cmp_pd_mask(threshold, k, _CMP_LE_OS)` fixes it. The assembler catches instructions KNC lacks outright, but **not** instructions whose immediate encoding differs — a general hazard for this target.
- **Found** `calloc`'s 16-byte alignment is not enough for `vmovapd`: the first vectorised build died with a general-protection fault. Arrays passed to the vector kernel now use `posix_memalign(..., 64)`. KNC has no unaligned vector access at all, so this is a fault, not a slowdown.
- **Explained** The `vpackstorelpd` crash that has pinned T7 to `-O0` since it was written. The working `-O0` build has 12 of them, all `%rbp`-relative, and runs; the crashing `-O2` build has 11, of which one is `0x10(%r12)` — a single-double store in KNC's compressed `disp8*N` form, which the manual flags as element-granular for `VPACKSTORE`. So the count really is not the criterion, as this file has said for months; the addressing form is. The vector kernel emits **zero** of them, which is why it can be built `-O2`.
- **Added** The measurement localising the remaining 95 %: the inner loop is 41 instructions per 8 pairs and takes ≈230 cycles (5.6 cycles/instruction). `-O2`/`-O3`/`-O3 -funroll-loops` are flat — 52.02/51.34/48.63 GFLOPS — so it is not codegen; it is an in-order core paying a ~15–20-deep dependent chain serially. Recorded next step: unroll over `i` to give the scheduler two independent chains, doubling ILP without changing the summation order.
- **See** [N9](../docs/N9-t7-nbody-overview.md)–[N12](../docs/N12-t7-nbody-headroom.md) for the full write-up.

## 2026-10-08 (second addendum) — T7 round two: i-unroll, prefetch, and the guard that would not assemble

- **Added** Two `i` blocks per `j` iteration in `nbody_accel`, and `vprefetch0` on the four load streams. Tier 3 best-of-N: 52.4 → 54.5 (unroll) → **56.2** (unroll + prefetch-16). Small, and reported as small.
- **Fixed** A regression the unroll introduced: halving the parallel work items left only 64 groups for 240 threads at N=1024, so tier 1 fell from 2.86 to 0.72–0.97 GFLOPS. The kernel now falls back to one block per iteration when the items cannot fill the machine; tier 1 is back to a median 2.01. **Any change that reduces the work-item count needs this guard**, and the guard must be written without `cmov` — `if (c) x = 0;` gives ``Error: `cmovb' is not supported on `k1om'``, so it is written as `nvp *= (unsigned)(nvp >= 4u * nthreads)`.
- **Found (negative result)** `_mm_prefetch` and `__builtin_prefetch` are unusable on k1om; only inline-assembly `vprefetch0` works. The distance sweep on single runs showed nothing (50.7–53.1 across 0–64); best-of-5 resolves +2.3 %. **Below ~5 %, single runs on this card are not a measurement.**
- **Added (measurement)** Ablations that localise the remaining 95 %: unmasked scaling → 64.6 GFLOPS (+19 %); no reciprocal-square-root at all → 108.1 GFLOPS (+96 %). The transcendental chain is ~half the runtime, but removing it entirely still only reaches 9 % of peak — the surrounding 14-instruction loop takes ≈111 cycles on its own. The floor is memory/issue behaviour, not mathematics.

## 2026-10-08 (third addendum) — T7: five tiers, and a verification that can fail

- **Added** Tiers 4 (N=32768, 4 steps) and 5 (N=65536, 2 steps) via a shared `NTIER` in `nbody_host.cpp` and a five-iteration loop in `t7_nbody.sh`. Throughput keeps rising with N: 4.55 / 37.84 / 55.37 / 78.86 / **95.44** GFLOPS, with a best observation of 100.96. The scalar `-O0` baseline rebuilt with `-DNB_SCALAR_BASELINE=1` on the same inputs gives 0.57 / 2.07 / 2.64 / 2.96 / 3.02 — **8.0× to 31.6×**, and the earlier three-tier picture understated the result.
- **Fixed (serious)** The suite could not detect a wrong force law. With `inv = 0` — no gravitational force at all — the host-reference checksum still passed and the energy error *improved*. The initial conditions were a regular lattice whose net force cancels by symmetry, and the dynamics were ballistically dominated. Both facts are now corrected: positions come from an integer `hash01` (irregular, and bit-reproducible across host and card), and two direct kernel self-tests were added.
- **Added** `rsqrt_max_rel_err` — 512 log-uniform points spanning the documented design range, card vector `rsqrt_pd` vs scalar `1/sqrt`: **2.556e-16**. And `accel_max_rel_err` — the vector force kernel on the first 64 bodies vs a scalar libm loop on the card: **6.340e-16**. The four deliberately-broken force variants all fail the second check (1.000e+00 to 1.001e+00) while passing the checksum, which is exactly the point.
- **Changed** The host reference is compared at every tier, not only `n <= 4096`, and is parallelised **over `i` only** — each thread runs a complete sequential `j` loop, reproducing the card kernel's accumulation shape. Parallelising over `j` would change the summation order and void the comparison. Diffs at the five tiers: 0.000e+00, 0.000e+00, 1.482e-16, 2.960e-16, 1.482e-16.
- **Added** Two gates to `t7_nbody.sh`: the force-kernel self-test (5/5 tiers) and the all-tier reference comparison. The suite now runs **17 checks**, all passing.
- **Found** `cmov` again, three times: `cmovb` from the unroll guard's conditional assignment, `cmovs` from a signed `n / 8` in the new probe, and once in the host. Write guards as `x *= (unsigned)(cond)`.

## 2026-10-08 (fourth addendum) — T7: sixth tier, four-step floor, opt-in reference

- **Added** Tier 6 (N=131072, 4 steps) — `NTIER` is now 6 and the driver loops 1..6. Card memory at this tier is 7 MB across the seven arrays.
- **Changed** `stepss` to `{ 20, 10, 5, 4, 4, 4 }`: no tier runs fewer than 4 steps. Shorter runs let the ballistic term dominate, which is exactly the weakness that let a *zero* force pass every check (see the third addendum).
- **Added** `T7_REF` control in `nbody_host.cpp` with `NREF_DEFAULT 16384`. The O(N²)·steps reference now runs by default only for N ≤ 16384; `T7_REF=1` forces all six tiers, `T7_REF=0` disables it entirely. Suite time: **32 s** default, **227 s** forced.
- **Changed** `t7_nbody.sh` records a skipped reference as **SKIP** rather than PASS, so an unverified tier can never be counted as verified. Result in each mode: default 16 pass / 3 skip / 0 fail; `T7_REF=1` 19 pass / 0 skip / 0 fail.
- **Unchanged (deliberately)** The force-kernel and `rsqrt` self-tests are not gated — they run at every tier in both modes, because they are the checks that can actually fail.
- **Measured** Six-tier scalar vs vector on identical inputs: 0.52/2.38, 2.09/20.38, 2.65/56.48, 2.97/86.72, 3.02/105.05, 3.07/104.72 GFLOPS. The vector build plateaus at ≈105 GFLOPS (8.7 % of FP64 peak) from N=65536 upward; the scalar build plateaus at ≈3.

## 2026-10-08 (fifth addendum) — T7 headroom analysis and a fix to the force self-test

- **Fixed** The force-kernel self-test was exercising the wrong code path. With `NSB=64` the parallelism guard decides the work items cannot fill 240 threads and falls back to the single-block tail loop, so the two-block main loop was never tested — breaking its `rsqrt` left the self-test reporting 6.340e-16. `nbody_accel_force2()` now lets the test force the two-block path, and the self-test runs both. The broken path is caught at **1.001e+00**.
- **Measured** Corrected cycle accounting for the inner loop: 119 instructions (78 FP-pipe) in 113–115 cycles = **1.05 IPC, 69 % of the FP-issue bound**, consistent at N=65536 and N=131072. Deleting the reciprocal-square-root gives 289.8 GFLOPS against 109.7 (2.6×), matching the model's ~3.5× prediction minus the non-FP instructions.
- **Found (negative result)** Manual lockstep interleaving of the two blocks' `rsqrt` chains (`rsqrt_pd_2`) changes nothing — the kernel is FP-issue bound, not latency bound. Kept in the source with that recorded.
- **Recorded** The one remaining lever is symmetry (each pair computed once): halves rsqrt evaluations from n²/8 to n²/16 and reduces memory ops, expected 1.5–1.8×. Costs: summation order changes, masked diagonal blocks, partner-accumulator load-modify-store. See [N12](../docs/N12-t7-nbody-headroom.md) §4.

## 2026-10-08 (sixth addendum) — T7: architecture-level knobs all measure flat

- **Measured** Tier-6 best-of-3 sweeps of every architecture-level lever on KNC: threads/core (61/122/183/240/244) → 111.4–113.0 GFLOPS; thread affinity (none, `GOMP_CPU_AFFINITY`, `OMP_PROC_BIND` spread/close/true) → 110.7–113.1; `-falign-loops` 16/32/64/128 and `-falign-functions=64` → 111.2–112.0; `vprefetch0/1/2/NTA` → 111.0–111.9. **Nothing moves the number.**
- **Understood** Per-core throughput is identical at 1 and at 4 threads/core, so a single thread already saturates the limit. A KNC programming guide confirms the mechanism: ILP on this chip comes from the hardware threads, not from intra-thread interleaving — which is why the lockstep experiment in the fifth addendum could not have worked.
- **Found (blocked)** The MVEX embedded broadcast `{1to8}`, which would fold the inner loop's four `vbroadcastsd` into the arithmetic, is documented in the ISA manual but unreachable: GCC never emits it and GNU as rejects the syntax.
- **Note** `T7_REF`, `OMP_NUM_THREADS` and `GOMP_CPU_AFFINITY` are all honoured by the current driver; the sweeps above were run through the built sink directly.

## 2026-10-08 (seventh addendum) — MPairs/s, embedded broadcast, symmetry analysis

- **Added** `mpairs` to the sink `Result` (ABI updated on both sides) and the host: million physics pairs per second, `pairs · steps / secs / 1e6`. `t7_nbody.sh` now prints both rates per tier.
- **Found** The MVEX embedded broadcast `{1to8}` **is** usable: the earlier rejection was GCC's asm template swallowing bare braces (it emitted `(%rdi)1to8`); `%{1to8%}` is the escape. Applied to the two-block loop it removes all four `vbroadcastsd`. Verified bit-identical via the force self-test and checksum.
- **Measured (negative)** …and it is 6.5 % slower (5204.68 vs 5567.46 MPairs/s at tier 6). `NB_BCAST_FOLD` defaults to 0.
- **Analysed** Symmetry is **not** the remaining 1.5–1.8× lever that N12 claimed: it requires a cross-lane reduction, and k1om rejects every lane shuffle and permute (`vpermilpd`, `vshufpd`, `vshuff64x2`, `vpermq`, `valignq`, `vshufi64x2`), with no MVEX swizzle intrinsics in the headers. The memory round-trip fallback costs more than the saved rsqrt.

## 2026-10-08 (eighth addendum) — T7: thread count is a real parameter now

- **Fixed** `OMP_NUM_THREADS` never reached the card — the host environment does not cross `COIProcessCreateFromFile`. All prior thread/affinity sweeps were therefore re-running one configuration. `Params` now carries `nthreads` (host fills it from **`T7_THREADS`**) and the sink calls `omp_set_num_threads()` before any parallel region. The host prints whether the count came from the variable or the library default.
- **Measured** With the fix, threads matter: tier 6 MPairs/s, best-of-7 — 61 → 2490.88, 122 → 4005.63, 183 → 4721.80, 220 → 5172.21, **240 → 5580.94**, 244 → 5001.22. SMT is worth ≈2.2×; **over-subscribing to 244 is 10 % slower than 240**, so the library's 240 default is the good one. Tier 3 prefers 220 (3983.79 vs 3502.82).
- **Retracted** The earlier "thread count and affinity are flat" claim, and the inference drawn from it that a single thread saturates the core. `GOMP_CPU_AFFINITY` / `OMP_PROC_BIND` are host variables too and were never applied — affinity remains unmeasured.

## 2026-10-08 (ninth addendum) — N-body design specification published

- **Added** [N14 — N-body computation: algorithm design specification](../docs/N14-nbody-algorithm-design.md) (EN + CN), consolidating the T7 work into a single document sufficient to re-implement or port the kernel: requirements, algorithm, numerical design, the KNC capability table, kernel and software structure, verification design, performance, rejected alternatives, and porting notes.
- **Note** No code changed in this addendum; N14 is a consolidation of measurements already recorded in N9–N13.

## See also

- Suite description and usage: `tests/README.md`
- Acceptance results and per-check criteria: `docs/J-acceptance-tests.md`
- Changes to the objects under test: each sub-project's `CHANGELOG.md`
