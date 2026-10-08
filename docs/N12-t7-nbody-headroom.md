# Appendix N12 — T7 N-Body: Where the Remaining 90 % Is

T7 runs at a median ≈105 GFLOPS at N ≥ 65536 — about 35× the scalar `-O0`
implementation on identical inputs, and **8.7 %** of the card's ≈1208 GFLOPS FP64
peak. This report is the answer to "is there anything left", and it is written
from measurement: four separate attempts to go faster did not, and one
structural change that should work has not been attempted.

## 1. The cycle accounting, done correctly

An earlier draft of this report got this wrong by a factor of two — it divided
the cycle budget by the wrong iteration count and concluded the kernel was
running at 34 % of its issue bound. The corrected accounting, using the inner
`j` loop extracted from the built object:

| | per iteration (16 pairs) |
|---|---|
| total instructions in the loop | **119** |
| of which FP-pipe ops (FMA / mul / sub / add / cmp / getexp / getmant) | **78** |
| `vbroadcastsd` loads | 4 |
| `vprefetch0` | 4 |
| `vmovapd` register moves | 7 |
| loop overhead | 8 |

Back-solving cycles at both large tiers:

| | tier 5 (N=65536) | tier 6 (N=131072) |
|---|---|---|
| vector iterations per core | 1.76 × 10⁷ | 7.05 × 10⁷ |
| cycles per core | 2.03 × 10⁹ | 7.95 × 10⁹ |
| **cycles per iteration** | **115** | **113** |
| **instructions per cycle** | **1.04** | **1.05** |

So the kernel issues ~1.05 instructions per cycle, and the FP pipe — one 512-bit
FP64 operation per cycle on this core — is busy 78 of those 113 cycles. **It runs
at 69 % of its FP-issue bound**, which for an in-order core with no
out-of-order window is close to the practical ceiling for this instruction mix.

The model makes a falsifiable prediction, and it holds: deleting the reciprocal-
square-root removes 56 of the 78 FP ops (2 blocks × 28), leaving 22, and should
therefore be worth ~3.5×. Measured at tier 6: **289.8 GFLOPS against 109.7 — 2.6×.**
The shortfall from 3.5× is the non-FP instructions, which do not go away.

## 2. Four things that did not work

Every one of these was a reasonable idea. All were measured; none moved the
number outside noise.

| Attempt | Result at tier 5/6 | Verdict |
|---|---|---|
| `vprefetch0` on the four load streams, distances 0/4/8/16/32/64 | +2.3 % at distance 16, nothing else | small, kept |
| aligned 8-wide `_mm512_load_pd` instead of the four broadcasts | 92.7 vs 98.4 | **slower** |
| degree-9 polynomial in the exponent, no compares, no masks | 99.84 vs 98.81 | a wash |
| **manual lockstep interleaving of the two `rsqrt` chains** | 106.8 / 110.0 vs 95.4–109.3 | **a wash** |

The lockstep one is worth explaining, because the reasoning behind it was sound
and the disassembly confirmed the premise. GCC inlines the two `rsqrt_pd` calls
into sequences that are **not interleaved**: in the inner loop, block 0's
`vgetexppd` sits ~30 instructions before block 1's, with block 0's entire
dependency chain in between. On an in-order core that means the second block
cannot issue until the first has finished stalling — which explains why the
two-block unroll bought only +4 %.

So `rsqrt_pd_2` was written by hand to alternate the two chains instruction by
instruction. It did not help. **The premise was true but irrelevant**: §1 shows
the kernel is uniformly FP-issue bound at ~1.05 IPC, not chain-latency bound, so
there was no stall for the extra independence to fill. GCC also re-separated
some of the pairs anyway, which is a reminder that the scheduler is not obliged
to respect the source's ordering of independent operations.

## 2b. Architecture-level knobs: one measured flat, one measured wrong

### The thread-count sweep was invalid, and the correction matters

The first version of this section reported that threads-per-core and thread
affinity were both flat. **Those measurements were meaningless.** The host
launches the card-side process with `COIProcessCreateFromFile`, and **the host's
environment does not cross that boundary** — so `OMP_NUM_THREADS=61`, `=122`,
`=183` and `=244` all ran the same 240-thread configuration. Verifying the
kernel's own reported thread count exposes it immediately:

| intended | kernel reported |
|---|---|
| `OMP_NUM_THREADS=61` | **240** |
| `OMP_NUM_THREADS=122` | **240** |
| `OMP_NUM_THREADS=183` | **240** |
| `OMP_NUM_THREADS=244` | **240** |

The fix is to pass the count as a **parameter** and call `omp_set_num_threads()`
on the card. `Params` gained an `nthreads` field, fed from `T7_THREADS` on the
host. Verified: `T7_THREADS=61 → 61`, `=183 → 183`, `=244 → 244`.

> **Rule.** On an offload target, an environment variable set on the host is not
> a parameter. If the measurement does not confirm that the setting took effect,
> it did not.

### The real sweep

MPairs/s, best-of-7, per tier:

| `T7_THREADS` | threads/core | tier 3 | tier 5 | tier 6 |
|---|---|---|---|---|
| 61 | 1.0 | 2257.81 | 2403.27 | 2490.88 |
| 122 | 2.0 | 2974.41 | 3686.00 | 4005.63 |
| 183 | 3.0 | 3559.64 | 4399.92 | 4721.80 |
| 220 | 3.6 | **3983.79** | 5020.38 | 5172.21 |
| **240** | 3.9 | 3502.82 | **5354.48** | **5580.94** |
| 244 | 4.0 | 2895.88 | 4479.18 | 5001.22 |

Three things follow, and the first two contradict what this report said before.

1. **Thread count matters a great deal.** Going from 1 to 4 threads per core more
   than doubles throughput (2491 → 5581 MPairs/s at tier 6). SMT is worth ≈2.2×
   here, so the earlier claim that "a single thread already saturates the core"
   was an artefact of the broken sweep.
2. **Over-subscribing is worse than leaving a core idle.** 244 threads — every
   hardware thread on the card — is **10 % slower at tier 6 than 240**, which is
   four threads on 60 of the 61 cores. Something else needs to run: the COI
   daemon and the kernel's own work. The library default already picks 240, so
   the shipped configuration is the good one.
3. **Small tiers want fewer threads.** Tier 3 peaks at 220 (+14 % over 240),
   because its work — 2048 vector blocks — divides more evenly without the last
   core's threads. That tier is fixed-cost dominated anyway, so it does not
   change any conclusion, but it is why a per-tier optimum exists at all.

`T7_THREADS` is now a supported knob (`T7_THREADS=220 bash t7_nbody.sh`).

### Thread affinity: still unmeasured

`GOMP_CPU_AFFINITY` and `OMP_PROC_BIND` are also host environment variables, so
they never reached the card either, and the earlier "flat" reading for affinity
is not evidence of anything. Binding threads on the card would need
`sched_setaffinity` calls inside the sink. It is untested, and given that 240
threads — a plain `schedule(static)` over the blocks — already lands within 10 %
of the best number found, it is not obviously worth the plumbing.

### What remains genuinely flat

The compile-time knobs were real measurements, because they change the emitted
code rather than the runtime environment:

| Lever | Range tested | Result |
|---|---|---|
| code alignment (paired-instruction decode window) | `-falign-loops` 16/32/64/128, `-falign-functions=64` | 111.2–112.0 GFLOPS — flat |
| prefetch hint | `vprefetch0` / `1` / `2` / `NTA`, distance 16 | 111.0–111.9 — flat |
| prefetch distance | 0 / 4 / 8 / 16 / 32 / 64 | +2.3 % at 16, nothing else |

### The one architectural feature that would help is toolchain-blocked

KNC's MVEX encoding supports an **embedded broadcast**: the memory operand of an
arithmetic instruction can be a single element replicated across the vector,
written `{1to8}` for float64. The ISA manual documents it (`VMULPD`'s operand is
`Sf64(zmm3/mt)`; MVEX table: `001 [rax] {1to8} 8`), and it would fold all four
`vbroadcastsd` instructions in the inner loop into the arithmetic that consumes
them.

It is not reachable with this toolchain:

| | result |
|---|---|
| GCC emitting it | never — `_mm512_fmadd_pd(_mm512_set1_pd(p[3]), a, a)` produces `vbroadcastsd` + `vfmadd132pd` |
| GNU as accepting it | `vfmadd213pd (%rax){1to8}, %zmm1, %zmm0` → ``Error: junk `1to8' after expression`` |
| same for `vmulpd`, `vsubpd` | rejected |

One more thing the manual settles, which [N11](N11-t7-two-units-and-three-traps.md)
§4 had inferred from behaviour:

> A given vector instruction's 8-bit displacement is **always multiplied by the
> total number of bytes of memory the instruction accesses**, which can mean
> multiplication by 64, 32, 16, 8, 4, 2 or 1, depending on any broadcast and/or
> data conversion in effect. … **The use of disp8*N … shrink[s] the required size
> of the paired-instruction decode window by 3 bytes.**

That is the `vpackstorelpd` mechanism, stated by the vendor: the displacement
scales with the *element* size, so the same bytes decode to different addresses
depending on the operand form — exactly what made the `%rbp`-relative encoding
work and the `0x10(%r12)` one fault.

## 2c. The embedded broadcast is reachable — and slower

KNC's MVEX encoding allows an arithmetic instruction's memory operand to be a
**single element replicated across the vector**, written `{1to8}` for float64
(§2b). It was recorded there as toolchain-blocked. **That was wrong**, and the
mistake is instructive:

| attempt | result |
|---|---|
| standalone `.s` file | **accepted** — `vmulpd (%rax){1to8}, %zmm2, %zmm1` encodes with MVEX prefix byte `0x18`, against `0x08` for the plain form |
| inline asm, written `(%2){1to8}` | rejected: ``junk `1to8' after expression`` |
| **the generated `.s` shows why** | GCC's asm template **eats the braces**: it emitted `vmulpd (%rdi)1to8, %zmm0, %zmm0` |
| inline asm, written `(%2)%{1to8%}` | **accepted** — emits `vmulpd (%rdi){1to8}, %zmm0, %zmm0` |

`%{` and `%}` are GCC's *dialect alternative* delimiters, which is why bare
braces disappear. With the escape, eight `{1to8}` instructions appear in the
compiled kernel and all four `vbroadcastsd` disappear from the two-block loop.

One constraint shapes how it can be used: the MVEX memory operand can only sit in
Intel's last source slot, so `VSUBPD` can compute `a − broadcast(*p)` but not
`broadcast(*p) − a`. Since the kernel needs `dx = x[j] − x[i]`, the broadcast form
yields `x[i] − x[j]` and the sign is recovered by swapping the accumulation from
`vfmadd` to `vfnmadd`. **Sign flip is exact in IEEE, and `fnmadd(a,b,c)` is a
single rounding of `−(a·b)+c`, so the result is bit-identical** — confirmed by the
force self-test (6.340e-16) and the end-to-end checksum, both unchanged.

And it is slower:

| `NB_BCAST_FOLD` | tier 6, best-of-3 | force self-test | checksum |
|---|---|---|---|
| 0 — separate `vbroadcastsd` | **5567.46 MPairs/s** | 6.340e-16 pass | match |
| 1 — embedded `{1to8}` | 5204.68 MPairs/s | 6.340e-16 pass | match |

**−6.5 %, and correct.** Plausible reasons: the MVEX form carries an extra prefix
byte plus a displacement, so the instructions are longer; and a
broadcast-from-memory operand may occupy the load port differently from a
separate `vbroadcastsd` feeding a register operand. The feature is real, it is
now reachable, and on this kernel it does not pay. The switch stays in the source
at `NB_BCAST_FOLD`, default 0.

## 3. Why the FP-op count is near its floor

Per `i` block, the FP ops are:

| | ops |
|---|---|
| `r² = dx²+dy²+dz²+soft2` | 3 |
| `1/sqrt` — exponent/mantissa, 4-step masked scaling, degree-5 polynomial, 2 Newton iterations | **28** |
| `y³ · m` | 3 |
| accumulate into 3 axes | 3 |
| **total** | **37** |

The reciprocal-square-root is 76 % of it, and it resists compression:

- **Two Newton iterations are required.** The degree-5 mantissa polynomial gives
  1.17e-5; one iteration reaches 2.1e-10, which fails the 1e-14 force-kernel
  tolerance. Reaching 1e-14 with one iteration needs a degree-10 polynomial,
  which costs more than the iteration it saves.
- **The four scaling steps are required.** The exponent range `e ∈ [-12, 4]`
  needs four bits; three steps would only cover `e ∈ [-8, 0]`, which does not
  contain the actual range.
- **The mask-free formulation is not cheaper.** Degree-9 `P(e) ≈ 2^(-e/2)`
  replaces 4 compares + 4 masked multiplies + 4 masked subtracts (12 ops) with
  9 FMAs — three fewer, and measured identical.

KNC has no FP64 reciprocal-square-root to fall back on
([N10](N10-t7-gemm-fp64-no-transcendentals.md)), so 28 operations for a 1-ulp
result is what the arithmetic costs.

## 4. Symmetry: the lever that arithmetic says should work and the ISA forbids

The obvious remaining structural change is Newton's third law: compute each
pair's force once and apply `+f` to one body and `−f` to the other. The
arithmetic is genuinely attractive — counting reciprocal-square-root evaluations
per **physics** pair (a distinction an earlier draft of this report got wrong),
the current kernel spends one per 4 pairs and a symmetric one would spend one per
8. That is a 10 → 5.5 FP-op-per-physics-pair reduction, or ~1.8×.

**It cannot be expressed on this hardware.** The reason is structural, not a
matter of clever coding.

The current kernel vectorises over `i` and broadcasts `j`, so the accumulator
lives in the vector lanes — the natural place. Symmetry requires the opposite:
for a block pair `(I,J)`, the force on `J` must be accumulated onto the
**broadcast** index while the force on `I` goes to the vectorised one. One of the
two accumulations therefore needs a **cross-lane reduction** — an 8-lane sum —
for every `i` or every `j`.

KNC has no instruction that can do it:

| intrinsic | assembler verdict |
|---|---|
| `_mm512_permute_pd`, `_mm512_mask_permute_pd` | ``vpermilpd' is not supported on `k1om'`` |
| `_mm512_shuffle_pd` | ``vshufpd' is not supported on `k1om'`` |
| `_mm512_shuffle_f64x2` | `no such instruction: vshuff64x2` |
| `_mm512_permutex_epi64`, `_mm512_permutexvar_epi64` | ``vpermq' is not supported on `k1om'`` |
| `_mm512_alignr_epi64`, `_mm512_shuffle_i64x2` | `no such instruction` |
| `_mm512_swizzle_pd` (KNC MVEX swizzle) | not declared — the toolchain headers ship no `_MM_SWIZ_*` constants |

**There is no way to move data between the 512-bit lanes of an FP64 vector** —
not with floating-point shuffles, not with integer permutes, not with the MVEX
swizzle. The only cross-lane mechanisms on KNC are the swizzle/broadcast encoded
*inside a load operand* (which reaches only `{1to8}`, not an arbitrary
permutation) and a memory round-trip.

The memory round-trip is what is left, and it costs more than it saves. Reducing
one 8-lane vector to a scalar requires a 64-byte store, an aliasing barrier, then
eight broadcast-loads and eight adds — **≈18 vector instructions per component,
~54 for the three axes**, charged once per `i` per `j`-block. Adding that to the
per-pair cost:

| j-tile width | ops per physics pair | vs current (10) |
|---|---|---|
| 1 block (8 j) | 13.1 | **0.76× — slower** |
| 2 blocks | 9.6 | 1.04× |
| 4 blocks | 7.8 | 1.28× |
| 8 blocks | 6.9 | 1.45× |

Wider tiles need more register-resident state (j-blocks × 4 arrays, plus the
partner accumulators), and beyond two blocks the kernel spills. **The realistic
landing zone is 0.8–1.0×, i.e. no win.**

> **Rule.** Symmetry in N-body is a scatter, and a scatter needs a transpose. On
> a SIMD ISA without lane shuffles, price the transpose before counting the
> saved square roots. Here it costs ~54 instructions per 64 pairs — against a
> saving of 28 per 64 pairs.

This is also why the earlier draft's "≈1.5–1.8×" was wrong: it counted the saved
rsqrt and never asked how the partner's force would get back onto the right
lanes.

## 5. What is not worth doing

- **More compiler flags.** `-O2`, `-O3`, `-O3 -funroll-loops` are flat —
  52.02 / 51.34 / 48.63 at tier 3.
- **More unrolling over `i`.** The two-block unroll gave +4 %, and the three
  attempts to make its second block useful all failed; a four-block unroll would
  multiply register pressure for the same reason.
- **Chasing the non-FP instructions.** 41 of 119 instructions are non-FP, worth
  ~31 % of the cycles at most, and three targeted attempts produced nothing.
- **A vendor library.** None exists for k1om.
- **Further transcendental work.** §3 is the evidence that 28 ops is the floor
  for 1-ulp accuracy.

## 6. Summary

| question | answer |
|---|---|
| Is the kernel well optimised for its instruction mix? | **Yes** — 1.05 IPC, 69 % of the FP-issue bound |
| Is the FP-op count reducible? | **No** — the rsqrt is at its arithmetic floor for 1 ulp |
| Is the non-FP overhead removable? | **Three attempts say no** |
| Can architecture-level tuning help? | **Mostly no** — alignment and prefetch hints are flat; **thread count is not** (240 threads is 10 % better than 244, §2b), and affinity is still unmeasured |
| Is there a big lever left? | **No** — symmetry, the only candidate, needs a cross-lane reduction the ISA cannot do (§4) |

## 7. Reproducing

```bash
cd release/tests
T7_REF=1 bash t7_nbody.sh                    # six tiers, full verification
# inner-loop composition:
$K1OM_OBJDUMP -d logs/run-*/nbody_vec.o | sed -n '/accel._omp_fn.0>:/,/pot_sum._omp_fn.1>:/p'
```

The ablation expressions are one-line substitutions on a copy of
`src/nbody_vec.cpp`; each is quoted in §1 and §2.

## 8. Related

- The transcendental being measured: [N10](N10-t7-gemm-fp64-no-transcendentals.md)
- The verification that must survive any of this: [N13](N13-t7-verification.md)
- Results and the corrected scaling curve: [N9](N9-t7-nbody-overview.md)
