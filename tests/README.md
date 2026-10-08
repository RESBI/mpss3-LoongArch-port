# MPSS 3.8.6 LoongArch Port · Acceptance Test Suite

These scripts turn "does this port actually work?" into a set of repeatable checks that report PASS/FAIL one by one. They double as a regression fence: every criterion in `t4`/`t5`/`t7` corresponds to a pit that was really stepped in during the port.

**Design constraint: the suite is not tied to the layout of any one machine.** All paths are resolved in one place, `lib/common.sh`, in the order "environment variable → `tests/config.sh` → auto-detection". When detection fails it does not guess and does not push ahead with a wrong value; it marks the stage `SKIP` and prints which variable to set. No concrete IP, user name or absolute path appears in the stage scripts.

## I. How to Use It (Three Steps)

```bash
cd tests
cp config.example.sh config.sh     # (1) copy the site configuration example
$EDITOR config.sh                  # (2) fill in only what auto-detection cannot find (usually one or two lines)
bash run_tests.sh                  # (3) run the functional tests (an ordinary user is enough, no root needed)
```

Environment variables take precedence over `config.sh`, so a one-off override needs no edit:

```bash
CARD_HOST=192.168.1.2 bash run_tests.sh --only t2
```

## II. Prerequisites and Auto-detection

| Used for | What it needs | When detection fails |
|---|---|---|
| `t0` / `t1` (build and install) | root, kernel source or headers, a complete and writable release tree | mark `SKIP`, prompt to set `KSRC`/`RELEASE_DIR` |
| `t2`–`t4` (card access and data path) | an **ordinary user** is enough; `/dev/mic/scif` readable and writable by the user; the `mic` module loaded, the MPSS stack started, the card `online` | mark `SKIP`, prompt to set `CARD_HOST` or to have MPSS configure `mic0.conf` |
| `t5`–`t7` (offload) | additionally needs the k1om compiler, a k1om sysroot, and the card-side dependency library directory (including the self-built k1om `libgomp`) | mark `SKIP`, naming item by item which one is missing |

What auto-detection covers (tried from top to bottom):

| Item | Variable | Detection order |
|---|---|---|
| Card address | `CARD_HOST` | MPSS's `/etc/mpss/mic0.conf` (`micip=`) → left empty means skip |
| Card login user | `CARD_USER` | defaults to the user running the tests (`SUDO_USER` under `sudo`) |
| Release tree | `RELEASE_DIR` | the directory holding `tests/` (if that is a complete release tree) → `release*` under the project root. The criterion is that the top-level `Makefile` contains `PKGS` and the subpackages carry `Makefile.mpss` |
| Install prefix | `PREFIX` | defaults to `/usr` |
| COI headers and libraries | `STAGE` or `COI_PREFIX` | `STAGE` → `COI_PREFIX/usr` → `COI_PREFIX` → `/usr/local` → `/usr` (the directory must contain `include/intel-coi` and `libcoi_host.so`) |
| User-space SCIF library | same as above | same as above (must contain `include/scif.h` and `libscif.so`) |
| k1om SDK and sysroot | `K1OM_SDK` | `k1om-sdk` under the project root → `/opt/mpss/sdk` → `/opt/mpss` |
| k1om compiler | `K1OM_CC`/`K1OM_CXX` | the wrapper script inside the project → `PATH` → the path inside the SDK (when the SDK compiler is called directly, `-B` and `--sysroot` are added automatically) |
| Card-side dependency library directory | `SINK_LIBS` | the SDK's k1om sysroot `usr/lib64` (if that has no `libgomp.so`, the offload stages prompt you to point at your own directory) |
| k1om objdump | automatic | `k1om-mpss-linux-objdump` inside the SDK |
| Kernel source | `KSRC` | `/lib/modules/$(uname -r)/build` → `/usr/src/linux-headers-$(uname -r)` |
| Module install directory | automatic | `modinfo -n mic` → `/lib/modules/$(uname -r)/updates` (or `extra`) |

## III. Stage Overview

| Stage | Script | What it tests | Needs root |
|---|---|---|---|
| T0 | `t0_build.sh` | builds the release tree's 9 subpackages in dependency order and checks that `mic.ko`/`libscif.so`/`mpssd`/`libcoi_host.so`/the card-side image were produced | yes |
| T1 | `t1_install.sh` | `make install` → `depmod` → `modprobe mic` → install modern udev rules that open up device permissions → start mpss → wait for the card to come `online` → card-side `coi_daemon` | yes |
| T2 | `t2_ssh.sh` | SSH reachability, the card-side environment (kernel / uptime / memory / ramfs), and cross-compiling and deploying a small program (md5 checked) before running it on the card | no |
| T3 | `t3_scif_comm.sh` | establishing a SCIF link, round trips of a small message (64 B) and a large one (26496 B, the size of a COI create command), card-initiated messages, bidirectional fences | no |
| T4 | `t4_rma_dma.sh` | a seven-step length sweep for RMA/`writeto` (with offset controls and non-page-multiple lengths) plus a fixed 26496-byte byte-by-byte check over the whole buffer | no |
| T5 | `t5_offload.sh` | COI end to end: card-side `CardReduce` must be in `.dynsym` → enumerate engines → create a process on the card → create pipes → fetch the function handle → OpenMP reduction on the card → compare against the parsed value | no |
| T6 | `t6_offload_stress.sh` | many rounds at large scale, creating and destroying processes repeatedly; each round checks that the card-side daemon is alive and the kernel logged nothing unusual | no |
| T7 | `t7_nbody.sh` | N-body gravity O(N²) at three sizes (1024/8192/16384): the card side generates its own initial conditions, results come back through the return area, the checksum is compared bit for bit against the host reference implementation, energy conservation, GFLOPS; it also counts the vector instructions in the card-side binary (informational, not a criterion) | no |
| T8 | `t8_bigxfer.sh` | Bulk transfer: **4 GiB** by default, pushed from the host to the card — a 4 GiB card-side `malloc` with every page touched, a 1 MiB registered window, chunked RMA; **per-window** verification plus whole-buffer checksums on both sides; bandwidth measured two ways. Measured on hardware: 64 MiB / 256 MiB / 4 GiB all pass, 4 GiB at **119.3 MB/s** end-to-end (**3478 MB/s** inside `scif_writeto`; the link is PCIe Gen2 x8) with bit-for-bit identical checksums. Sizes come from `TEST_BIG` / `TEST_BIG_WINDOW` / `TEST_BIG_RMA`; `--quick` drops to 256 MiB | No |
| T9 | `t9_gemm_bench.sh` | GEMM FP32/FP64 at N=256/1024/2048 (checksum against the host reference, GFLOPS). This is the **scalar baseline**: card-side sink built with `-O0`, triple loop, no tiling and no vectorisation | no |
| T9-VEC | `t9_gemm_vec.sh` | Same GEMM but card-side sink built with **`-O2 -mavx512f`** and hand-written IMCI intrinsics. Gates on disassembly (`vfmadd > 0` and `xmm/ymm == 0`), not just "it ran". Measured N=2048 at 244 threads: **FP32 147–261 GFLOPS, FP64 120–138 GFLOPS** (≈6–11 % of the 7120P's 2416 / 1208 GFLOPS peaks); run-to-run spread is wide on FP32, so the script's own best-of-3 is the number to quote. See §VIII for the recipe, the tuning table and the variance caveat | no |

There are three auxiliary scripts:

| Script | Purpose | Needs root |
|---|---|---|
| `precheck.sh` | builds only, runs nothing and never touches the card: it separates "build problems" from "runtime problems" (run it first when a functional test fails) | no |
| `perm_probe.sh` | verifies the "non-root use" scenario: after device permissions are opened up, it **really completes one offload run** as an ordinary user | yes |
| `run_all.sh` / `run_tests.sh` | convenience entry points (see below) | depends on the stage |

## IV. How to Run It

```bash
# full pipeline: build → install → functional tests (full scale by default)
sudo bash run_all.sh

# functional tests only (no root needed)
bash run_tests.sh

# a single stage
bash run_tests.sh --only t7
sudo bash t0_build.sh
sudo bash t1_install.sh

# small, fast regression (T5/T6's N drops one step automatically)
bash run_tests.sh --quick
```

Switches for `run_all.sh`: `--no-build`, `--no-install`, `--tests-only`, `--quick`.
Scale can be overridden with environment variables: `TEST_N` (T5/T6 reduction size), `TEST_N2`/`TEST_N3` (the last two rounds of T6), `TEST_ROUNDS` (number of T6 rounds), `TEST_NFINAL` (the closing size for T6).

Logs get a directory of their own **per run**, so ownership problems left by the previous root-owned run cannot produce a "false failure":

```text
tests/logs/run-<YYYYmmdd-HHMMSS>/
```

## V. How to Read the Results

| Marker | Meaning | What to do |
|---|---|---|
| `[PASS]` | the criterion holds | — |
| `[FAIL]` | the criterion does not hold; the problem lies with **the thing under test or the environment** | read the stage log and the matching chapter of the report |
| `[SKIP]` | this machine did not provide the prerequisite (missing configuration, missing toolchain, card unreachable, and so on) | set the variable the printed hint names and run again; a skip is not a pass |

The summary lists the skipped items separately. Acceptance before a release should reach "zero skips"; everyday regression runs may skip, but you should know why each item was skipped.

## VI. Directory Layout

```text
tests/
├── config.example.sh     site configuration example (copy to config.sh and fill it in; config.sh is not committed)
├── run_all.sh            build → install → test in one shot
├── run_tests.sh          functional test entry point (T2…T7)
├── t0_build.sh  t1_install.sh
├── t2_ssh.sh    t3_scif_comm.sh   t4_rma_dma.sh
├── t5_offload.sh  t6_offload_stress.sh  t7_nbody.sh
├── t8_bigxfer.sh         bulk transfer
├── t9_gemm_bench.sh      GEMM scalar baseline (-O0)
├── t9_gemm_vec.sh        GEMM with IMCI intrinsics (-O2 -mavx512f)  ← §VIII
├── precheck.sh  perm_probe.sh
├── lib/common.sh         shared library: configuration loading, path detection, PASS/FAIL/SKIP accounting, card-side deployment, capability checks
├── src/                  test case sources (card-side .c/.cpp paired one-to-one with the host side)
└── CHANGELOG.md          change log for this subproject
```

## VII. A Few Measured Constraints (the test cases already steer around them for you; be careful when writing your own programs)

1. **A card-side program must be built with `-rdynamic`**, otherwise the exported symbol is not in `.dynsym`, card-side `dlsym` fails, and the host gets `COI_DOES_NOT_EXIST(5)`.
2. **The optimisation level for a card-side program has to be measured source file by source file** — but the reason is narrower than it first looked, and §VIII now explains it. Both outcomes turned up in this suite: the reduction programs in `t5`/`t6` build with `-O2` and run fine (17/17), whereas the N-body program in `t7` **always crashes** at `-O1`/`-O2` (card-side `/var/log/messages` shows `segfault at 0`, with the faulting ip on a `vpackstorelpd` instruction); only `-O0` runs, and then all three sizes agree with the host reference implementation bit for bit — so `t7` is pinned to `-O0`.
   The root cause is not "-O2 is bad" but **which instructions the optimisation exposes**: `-O2` at `-march=knc` generates *scalar* floating-point code (`vaddss`/`vcvtsi2ss`, xmm forms) and `cmov`, none of which KNC has; the k1om assembler rejects some of them outright, and for the ones it accepts (masked compress stores) the card faults at run time. `t9_gemm_vec.sh` shows the way out: `-O2 -mavx512f` **runs fine and is 38× faster than the `-O0` baseline**, provided the source keeps every floating-point operation inside 512-bit intrinsics.
   **But do not use "does the disassembly contain `vpackstore`/`vscatter`" as a criterion**: counted with the k1om objdump that actually ships in the SDK, the working N-body `-O0` build has **12** of them (all writing to the stack), and the crashing `-O1`/`-O2` builds have **11** each (one of which switches to register addressing). **The count is not the criterion; running on the card is.**
3. **`COIBufferCreate` is unusable on this port's toolchain** (it returns `COI_OUT_OF_MEMORY(13)`), so large data always takes the route of "the host passes only a few dozen bytes of parameters and the card side generates the rest from a deterministic formula", with the results coming back through the return area.
4. **The return value must carry a deterministic checksum**, and the host computes the same number with its own reference implementation and compares — far stronger than "did a result come back", because it catches both a wrong computation and a wrong transfer.
6. **Page counts and chunk spans must be interpreted with the page size of the side that owns the window** (a pitfall hit twice; the full chain is in `../docs/I-page-size-alignment.md` I.12): a host window's counts were once packed in peer units (x4), so unmapping removed four times the pages (`mic_smpt[i].ref_count < 0`) and eleven seconds later `micscif_get_dma_addr` could not find the address and hit `BUG()`; peer counts were meanwhile divided by four, turning the common "one card page = 4 KiB" chunk into zero and failing the same lookup. After the fix, T8 runs 4 GiB reliably with a 1 MiB window and 26496-byte RMAs. **The earlier claim that a single RMA larger than 26496 bytes wedges the machine is retracted** — it was measured against an already-broken path; the real limit has yet to be calibrated.
5. For the detailed causes, correct-versus-incorrect code side by side, and debugging techniques, see `../docs/OFFLOAD_GUIDE.md` (the constraints are in §11.5 access flags, §14 reference counting and state, §15 large data transfer; troubleshooting is in Part 6).

## VIII. Getting Real Vector Code out of the k1om Toolchain (T9-VEC)

T9's card-side GEMM was taken from the 0.33 GFLOPS scalar baseline to
**147–261 GFLOPS FP32 / 120–138 FP64** (≈6–11 % of the 7120P's 2416 / 1208
GFLOPS peaks) by writing the kernel with explicit IMCI intrinsics and building it
`-O2 -mavx512f`. The short version of why that is not obvious, and what it
overturns:

- The k1om backend **never auto-vectorises** — `-O0/-O1/-O2/-O3/-march=knc` all
  yield `vfmadd = 0`. Intrinsics need `-mavx512f`, but that flag retargets the
  whole file to generic AVX-512F, so **any scalar floating-point code** turns
  into xmm forms that KNC has no registers for.
- KNC also lacks `vcvtdq2ps`, all 64-bit integer vector ops, **any unaligned
  vector access**, and **`cmov`** — the last of which means **`? :` is unusable**
  in such a file, because it becomes `cmova` and the assembler stops.
- So the suite's old rule *"a card-side sink can only be built with `-O0`"* is
  **too broad**. The real rule is about the source, not the flag: keep every
  floating-point operation inside a 512-bit intrinsic and `-O2 -mavx512f` is
  correct **and** 38× faster than `-O0`. T7 now splits the difference: its
  vector unit builds `-O2 -mavx512f` and its scalar unit stays `-O0`
  ([N11](../docs/N11-t7-two-units-and-three-traps.md)).

The full write-up — the four constraints, the five optimisation steps with their
measured deltas, the tuning table, and what is left — is a standalone series in
`../docs/`:

| | |
|---|---|
| [N1 — Overview and results](../docs/N1-t9-gemm-optimization-overview.md) | the ladder, the verification method, the caveats — start here |
| [N2 — Four constraints](../docs/N2-t9-gemm-isa-constraints.md) | the prerequisites, and a checklist for a new vectorised sink |
| [N3 — Explicit IMCI micro-kernel](../docs/N3-t9-gemm-explicit-vectorisation.md) | 6.39 → 57.4 |
| [N4 — Fixed parallel overhead](../docs/N4-t9-gemm-fixed-overhead.md) | 57.4 → 104.1 |
| [N5 — B panel packing](../docs/N5-t9-gemm-b-panel-packing.md) | 104.1 → 113.9 |
| [N6 — A row-stride padding](../docs/N6-t9-gemm-row-stride-padding.md) | 113.9 → 149.2, the biggest single win |
| [N7 — j-blocking and tuning](../docs/N7-t9-gemm-jblocking-and-tuning.md) | 149.2 → 260.9 |
| [N8 — Remaining headroom](../docs/N8-t9-gemm-headroom.md) | where the other 90 % is |
| [N9 — T7 N-body overview](../docs/N9-t7-nbody-overview.md) | **second sub-series**: T7 at 2.7 → 52.4 GFLOPS |
| [N10 — KNC's FP64 transcendental gap](../docs/N10-t7-gemm-fp64-no-transcendentals.md) | no FP64 sqrt/divide at all; building a 1-ulp `1/sqrt` from `VGETEXPPD`/`VGETMANTPD` |
| [N11 — Two units, three traps](../docs/N11-t7-two-units-and-three-traps.md) | the `-O0` + `-O2 -mavx512f` split, and three faults only the card's kernel log revealed |
| [N12 — T7 remaining headroom](../docs/N12-t7-nbody-headroom.md) | 41 instructions taking 230 cycles — the in-order latency wall |
| [N13 — What the tests actually verify](../docs/N13-t7-verification.md) | deleting the force left every check green; the three-layer verification that replaced it |
| [N14 — N-body algorithm design](../docs/N14-nbody-algorithm-design.md) | **the design specification** — algorithm, numerical design, architecture constraints, kernel structure, rejected alternatives |

Chinese versions: append `_CN` to any of the above. Summary and deliverable
list: `T9_FINAL_REPORT.md` at the repository root.

**T7's card-side thread count is a parameter, not an environment variable.**
The host starts the card process through COI, and **host environment variables do
not cross that boundary** — `OMP_NUM_THREADS` on the host does nothing (verified:
61/122/183/244 all ran 240 threads). Use **`T7_THREADS=<n>`** instead:
`T7_THREADS=220 bash t7_nbody.sh`. Measured best-of-7 at tier 6: 240 threads
gives 5580.94 MPairs/s, 244 gives 5001.22, 61 gives 2490.88 — so SMT is worth
≈2.2× and over-subscribing past 240 costs 10 %.

**T7 runs six tiers and the host reference is opt-in for large N.**
`t7_nbody.sh` covers N = 1024 / 8192 / 16384 / 32768 / 65536 / 131072 with
20/10/5/4/4/4 steps — never fewer than 4, so the force has time to matter. The
O(N²)·steps host reference runs by default only for N ≤ 16384: at N=131072 it
alone costs ~5.2 × 10¹⁰ pair evaluations, which took the suite from 32 s to
227 s. Set **`T7_REF=1`** to force it at every tier, or `T7_REF=0` to disable it
entirely. A skipped tier is reported as **SKIP, never PASS**.

The two kernel self-tests — the force kernel against scalar libm, and `rsqrt`
against `1/sqrt` — are **not** gated and run at every tier. They are the checks
that can actually fail; see [N13](../docs/N13-t7-verification.md).

**T7's sink is now two translation units.** `src/nbody_vec.cpp` holds the O(N²)
hot path built with `-O2 -mavx512f`; `src/nbody_sink.cpp` keeps the `sin`/`cos`
initial conditions, kinetic energy, checksum and timing at `-O0`, because those
are scalar and `-mavx512f` would turn them into xmm instructions. They are linked
into one `nbody_sink`, and the interface between them carries pointers and ints
and never a `double`. The two flags are not interchangeable — see
[N11](../docs/N11-t7-two-units-and-three-traps.md).

**Run it**

```bash
cd release/tests
bash t9_gemm_vec.sh                       # N=256/1024/2048 at 244 threads
TIERS='2048' THREADS=244 bash t9_gemm_vec.sh
MR32=16 JSB32=32 bash t9_gemm_vec.sh      # sweep a parameter
```

Results and the card's cold/warm timings per tier land in
`logs/run-<timestamp>/T9V-*.log`.

## IX. Common Snags

| Symptom | Cause | What to do |
|---|---|---|
| `scif_ok` reports `/dev/mic/scif: Permission denied` (or the pre-flight status says "device accessible: no") | The device node is `crw------- root root`: upstream's udev rule uses the legacy `NAME="mic/%k"` form, which systemd-udev ignores, so its `MODE="0666"` never reaches the node | `sudo bash tests/t1_install.sh` (installs `55-mic-perms.rules` and applies it at once); in a hurry, `sudo chmod 666 /dev/mic/scif /dev/mic/ctrl` |
| After a reboot the card is offline and `/dev/mic` does not exist | The `mic` module is not loaded (blocked by a blacklist in `modprobe.d`, or `modules-load.d` did not take effect) | Check with `lsmod \| grep mic`; run `sudo modprobe mic`; automatic loading relies on `/etc/modules-load.d/mic.conf` |
| The card sits in `booting` and does not move | The card-side image did not come up (mismatched `initramfs` and `bzImage`, or the host-side push failed) | Check `dmesg` and the card console; re-write `echo boot:linux:<bzImage>:<initramfs> > /sys/class/mic/mic0/state` |
| A stage reports `SKIP` | This machine lacks that prerequisite (toolchain, dependency directory, unreachable card, …) | Set `tests/config.sh` or the environment variable named in the printed hint; skipped does not mean passed |
| `make` fails with `modules.order: Permission denied` | The source tree holds root-owned build products left by a previous `sudo make install` | `sudo rm -f Module.symvers modules.order` (or `sudo chown -R $USER .`); building with `make` first and only then `sudo make install` reduces how often this happens |

**Run it with `bash`, not `sh`**: the scripts use bash syntax and features, and on some distributions `sh` is dash, which fails in all sorts of odd ways.

## X. Related Documents

| Want to know | Read |
|---|---|
| What changed in the suite itself | `CHANGELOG.md` |
| The real-hardware acceptance verdict and each criterion | `../docs/J-acceptance-tests.md` |
| How to use the COI API and how to write the programs | `../docs/OFFLOAD_GUIDE.md` |
| What changed in the code under test | the `CHANGELOG.md` in each package directory of the release tree |
| The whole project's change log | `CHANGELOG.md` in the project root |
