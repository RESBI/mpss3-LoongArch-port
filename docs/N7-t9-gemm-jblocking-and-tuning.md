# Appendix N7 — T9 GEMM Step 5: j-Blocking and Register-Tile Tuning

**Measured effect: FP32 149.2 → 260.9 GFLOPS, FP64 68.1 → 133.9 GFLOPS**
(N=2048, 244 threads; best of three). Two independent changes, both found by
parameter sweep rather than by reasoning — which is itself the lesson.

## 1. The remaining traffic

After [N6](N6-t9-gemm-row-stride-padding.md) the per-core problem was fixed, and
the next limit showed up as imperfect scaling across cores. Counting the traffic
explains it. The micro-kernel re-reads its `MR` rows of A **in full** for every
j strip it processes:

```
A traffic = njs × (N/MR) × (MR × N × 4 bytes)
          = 128 × 256 × (8 × 2048 × 4)
          ≈ 2.16 GB          at N = 2048, MR = 8
```

against about 16 MB for B once packed ([N5](N5-t9-gemm-b-panel-packing.md)). A is
the whole memory story, and it is re-read 128 times — once per j strip.

## 2. Change A: process several j strips per pass over A

Invert the inner nesting: keep `i0` fixed and sweep a run of `jsb` consecutive j
strips before moving to the next `i0`. The A rows then stay resident in L1/L2
across those `jsb` strips, and A traffic divides by `jsb`:

```c
#pragma omp parallel for collapse(2) schedule(static) reduction(+:t)
for (uint32_t jb = 0; jb < njsb; jb++)            /* njsb = njs / jsb */
    for (uint32_t i0 = 0; i0 < N; i0 += MR32) {
        const uint32_t js0 = jb * jsb;
        for (uint32_t js = js0; js < js0 + jsb; js++) {
            micro32p(A, lda, Bp, N, C, N, i0, js);
            t++;
        }
    }
```

Parallel tasks remain `(njs/jsb) × (N/MR)`, which at `jsb=32` is 1024 tasks for
N=2048 — ample for 244 threads.

**Two constraints this loop shape has to respect on KNC:**

- **No `min()`, no ternary.** The natural way to write the inner bound is
  `js1 = (js0 + jsb < njs) ? js0 + jsb : njs`, and that is fatal: it compiles to
  `cmova`, which the k1om assembler rejects
  ([N2 Constraint 4](N2-t9-gemm-isa-constraints.md)). The bound is therefore
  written as `js0 + jsb` with no clamp, and safety comes from divisibility —
  `pick_jsb()` only ever returns a value that divides `njs`.
- **`jsb` must vary with N.** A fixed `jsb` that divides 2048 does not divide
  256, so the kernel would refuse to run at small N. `pick_jsb()` picks the
  largest power-of-two `jsb` that divides `njs` **and** still leaves at least
  four j blocks and 128 tasks:

```c
static uint32_t pick_jsb(uint32_t N, uint32_t NR, uint32_t maxjsb, uint32_t MR)
{
    const uint32_t njs = N / NR;
    for (uint32_t j = maxjsb; j > 1; j >>= 1) {
        if ((njs % j) != 0) continue;
        if ((njs / j) < 4) continue;
        if ((N / MR) * (njs / j) < 128) continue;
        return j;
    }
    return 1;
}
```

  The "at least four j blocks" floor was added after measurement: with only two
  blocks, N=1024 produces 256 tasks for 244 threads — a single wave, where the
  tail imbalance costs more than the extra A reuse saves. It was worth roughly
  2× at that size.

## 3. Change B: make the register tile *smaller*

This is the counter-intuitive result. The standard argument says a larger `MR`
is better, because B is loaded once per k step and reused `MR` times, so bytes
per FLOP fall as `(MR·4 + 64) / (MR·32)`. Sweeping it says otherwise:

| JSB32 | MR32 | JSB64 | MR64 | FP32 | FP64 |
|---|---|---|---|---|---|
| 32 | 8 | 16 | 4 | **260.9** | **133.9** |
| 32 | 8 | 8 | 4 | 236.3 | 130.7 |
| 32 | 8 | 8 | 8 | 221.9 | **70.3** |
| 32 | 4 | 8 | 4 | 227.1 | 129.5 |
| 16 | 8 | 8 | 4 | 223.4 | 130.6 |
| 8 | 8 | 8 | 4 | 206.1 | 133.4 |
| 32 | 16 | 4 | 8 | 183.9 | 69.6 |

`MR64 = 8` versus `MR64 = 4` is the starkest: **70.3 against 133.9 GFLOPS**, a
factor of 1.9, on a change that *reduces* arithmetic intensity. The explanation
is visible in the [N3](N3-t9-gemm-explicit-vectorisation.md) disassembly: at
`MR32 = 8` the loop already reloads five A row pointers from the stack every
iteration, so the register file is at its limit. Eight FP64 accumulators on top
of the broadcast, the B vector and the address registers spills, and spilling in
an in-order core is far more expensive than the B reuse it buys.

Interpretation: **on KNC the register file, not arithmetic intensity, is the
binding constraint at this tile size.** The shipped defaults are
`JSB32=32, MR32=8, JSB64=16, MR64=4`; all four are `-D`-overridable so the sweep
can be repeated on other hardware.

Note also that `MR64 = 3` returns FP64 = 0 rather than a wrong answer: N=2048 is
not divisible by 3, and the validation in `GemmVecRun` rejects the configuration
instead of computing garbage. That is the divisibility contract from §2 doing
its job.

## 4. Result

| | step 4 | step 5 |
|---|---|---|
| FP32, N=2048 | 149.2 | **260.9** |
| FP64, N=2048 | 68.1 | **133.9** |
| share of peak | 6.2 % / 5.6 % | **10.8 % / 11.1 %** |

Two things worth noting about this step. First, it is where FP64 caught up:
before it, FP64 sat at 5.6 % of its peak while FP32 was at 6.2 %, and after it
both land at ~11 %, i.e. the kernel became balanced across precisions. Second,
neither half was predicted — the j-blocking was reasoned out from the traffic
count, but the size and even the *sign* of the `MR` effect were not. The tuning
table took one afternoon and is the highest-return artefact in this series.

## 5. Rule

> When one operand is re-read once per strip of the other, invert the nesting
> and sweep several strips per pass; divide the traffic by the sweep length.
> Do this with a run-time-chosen, divisibility-guaranteed block size, and
> without `min()` — on KNC a clamp is an assembler error, not a slowdown.
>
> Then **sweep the register tile rather than reasoning about it.** The textbook
> argument optimises bytes per FLOP and ignores the register file; on an
> in-order core with 32 vector registers, spilling beats arithmetic intensity
> every time. Shrinking `MR64` from 8 to 4 nearly doubled FP64.
