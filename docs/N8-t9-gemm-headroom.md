# Appendix N8 — T9 GEMM: What Is Left, and the Next Order of Magnitude

The kernel reached 6–11 % of peak ([N1](N1-t9-gemm-optimization-overview.md)).
This report states where the remaining 90 % is, which of it is reachable, and
the concrete next move — including the one the ISA manual points at directly.

## 1. Where the time actually goes now

The inner loop is not the problem. At `MR32 = 8` it is 8 `vbroadcastss` plus
8 `vfmadd231ps` plus one B load and loop overhead — 16 vector instructions per
8 FMAs, which is the arithmetic the hardware wants
([N3](N3-t9-gemm-explicit-vectorisation.md)).

Re-deriving the budget at the current operating point, N=2048, 244 threads:

| | |
|---|---|
| total FLOPs | 2·2048³ = 1.718 × 10¹⁰ |
| measured time (FP32, 261 GFLOPS best) | 0.066 s |
| issue-bound time at 16 FMA / 21 cycles, 61 cores | ~0.009 s |
| DRAM traffic implied by the loop structure | ≈ 2 GB |
| bandwidth that traffic would need in 0.066 s | ≈ 30 GB/s |

The card's DRAM path is on the order of 350 GB/s. So **neither issue slots nor
DRAM bandwidth are the binding constraint** — the kernel is short of
memory-level parallelism. The evidence is the thread sweep in
[N6](N6-t9-gemm-row-stride-padding.md): perfectly linear from 1 to 4 threads
(latency-bound), then flattening well before the core count is exhausted. There
is enough bandwidth; there are not enough independent outstanding loads per
thread to use it.

That framing matters, because it says the next win is *more concurrency per
thread*, not less traffic. The three items below are ordered by that criterion,
not by the usual GEMM playbook.

## 2. Next move: FMA with an embedded broadcast (the manual points at this)

The ISA manual gives `vfmadd231ps` the form

```
VFMADD231PS zmm1 {k1}, zmm2, Sf32(zmm3/mt)      S2S1S0 = 001
   001  broadcast 1 element (x16)   [rax] {1to16}   disp8*N = 4
```

i.e. the A scalar can be broadcast **inside the FMA**, reading it straight from
memory. That collapses each `vbroadcastss` + `vfmadd231ps` pair into one
instruction:

```
now:  vbroadcastss (%rdx),%zmm17      ; load + broadcast
      vfmadd231ps  %zmm0,%zmm17,%zmm16

with: vfmadd231ps  %zmm0, (%rdx){1to16}, %zmm16
```

Why this is the right next step rather than merely a smaller loop:

- It **halves the instruction count** in the hot loop (8 + 8 → 8), which raises
  the FMA issue rate toward the 1-per-cycle ceiling.
- It **frees a vector register** (`zmm17`, the broadcast temporary), which the
  register-pressure analysis in [N7](N7-t9-gemm-jblocking-and-tuning.md) says is
  the binding constraint — and that in turn may make a larger `MR` viable again,
  which would *also* cut A traffic.
- It is the one form the compiler will not generate. Measured: GCC 5.1 emits the
  two-instruction sequence for `_mm512_fmadd_ps(_mm512_set1_ps(mem), b, c)`
  every time. This needs an inline-assembly micro-kernel.

That is the trade: an assembly kernel with an `MR`-deep unrolled body, versus the
current intrinsic kernel. The rest of the file — packing, padding, `pick_jsb`,
the host driver, the verification — is unaffected.

## 3. Second: pack A as well

B was packed in [N5](N5-t9-gemm-b-panel-packing.md); A was not, and A is now the
dominant traffic. A is read as `MR` parallel streams, each sequential, 8 KB per
row per strip. Packing A into `[k-block][i][MR]` would turn the `MR` scalar
broadcasts per k step into **one contiguous 64-byte load** — a second vector,
one broadcast-free FMA per row, and one stream instead of `MR`.

The cost is the same shape as B's packing: O(N²) bytes moved once per pass. The
reason it is second and not first is that it interacts with §2 — packing A only
pays off if the broadcast is cheap, which is exactly what the embedded form
makes it.

## 4. Third: three-level blocking (MC / KC / NC)

Analytically this is the big one: proper `MC × KC` and `KC × NC` panels with
packing bring DRAM traffic from ~2 GB to a few hundred MB, because A and B stop
being re-read per strip. For N=2048 the numbers are roughly

| blocking | A traffic | B traffic | C traffic | total |
|---|---|---|---|---|
| current | ~2.1 GB | 16 MB | 32 MB | ≈ 2.1 GB |
| MC=NC=256, KC=256 | 128 MB | 128 MB | 256 MB | ≈ 0.5 GB |

But §1 says bandwidth is not the binding constraint: 2 GB in 0.066 s is 30 GB/s
against a ~350 GB/s path. So this is a 4× reduction in a resource that is at
~9 % utilisation, and it will only pay once §2 and §3 have made the kernel
issue- or latency-limited *on the compute side*. Treat the table as a reason to
do it eventually, not as a predicted speedup — an early attempt at k-blocking in
this work produced no measurable gain for exactly this reason.

## 5. What not to bother with

- **Bigger `MR` on the current code.** Measured and negative
  ([N7](N7-t9-gemm-jblocking-and-tuning.md)).
- **`-O3`, `-funroll-loops`, `-ffast-math`.** The k1om backend produces the same
  loop; the unrolling that matters is already forced by template recursion.
- **Chasing the N=256/1024 numbers.** Those are parallel-granularity limits, not
  kernel limits (§2.3 of [N1](N1-t9-gemm-optimization-overview.md)). A larger N
  is the fix, and N=2048 is already the largest the acceptance test runs.
- **Vendor BLAS.** If a k1om MKL were available it would win outright, and that
  remains the honest answer for production use. The value of this series is the
  toolchain recipe and the memory-layout findings, which apply to any code on
  this part, BLAS or not.

## 6. Ceiling estimate

For calibration, not as a promise. A hand-written KNC GEMM kernel that packs both
operands, uses the embedded-broadcast FMA, and blocks all three levels can
realistically reach 40–60 % of this part's peak on a square problem that fits in
memory — roughly 1000–1400 GFLOPS FP32 here. The gap from 261 to that is
entirely in §2–§4, and §2 is the piece the ISA manual hands over directly.

## 7. Reproducing the current state

```bash
cd release/tests
bash t9_gemm_vec.sh                       # 8 checks, gates on vfmadd/xmm
TIERS='2048' THREADS=244 bash t9_gemm_vec.sh
MR32=16 JSB32=32 bash t9_gemm_vec.sh      # re-run the N7 sweep
```
