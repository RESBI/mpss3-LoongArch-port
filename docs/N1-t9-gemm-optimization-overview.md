# Appendix N1 — T9 GEMM Optimisation: Overview and Results

This is the index of a series of eight reports that document, step by step, how
the card-side GEMM in acceptance test T9 went from **0.33 GFLOPS to 147–261
GFLOPS** on a Xeon Phi 7120P. Each report covers one step, with the hypothesis,
the change, the measured effect, and the rule that generalises.

- **N1 — Overview and results** (this document)
- [N2](N2-t9-gemm-isa-constraints.md) — Prerequisites: the four constraints that block everything
- [N3](N3-t9-gemm-explicit-vectorisation.md) — Step 1: an explicit IMCI micro-kernel
- [N4](N4-t9-gemm-fixed-overhead.md) — Step 2: removing the fixed parallel overhead
- [N5](N5-t9-gemm-b-panel-packing.md) — Step 3: packing the B panel
- [N6](N6-t9-gemm-row-stride-padding.md) — Step 4: padding A's row stride
- [N7](N7-t9-gemm-jblocking-and-tuning.md) — Step 5: j-blocking and register-tile tuning
- [N8](N8-t9-gemm-headroom.md) — What is left, and the next order of magnitude

Code: `release/tests/src/gemm_vec_sink.cpp` (card side),
`release/tests/src/gemm_vec_host.cpp` (host side),
`release/tests/t9_gemm_vec.sh` (driver).

## 1. Why this series exists

T9's original purpose was to prove that **COI offload computes correctly**, not
to win a performance contest: a naive triple loop, card side built with `-O0`,
0.33 GFLOPS. That was fine as a correctness test and misleading as a performance
number — it is 0.014 % of what the card can do.

The obvious next move ("rebuild it with `-O2`") does not work on this toolchain,
and the reason took several wrong turns to find. The result is that this suite
had accumulated a conclusion — *"a card-side program can only be built with
`-O0`"* — that is **too broad**, and the reports below replace it with the
narrow, checkable rule:

> The constraint is not the optimisation level. It is whether the source
> contains *scalar* floating-point code or a `? :` operator. Keep every
> floating-point operation inside a 512-bit intrinsic and `-O2 -mavx512f`
> is correct **and** 38× faster than `-O0`.

## 2. Results

Card: Intel Xeon Phi 7120P, 61 cores × 4 hardware threads at 1.238 GHz.
Peak, from 61 cores × 1.238 GHz × 32 FLOP/cycle: **FP32 ≈ 2416 GFLOPS,
FP64 ≈ 1208 GFLOPS**.

### 2.1 The ladder (N=2048, 244 threads)

Numbers from one controlled sweep (each configuration run three times, best
taken), so they are comparable to each other. Across sessions the absolute FP32
figure moves by up to 1.5× — see §4.

| Step | FP32 | FP64 | Change | Report |
|---|---|---|---|---|
| naive triple loop, `-O0` | 0.33 | 0.33 | — | — |
| tiled, `-O2 -march=knc` — **not vectorised at all** | 6.39 | 3.89 | blocking only | [N3](N3-t9-gemm-explicit-vectorisation.md) |
| 1. explicit IMCI micro-kernel | 57.4 | 45.4 | real 512-bit FMA | [N3](N3-t9-gemm-explicit-vectorisation.md) |
| 2. untimed warm-up + `schedule(static)` | 104.1 | 61.1 | −0.15 s of fixed cost | [N4](N4-t9-gemm-fixed-overhead.md) |
| 3. pack B into `[j-strip][k][NR]` | 113.9 | 81.1 | B stops striding through 16 MB | [N5](N5-t9-gemm-b-panel-packing.md) |
| 4. pad A's row stride by 64 B | 149.2 | 68.1 | **the single biggest win** | [N6](N6-t9-gemm-row-stride-padding.md) |
| 5. j-blocking + tuned register tile | **260.9** | **133.9** | A rows reused across strips | [N7](N7-t9-gemm-jblocking-and-tuning.md) |

### 2.2 Final state

| | FP32 | FP64 |
|---|---|---|
| GFLOPS (N=2048, 244 threads) | 147–261 | 120–138 |
| best of 3 | 260.9 | 133.9 |
| share of peak | 6–11 % | 10–11 % |
| versus T9 baseline (0.33) | 445–790× | 365–420× |
| versus tiled-but-unvectorised (6.39 / 3.89) | 23–41× | 31–35× |

Per-core, which is the quantity that says whether the kernel itself is sound:
**16.34 GFLOPS FP32 on one core** (4 hardware threads), against a per-core peak
of 39.6 GFLOPS — 41 %. Single-threaded it is 4.13 GFLOPS.

### 2.3 By problem size

| N | FP32 | FP64 |
|---|---|---|
| 256 | 3.0–3.6 | 3.1–4.2 |
| 1024 | 39–55 | 28–88 |
| 2048 | 147–261 | 120–138 |

Small N is limited by *parallel granularity*, not by core efficiency: the work
falls as N³ while the micro-kernel count falls as N², so at N=256 there is not
enough work to fill 244 threads. This is a property of the benchmark, not of
the kernel.

## 3. How the numbers are checked

Performance figures are only quoted next to a correctness check, and the check
is stronger than "a result came back":

1. **Disassembly gate.** The script refuses to call a build good unless the
   card-side binary contains `vfmadd` (> 0) **and zero `xmm`/`ymm` operands**.
   KNC has no 128-/256-bit registers, so a single xmm operand in the output
   means the card will fault; and `vfmadd == 0` means it silently fell back to
   scalar code — which is exactly how the 6.39 GFLOPS version looked "optimised"
   while doing nothing. Measured values for the current build: 1199 instructions,
   `zmm` 80, `vfmadd` 12, `xmm/ymm` **0**.
2. **Two independent host references.** The card returns the 16 vector lanes of
   the *sum of all elements of C* (it cannot do a scalar reduction without
   emitting forbidden instructions — see N2). The host recomputes that same
   scalar in `O(N²)` via `sum_ij C[i][j] = sum_i sum_k A[i][k] · rowsumB[k]`,
   and for N ≤ 512 it *additionally* computes C in full `O(N³)` to prove the
   identity itself is right. Relative error: FP32 ≈ 1e-4, FP64 = 0 (bit-exact).
3. **Non-degenerate test data.** `A[i][k]` and `B[k][j]` vary with *both*
   indices. An earlier pattern was a function of the flat index `i*N+k` only;
   because N is a multiple of 64, that made every row of A identical, so a
   transposed or off-by-one row index would have passed. The current pattern is
   `A[i][k] = 1 + ((k + 16·(i mod 4)) mod 64)/64` and a reversed variant for B.

## 4. Caveats, stated up front

- **Run-to-run variance on FP32 is large** — up to 1.8× between sessions
  (147–261 GFLOPS for identical code and parameters). Quote the range, or the
  script's best-of-three; a single fast run is not evidence of a stable rate.
  FP64 is much steadier (120–138).
- **Read the low end as honestly as the high end.** The 147 GFLOPS figure is not
  an outlier to be discarded: it was recorded by `t9_gemm_vec.sh` itself, on the
  same build and parameters that produced 261, on a host that was busy with
  other work. Eight independent runs of the final configuration span 147, 172,
  219, 232, 242, 261 and two in between. If a number from this kernel matters to
  a decision, take the best of at least three on an otherwise idle machine and
  say so; do not quote a single run, in either direction.
- **The step-to-step directions are the durable result.** Every one of the five
  steps moved the number the same way in every session in which it was measured;
  only the magnitudes move. That is what makes the *rules* in N2–N7 worth
  carrying to another program, and it is why the series reports the ladder at
  all rather than just the final figure.
- **These are intrinsic-kernel numbers, not BLAS numbers.** A vendor-tuned
  `dgemm` would be several times faster again. What is demonstrated here is the
  toolchain recipe and the memory-layout lessons, which is what was missing.
- **The machine is shared.** Measurements were taken on a working host with the
  card otherwise idle, but without pinning or frequency control. The
  *directions* of the five steps were each reproducible across sessions; the
  exact percentages are not.

## 5. Reproducing

```bash
cd release/tests
bash t9_gemm_vec.sh                          # N=256/1024/2048, 244 threads
TIERS='2048' THREADS=244 bash t9_gemm_vec.sh
MR32=16 JSB32=32 bash t9_gemm_vec.sh         # sweep one parameter
bash t9_gemm_bench.sh                        # the 0.33 GFLOPS baseline, for contrast
```

Per-tier logs (with the card's cold and warm timings) land in
`release/tests/logs/run-<timestamp>/T9V-*.log`.

## 6. Related documents

- T9 baseline description and usage: `release/tests/README_T9.md`
- Acceptance suite as a whole: `release/tests/README.md`, Appendix [J](J-acceptance-tests.md)
- COI and OpenMP on this port: Appendix [F](F-coi-and-openmp.md), [H](H-offload-field-notes.md)
- What the ISA manual does and does not say: Appendix [D](D-manual-crosscheck.md)
