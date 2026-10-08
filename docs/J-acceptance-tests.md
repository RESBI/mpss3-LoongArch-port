# Appendix J — Acceptance Test Suite: Turning "It Runs" into Something Re-checkable

The preceding chapters answer "which lines have to change"; Appendices E, H and I record "what came out once they did". This appendix supplies the last link: **how someone else can verify these conclusions all over again.** A set of scripts ships with the release (under `tests/` in the release tree); it breaks "is this port actually usable" into eight stages, T0–T8, and every criterion corresponds to a pit that was actually stepped in during the port; run it once and you know whether the conclusions still hold.

---

## J.1 What It Is and Where It Lives

| Item | Detail |
|---|---|
| Location | `tests/` inside the release tree (a peer of `00-build-tools` … `09-boot-images`) |
| Entry points | `run_all.sh` (build → install → test), `run_tests.sh` (functional tests only, **an ordinary user** is enough) |
| Single stage | `t0_build.sh` … `t7_nbody.sh`, each runnable on its own with `--only tN` |
| Logs | each run gets its own directory `tests/logs/run-<timestamp>/`, with a per-stage `T*.log` plus a per-tier `T7-tier*.log` |
| Scale | `--quick` drops one tier; `TEST_N` / `TEST_N2` / `TEST_N3` / `TEST_ROUNDS` override it precisely |

The reason "runnable by an ordinary user" is treated as a hard requirement is that it is itself one of the properties under test: the goal of this port is that "the card can be used by an ordinary user", and every step that needs root (building, installing modules, installing udev rules) is fenced off inside the two stages T0 / T1.

---

## J.2 Stages and Criteria

| Stage | Script | What it tests | Key criteria | Needs root |
|---|---|---|---|---|
| T0 | `t0_build.sh` | builds the 9 sub-packages in dependency order | `mic.ko` / `libscif.so` / `mpssd` / `libcoi_host.so` / the card-side image are produced, and `modinfo` can read them | yes |
| T1 | `t1_install.sh` | install, `depmod`, `modprobe`, relax device permissions, start MPSS | the card reaches `online`, `coi_daemon` is up, `/dev/mic/scif` is mode 666 and an ordinary user can open it | yes |
| T2 | `t2_ssh.sh` | card access and deployment | SSH reachable, cross-compilation works, md5 matches after deployment, execution on the card prints the right output | no |
| T3 | `t3_scif_comm.sh` | SCIF message channel | 64-byte and 26496-byte round trips with 0 mismatches, card-side unsolicited messages, bidirectional fence | no |
| T4 | `t4_rma_dma.sh` | RMA / `writeto` | seven length tiers (including offset controls and non-page-multiple lengths) byte-for-byte correct, full 26496-byte verification with 0 mismatches | no |
| T5 | `t5_offload.sh` | COI end to end | the sink's `CardReduce` is in `.dynsym`, process creation returns `COI_SUCCESS(0)`, 240 threads, error against the analytic value <1e-12 | no |
| T6 | `t6_offload_stress.sh` | higher stress | repeated large-scale process create / destroy over several rounds, with the card-side daemon still alive and zero kernel anomalies after each round | no |
| T7 | `t7_nbody.sh` | heavy computation plus large data volumes | energy conservation at three scales, checksum bit-identical to the host reference implementation at small scale, GFLOPS | no |
| T8 | `t8_bigxfer.sh` | Bulk transfer (4 GiB by default) | Card-side large `malloc` succeeded, the transfer covered the full size, **checksums identical on both sides**, a bandwidth was measured, zero kernel exceptions | No |

There are also `precheck.sh` (builds only, runs nothing and never touches the card, to separate "build problems" from "run problems") and `perm_probe.sh` (relaxes permissions as root and then **actually completes one offload as an ordinary user**, to verify the non-root use case on its own).

---

## J.3 Measured Results on Real Hardware (2026-10-06)

Environment: Loongson 3A6000 (AOSC OS 13.3.1, kernel `7.1.13-aosc-main-16k`, 16 KB pages) plus a Xeon Phi 7120P, the card `online`, and the card-side `coi_daemon` running. Every functional stage was executed as an **ordinary user**.

| Stage | Passed | Failed | Skipped | Highlights |
|---|---:|---:|---:|---|
| T2 | 8 | 0 | 1 | card-side kernel `2.6.38.8+mpss3.8.6`; cross-compiled 10840 bytes, md5 matches after deployment, the card prints `HELLO_FROM_CARD`. The 1 skipped item is "card-side rootfs type detection" (BusyBox `mount` output does not match the criterion; purely informational) |
| T3 | 11 | 0 | 0 | both the 64-byte and the 26496-byte round trip show 0 mismatches; card-side unsolicited messages and bidirectional fence both present |
| T4 | 15 | 0 | 0 | all seven length tiers byte-for-byte correct; full 26496-byte verification 0 mismatches; no unaligned-access faults |
| T5 | 17 | 0 | 0 | process creation `COI_SUCCESS(0)`, 240 threads, reduction relative error **−3.35e-16**, daemon still alive afterwards |
| T6 | 12 | 0 | 0 | three rounds with n = 2×10⁷ / 2×10⁸ / 1×10⁸, 0.14 / 0.37 / 0.29 seconds on the card, errors −1.40e-16 / −2.24e-16 / 0; the daemon survives each round, zero kernel anomalies; offload still works normally after the stress |
| T7 | 9 | 0 | 0 | see the table below |
| T8 | 12 | 0 | 0 | A 4 GiB card-side `malloc` (every page touched) plus a 1 MiB registered window; the full 4 GiB transferred with **bit-for-bit identical checksums** on both sides; 325.9 MB/s wire-side, 98.8 MB/s end-to-end; no kernel exceptions |

N-body gravity, $O(N^2)$, at three scales (the card side generates its own initial conditions, and the results come back through the return area):

| Tier | N | Steps | Threads | Checksum vs. host reference | Energy relative error | Time on card | Throughput |
|---|---:|---:|---:|---|---:|---:|---:|
| 1 | 1024 | 20 | 240 | **difference 0.000e+00 (bit-identical)** | 1.96e-04 | 0.40 s | 0.62 GFLOPS |
| 2 | 8192 | 10 | 240 | — (too large; only energy conservation is checked) | 8.47e-05 | 3.1 s | 2.10 GFLOPS |
| 3 | 16384 | 5 | 240 | — | 4.02e-05 | 5.1 s | 2.68 GFLOPS |
| 4 | **When a window is reused, its staging area must not overlap the destination of window 0** | With the window registered at the start of the big buffer, every later window's inbound data overwrites what window 0 had just placed there — the symptom is "everything after the first MiB is right, the first MiB is wrong" | T8's card side registers a separate staging buffer and copies each window into the big buffer; a per-window checksum pins any error to a window number |
| 5 | **A large window's page count and chunk spans must be interpreted with the right side's page size** (see [Appendix I](I-page-size-alignment.md) I.12) | The host's own window counts were multiplied by four and peer counts divided by four, so unmapping removed four times the pages (`mic_smpt.ref_count < 0`) and lookups failed (`micscif_get_dma_addr` BUG) | After T4, **wait 30 seconds** before checking dmesg (the accident fires in a workqueue seconds later, invisible to T4's own check); T8 uses a 1 MiB multi-page-chunk window that exercises the "pages != chunks" branch |

T8 bulk transfer at three sizes (host to card; the card verifies every window and then the whole buffer):

| Case | Volume | Wire-side | End-to-end | Transfer | Card whole-buffer checksum | Checksums |
|---|---:|---:|---:|---:|---:|---|
| 1 | 64 MiB | 359.0 MB/s | 103.8 MB/s | 0.617 s | 0.28 s | identical ✓ |
| 2 | 256 MiB | 338.6 MB/s | 101.2 MB/s | 2.530 s | 1.14 s | identical ✓ |
| 3 | 4096 MiB | **325.9 MB/s** | **98.8 MB/s** | 41.46 s | 18.34 s | **bit for bit** ✓ (`0x76ac888ab487e57b`) |

The two bandwidths mean different things and both are reported: the **wire side** times only the `scif_writeto` calls themselves, which is the real PCIe throughput (consistent with the measured x1 link width); the **end-to-end** figure also includes the card's per-window verification and copy, because in windowed mode the host waits for the card before filling the next 1 MiB window. Integrity is covered by checksums on both sides — the host over its source buffer, the card over what it received — plus a **per-window** checksum that localises any error to a window number.

Across the functional stages the total is **84 passed, 0 failed, 1 skipped**. T0 / T1 need root and modify the system (writing to `/usr`, installing modules), so they were not re-run this round; they had already been walked through with the same scripts in the 2026-10-05 release acceptance run.

---

## J.4 Five measured pitfalls baked into the tests

All three were stepped in and then fixed during the port, and are now written into the scripts, so that **if a change breaks one of them the test goes red immediately**:

| # | Constraint | Symptom when violated | Criterion in the script |
|---|---|---|---|
| 1 | the card-side sink must be built with **`-rdynamic`** | the exported symbol is not in `.dynsym`, `dlsym` fails on the card side, and the host gets `COI_DOES_NOT_EXIST(5)` | after the T5 / T7 build, `readelf --dyn-syms` must show `CardReduce` / `NBodyRun` |
| 2 | the card-side sink's **optimisation level has to be measured per source** | the N-body sink built with `-O1` / `-O2` crashes on the card immediately (card-side log `segfault at 0`, with the faulting ip landing on a `vpackstorelpd`); yet the reduction sink is perfectly fine with `-O2` | T7 pins `-O0` and all three tiers must run to completion; T5 / T6 stay on `-O2` (measured 17/17 passed). **Run it once before changing the optimisation level** |
| 3 | **`COIBufferCreate` is unusable** (`COI_OUT_OF_MEMORY(13)`) | large data cannot be sent in one go | T5 / T7 switch to "the host passes only a few dozen bytes of parameters and the card side generates from a deterministic formula", with the results going through the return area (≤64 KiB in, 64 bytes out) |

Item 2 also comes with a self-correction: the self-check rule given earlier was "the disassembly must contain 0 `vpackstore` instructions", which was disproved on 2026-10-06 when re-checked with a correct k1om objdump — the `-O0` build that runs fine also has 12 of them, while the crashing `-O1` / `-O2` builds have only 11 each. **The count is not the criterion; whether it runs on the card is**, so that item in T7 has been demoted from an "assertion" to "reference information". The full evidence is in [Appendix H](H-offload-field-notes.md) H.14.

---

## J.5 Two Lessons in Criteria Design

1. **Use "the checksum is bit-identical" instead of "is there a result".** The card side computes a deterministic checksum into the return area, and the host computes the same number with its own reference implementation and compares. This catches both classes of fault at once — "computed wrong" and "transferred wrong"; it is precisely how the N-body example exposed the error in the front end's "interleaved array aliasing" formulation (card and host disagreed by 6.1e-04, and after switching to separate arrays they matched bit for bit).
2. **A self-check rule needs a control sample.** When judging "does this instruction exist", you must also measure a sample known to work (here, the T5 sink); otherwise a failed measurement command gets taken as the passing signal "0 hits" — that is exactly how the wrong rule in J.4 item 2 survived.

---

## J.6 Reproduction Commands

```bash
# full run (build → install → functional tests); T0/T1 need root
sudo bash tests/run_all.sh

# functional tests only (an ordinary user is enough; requires: installed, mic loaded, card online)
bash tests/run_tests.sh

# a single stage / quick scale
bash tests/run_tests.sh --only t7
bash tests/run_tests.sh --quick

# run this first when the build fails: builds only, never touches the card
bash tests/precheck.sh

# verify the "non-root use" scenario on its own (needs root to change device permissions)
sudo bash tests/perm_probe.sh
```

The offload-related stages (T5 / T6 / T7) additionally need three things in the porter's hands: a k1om cross-compiler, a k1om sysroot, and the card-side dependency library directory (including the self-built k1om `libgomp.so.1`). Their locations are given by `K1OM_SDK` / `K1OM_CXX` / `COSLIB` in `tests/lib/common.sh` and can be overridden with environment variables; the card address defaults to `<card-ip>` (`CARD_HOST` overrides it).

---

## J.7 How This Appendix Relates to the Others

| If you want to know | Read |
|---|---|
| where each criterion in the test scripts comes from (which pit corresponds to which item) | this appendix, J.2 and J.4 |
| how to write a card-side offload program, and how to use a particular API | the *KNC offload Programming Manual* shipped with the release, `OFFLOAD_GUIDE.md` (organised by COI API: model and getting started / processes and libraries / computation and threads / data channels / synchronisation / troubleshooting and tuning / interface reference one by one) |
| how COI was made to work, and what the three defects were | [Appendix H](H-offload-field-notes.md) H.13 |
| how a wrong criterion that once made it into the guide was disproved | [Appendix H](H-offload-field-notes.md) H.14 |
| the full investigation of the two defects in page size and struct layout | [Appendix I](I-page-size-alignment.md) I.10, I.11 |
| every porting patch on the tool side | [Appendix E](E-porting-patches.md) |
