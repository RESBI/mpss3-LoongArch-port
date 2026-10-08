# Appendix N6 — T9 GEMM Step 4: Padding A's Row Stride

**Measured effect: FP32 113.9 → 149.2 GFLOPS, single-core FP32 5.40 → 16.34
GFLOPS (3.0×)** — the largest single win in this series, and the least
intuitive. The change is 64 bytes of padding per row.

## 1. The observation that led to it

By this point the inner loop was textbook
([N3](N3-t9-gemm-explicit-vectorisation.md)) and the fixed overhead was gone
([N4](N4-t9-gemm-fixed-overhead.md)), yet back-solving from the measurement still
gave **~473 cycles per k iteration at one thread**, against an issue bound of
~21. The loop contains 42 instructions and no long dependency chains.

A thread sweep made the shape of the problem much clearer:

| threads | GFLOPS | GFLOPS/thread |
|---|---|---|
| 1 | 1.34 | 1.34 |
| 2 | 2.69 | 1.35 |
| 4 | 5.40 | 1.35 |
| 60 | 75.82 | 1.26 |
| 120 | 99.80 | 0.83 |
| 240 | 124.08 | 0.52 |

Perfect linear scaling from 1 to 4 threads is the signature of **latency**, not
throughput: KNC has 4 hardware threads per core, and the 4 of them together
saturate whatever the single thread was waiting on. Once that is established,
the question is *what* it is waiting on — and at 473 cycles per iteration with
only 17 loads, it is not the loop.

## 2. The arithmetic

`A` is row-major with `lda = N`. The micro-kernel holds `MR` rows of A open
simultaneously, touching `A[i0+r][k]` for r = 0..MR-1 each k step. The row
stride in bytes is `N × 4`; at N=2048 that is exactly

```
8192 bytes = 2^13
```

The L1 on Knights Corner is 32 KiB, 8-way, 64-byte lines — so **64 sets**, and
the set index is address bits **6..11**. A stride of 2^13 leaves bits 6..11
unchanged. Every one of the `MR` rows therefore maps to the *same L1 set*:

```
row 0:  base + 0·8192   → set (base >> 6) & 63
row 1:  base + 1·8192   → set (base >> 6) & 63     ← identical
row 2:  base + 2·8192   → set (base >> 6) & 63     ← identical
...
```

Eight ways cannot hold 16 rows. Every k step evicts and refetches, and the
"16 rows resident in L1" assumption the micro-kernel was designed around is
simply false — the loads go to L2 instead, at a latency the in-order core
cannot hide.

This is a textbook power-of-two stride pathology, and it is *invisible in the
source*: `lda = N` is the most natural thing to write, and it is correct.

## 3. The fix

Give A a padded row stride, `lda = N + PAD`, with `PAD` chosen so the stride is
no longer a multiple of the set-index field:

```c
#define PAD32 16    /* float : +64 bytes per row */
#define PAD64 8     /* double: +64 bytes per row */
```

For FP32 the stride becomes `(2048 + 16) × 4 = 8256` bytes `= 64 × 129`.
Consecutive rows now differ by `129 mod 64 = 1` in set index, so 16 rows land in
16 distinct sets. For FP64, `(2048 + 8) × 8 = 16448 = 64 × 257`, likewise safe.

**The padding does not change the logical data.** `A[i][k]` keeps its value; the
pad bytes are simply never read by the kernel. That matters because the host
reference implementation computes the same matrix independently — no change was
needed on the host side, and the checksum comparison stayed valid across the
change.

Allocation and assertion:

```c
const uint32_t lda = N + PAD32;
float *A = (float *)_mm_malloc((size_t)N * lda * sizeof(float), 64);
```

## 4. Result

| | before | after |
|---|---|---|
| FP32, N=2048 | 113.9 | **149.2** |
| single-core FP32 (4 threads) | 5.40 | **16.34** |
| cycles per k iteration (1 thread) | ~473 | ~157 |

The per-core figure is the one to quote, because it isolates the effect from
thread count and from the parallel harness: **3.0×** on one core.

FP64 moved only 61.1 → 68.1 in that same sweep — not because the padding was
wrong for it (the FP64 stride is padded too), but because FP64's optimum
register tile turned out to be different, which is
[N7](N7-t9-gemm-jblocking-and-tuning.md). After that step FP64 reached 133.9.

## 5. Rule

> Whenever several rows (or columns) of a matrix are held open at once, check
> whether the row stride is a power of two, and whether that power lands inside
> the L1 set-index field. If it does, every row aliases to one set and the
> associativity is wasted. Padding the stride by one cache line is a
> two-character fix that can triple throughput — and it does not perturb the
> logical values, so it needs no change to the reference implementation.
>
> Derive this from a **per-core** measurement, not from a threaded one: a
> latency-bound kernel scales perfectly from 1 thread to the number of hardware
> threads per core and then flattens, and that signature — not a low absolute
> number — is what distinguishes "waiting on memory" from "out of issue slots".
