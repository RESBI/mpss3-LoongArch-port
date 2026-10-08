# Appendix N5 — T9 GEMM Step 3: Packing the B Panel

**Measured effect: FP32 104.1 → 113.9 GFLOPS, FP64 61.1 → 81.1 GFLOPS**
(N=2048, 244 threads; FP64 gained 33 %). A layout change costing O(N²), applied
once before timing.

## 1. The access pattern

The micro-kernel holds a strip of `NR` columns of B fixed and walks down k:

```c
__m512 b = _mm512_load_ps(B + (size_t)k * ldb + j0);   /* ldb = N */
```

With N=2048 that is one 64-byte line every **8192 bytes**, N times. Each line is
fetched in full and used in full — the *utilisation* is perfect. What is not
perfect is the address sequence:

- **Page changes.** Card pages on this port are 4 KiB, so a B row (8 KiB) spans
  two pages and the walk crosses a page boundary **every second k iteration**.
  N=2048 iterations means ~1024 page crossings per micro-kernel, times 32768
  micro-kernels.
- **Prefetcher defeat.** The hardware prefetcher is looking for sequential or
  small-stride progress. A stride of 8192 bytes that touches one line per 128 is
  indistinguishable from random to it.
- **Working-set spread.** The data the micro-kernel needs is scattered across
  the full 16 MB of B rather than concentrated, which is what makes the TLB
  pressure in the first point cost anything.

## 2. The transform

Restamp B from row-major `[k][j]` into `[j-strip][k][NR]`:

```
Bp[js * N * NR + k * NR + 0 .. NR-1]  =  B[k * N + js * NR .. +NR-1]
```

The inner walk becomes `Bp + k*NR` — a **contiguous 64-byte stride**, one full
cache line per k iteration, addresses advancing linearly. Same number of bytes,
same number of lines, no element-level permutation.

It is worth being explicit that **no transpose happens**: the 16 floats of a
strip stay in their original order. The transform only changes which order the
strips' k-slices are laid out in. That keeps it to an aligned 64-byte copy, no
shuffles:

```c
static void pack32(const float * __restrict__ B, float * __restrict__ Bp, uint32_t N)
{
    const uint32_t njs = N / NR32;
    for (uint32_t js = 0; js < njs; js++) {
        float *dst = Pc + (size_t)js * N * NR32;
        const float *src = Bc + (size_t)js * NR32;
        for (uint32_t k = 0; k < N; k++)
            _mm512_store_ps(dst + (size_t)k * NR32,
                            _mm512_load_ps(src + (size_t)k * N));
    }
}
```

Cost: N² reads and N² writes — 16 MB + 16 MB at N=2048. At the card's DRAM
rates that is a few milliseconds against a ~100 ms kernel, so ~3 %. It is paid
once, outside the timed region.

## 3. Result

| | before packing | after |
|---|---|---|
| FP32, N=2048 | 104.1 | **113.9** |
| FP64, N=2048 | 61.1 | **81.1** |
| FP64, N=1024 | 46.34 | **56.98** |

Honest reading: **modest, not the breakthrough.** The remaining performance was
still locked in the L1 conflict problem of
[N6](N6-t9-gemm-row-stride-padding.md), which is why this step's gain was
smaller than the analysis above suggests it should be. It is kept in the series
because it is cheap, it is correct, and it prevents B from becoming the
bottleneck once the A-side problem is fixed — the step-4 measurement would
otherwise have been limited by B.

A secondary benefit shows up in the loop order. With the strips laid out
contiguously, `schedule(static)` handing consecutive `i0` blocks to a thread
means several threads read the *same* B strip at roughly the same time, so it is
fetched from DRAM once and served from the private L2s afterwards. This is why
the loop order is `(j-strip, i0)` rather than `(i0, j-strip)` — see
[N7](N7-t9-gemm-jblocking-and-tuning.md), which builds on it.

## 4. Rule

> When a kernel walks one operand along an index whose stride is a large power
> of two, the bytes are not the problem and the *address sequence* is. A
> one-off O(N²) restamp into a contiguous `[outer][inner][vector]` layout costs
> a few percent of the kernel and converts the walk into a linear one — and
> since it is a pure 64-byte block copy, it needs no shuffles and no unaligned
> access, both of which KNC lacks anyway
> ([N2](N2-t9-gemm-isa-constraints.md)).
