# Appendix N4 — T9 GEMM Step 2: Removing the Fixed Parallel Overhead

**Measured effect: FP32 57.4 → 104.1 GFLOPS, FP64 45.4 → 61.1 GFLOPS**
(N=2048, 244 threads). No change to the micro-kernel; the work was in the
harness around it.

## 1. Symptom: a constant that should not be there

With the vectorised kernel from [N3](N3-t9-gemm-explicit-vectorisation.md), three
problem sizes were timed. If the kernel were the only cost, time should scale as
N³ — a 512× increase in work from N=256 to N=2048.

| N | work (GFLOP) | time (s) | implied GFLOPS |
|---|---|---|---|
| 256 | 0.0336 | 0.152 | 0.22 |
| 1024 | 2.147 | 0.198 | 10.85 |
| 2048 | 17.18 | 0.299 | 57.43 |

The work rises 512×, the time rises 2×. Fitting the two larger points:
marginal rate ≈ 125 GFLOPS, **fixed cost ≈ 0.15 s**. At N=2048 that is half the
runtime; at N=256 it is essentially all of it. The kernel was never as slow as
the benchmark said — the benchmark was measuring process setup.

## 2. Cause 1: the OpenMP thread team is created inside the timed region

`omp_set_num_threads(240)` followed by the first `omp parallel for` creates the
team lazily, on entry. On this card, with 244 hardware threads, that creation
(thread spawn, stack setup, affinity, first barrier) costs on the order of
100 ms — visible directly in the retained cold/warm columns:

```
卡端耗时   : 0.138459 s  (热)
冷启动对比 : 0.258473 s  (线程场创建等一次性开销 120.0 ms)
```

**Fix.** Run the kernel once **untimed**, then time a second identical pass and
report that. This is legitimate here only because the micro-kernel overwrites C
rather than accumulating into it (§2.2 of [N3](N3-t9-gemm-explicit-vectorisation.md)),
so the second pass is idempotent and produces the same C. The result struct
carries both numbers (`time_us` and `time_us_cold`) so the fixed cost stays
visible instead of being quietly hidden — it is real, it just is not the
kernel's cost.

## 3. Cause 2: `schedule(dynamic,1)` over 16384 iterations

The parallel loop is `collapse(2)` over `(j-strip, i-row-block)`. At N=2048 with
`MR32=8` that is `(2048/16) × (2048/8) = 128 × 256 = 32768` micro-kernels, and
with `dynamic, 1` every one of them is a separate claim on a **single shared
counter**. 244 threads hammering one atomic, 32768 times, is pure contention —
and it buys nothing, because the micro-kernels are uniform in cost and there is
no load to balance.

**Fix.** `schedule(static)`. Iterations are divided once, up front, into equal
contiguous ranges. Each thread then walks a contiguous span of its own, which
also happens to be better for the cache (consecutive `i0` blocks reuse the same
B strip — see [N5](N5-t9-gemm-b-panel-packing.md)).

## 4. Result

| | before | after |
|---|---|---|
| FP32 GFLOPS, N=2048 | 57.4 | **104.1** |
| FP64 GFLOPS, N=2048 | 45.4 | **61.1** |
| FP32 GFLOPS, N=1024 | 10.85 | **47.84** |
| FP32 GFLOPS, N=256 | 0.22 | **1.68** |

N=1024 gained 4.4× on a change that does not touch the kernel, which is the
clearest evidence that the previous numbers were harness-dominated. Fixed cost
dropped from ~0.15 s to ~28 ms.

Two secondary observations from the same sweep, kept because they constrain what
"enough work" means at these sizes:

- **244 threads is the right operating point** for N=2048, but the scaling is
  not linear from there down. Measured per-thread rate at N=2048: 1 thread
  1.34 GFLOPS, 4 threads (= one core) 5.40, 60 threads 75.8, 120 threads 99.8,
  240 threads 124.1. The 60 → 240 step (4× the threads, 61× the cores) buys only
  1.6×, which is the first hint that the kernel is memory-latency-bound rather
  than issue-bound — the thread that follows.
- **The residual ~28 ms is not worth chasing further.** It is a one-off per
  offload invocation; a real workload would amortise it over many calls.

## 5. Rule

> Before tuning a kernel, measure the fixed cost by fitting time against work
> across at least three problem sizes. On this platform an offload invocation
> carries ~0.1 s of thread-team setup, so any benchmark whose total runtime is
> comparable to that is measuring the harness. Warm up untimed — but only when
> the kernel is idempotent, and report the cold figure alongside the warm one.
> For uniform work, prefer `schedule(static)`; `dynamic,1` with hundreds of
> threads is contention with no compensating benefit.
