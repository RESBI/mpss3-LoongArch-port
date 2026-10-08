# Appendix N3 — T9 GEMM Step 1: An Explicit IMCI Micro-Kernel

**Measured effect: FP32 6.39 → 57.4 GFLOPS, FP64 3.89 → 45.4 GFLOPS**
(N=2048, 244 threads). This is the step that turns the kernel from "tiled" into
"actually vectorised".

## 1. Where this step starts

The intermediate version began from the T9 baseline (naive triple loop, `-O0`,
0.33 GFLOPS) and applied the textbook cache fixes:

- 64×64 blocking;
- loop reordering from `i-j-k` to `i-k-j`, so the inner `j` loop walks B row-wise
  instead of column-wise — this alone removes the worst cache behaviour of the
  baseline;
- `__restrict__` pointers, 64-byte aligned allocation, OpenMP over `(i,j)`
  blocks;
- `#pragma omp simd` on the inner loop.

It reached 6.39 / 3.89 GFLOPS. Its disassembly contained **zero** `vfmadd` —
1199 instructions, 8 zmm mentions, no vector arithmetic at all. Per
[N2 Constraint 1](N2-t9-gemm-isa-constraints.md), no amount of pragmas was going
to change that. So the whole 6.39 GFLOPS came from cache locality, and the
vector unit was idle.

## 2. The micro-kernel

The shape follows the standard register-blocked GEMM: hold an `MR × NR` tile of
C in vector registers, stream the two operands past it, and do one FMA per
accumulator per k step.

- `NR32 = 16` floats / `NR64 = 8` doubles — exactly one zmm.
- `MR` rows of C live in `MR` zmm registers.
- Per k step: **one** 64-byte load of B, `MR` broadcast loads of A scalars,
  `MR` FMAs.

```c
/* N3: the inner loop, FP32 */
for (uint32_t k = 0; k < N; k++) {
    __m512 b = _mm512_load_ps(pb + (size_t)k * NR32);
    UnrollPS<MR32>::fma(c, pa, lda, b, k);
}
```

Two implementation details matter more than they look:

**2.1 The accumulators must be compile-time indexed.** `__m512 c[MR32]` with a
runtime index would be spilled to the stack, which defeats the entire point.
GCC 5 has no `#pragma GCC unroll` (that arrived in GCC 8), so the unrolling is
forced by template recursion — `UnrollPS<R>::fma` calls `UnrollPS<R-1>::fma`
and touches `c[R-1]`, making every index a constant:

```c
template <int R> struct UnrollPS {
    static inline void fma(__m512 *c, const float *pa, uint32_t lda, __m512 b, uint32_t k) {
        UnrollPS<R - 1>::fma(c, pa, lda, b, k);
        c[R - 1] = _mm512_fmadd_ps(_mm512_set1_ps(pa[(size_t)(R - 1) * lda + k]), b, c[R - 1]);
    }
    /* zero() and store() follow the same pattern */
};
```

**2.2 The micro-kernel writes C; it does not accumulate into it.** It computes
the full k range in one pass, so C is stored once and never read. That is what
makes a repeat pass idempotent, which step 2 ([N4](N4-t9-gemm-fixed-overhead.md))
depends on.

## 3. What the compiler actually emitted

Extracted from the built binary with the SDK objdump, the k-loop body of the
FP32 micro-kernel, at `MR32 = 8` (43 instructions including the loop overhead):

```
400d08:  vmovaps (%rcx),%zmm0              # B, one full cache line per k
400d0e:  mov   -0x60(%rbp),%rax
400d12:  vbroadcastss (%rdx),%zmm17        # A[i0+0][k]
400d18:  add   -0x40(%rbp),%rcx
400d1c:  vfmadd231ps %zmm0,%zmm17,%zmm16
400d22:  vbroadcastss (%rdx,%rsi,4),%zmm17
400d29:  vfmadd231ps %zmm0,%zmm17,%zmm15
         ... 6 more broadcast/FMA pairs ...
400deb:  add   $0x4,%rdx
400def:  cmp   %rdx,-0x38(%rbp)
400df3:  vfmadd231ps %zmm0,%zmm17,%zmm1
400df9:  jne   400d08
```

16 vector instructions per 8 FMAs — that is, `MR32` broadcasts and `MR32` FMAs,
the textbook KNC inner loop. The five `mov -0xNN(%rbp),%rax` are A row base
pointers the register allocator parked on the stack (`MR` base registers plus
loop state exceeds the budget); they are L1 hits and cost little, but they are a
hint that the tile is already at the edge — see
[N7](N7-t9-gemm-jblocking-and-tuning.md), where *shrinking* the tile wins.

Instruction mix for the whole object: `vfmadd` 24 (12 FP32 + 12 FP64 at the
shipped MR values), `vmovaps` 54, `vmovapd` 30, `vbroadcastss` 16,
`vbroadcastsd` 8, `zmm` 140, **`xmm`/`ymm` 0**.

## 4. Why it was only 57 GFLOPS out of ~1900 expected

The loop above should be issue-limited to roughly 16 FMAs per 21 cycles, which
on 61 cores is about 1900 GFLOPS. Measured: 57.4. The gap is not instructions —
back-solving from the measurement gives **~1462 cycles per k iteration against
an ideal of ~21**.

Three compounding causes, each fixed in a later step:

1. **Fixed parallel overhead of ~0.15 s** dominated everything at these sizes
   ([N4](N4-t9-gemm-fixed-overhead.md)) — at N=2048 the whole run was 0.30 s.
2. **B stride through a 16 MB address space**: one 64-byte line per k step, 8 KB
   apart, changing page every second iteration
   ([N5](N5-t9-gemm-b-panel-packing.md)).
3. **A's 16 rows all landing in one L1 set**, because the row stride 8192 is a
   power of two ([N6](N6-t9-gemm-row-stride-padding.md)) — the largest of the
   three.

The lesson worth keeping from this step is the *shape* of the diagnosis: a
correct-looking inner loop plus a terrible measured rate means the problem is
outside the loop, in the memory system or in the harness. Counting `vfmadd`
proves the loop; only the cycle budget says whether it is running.

## 5. Rule

> Write the inner loop as a register tile of `MR` accumulators with one
> `vbroadcastss` + one `vfmadd231ps` per row, force the unrolling at compile
> time (template recursion on GCC 5), and check `xmm/ymm == 0`. Then measure
> cycles per k iteration — if it is not within a few times the issue bound, stop
> tuning the loop and go look at the addresses.
