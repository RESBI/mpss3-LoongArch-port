# Appendix N2 — T9 GEMM Prerequisites: Four Constraints That Block Everything

Before any of the five optimisation steps in this series can run, four facts
about the k1om toolchain and the Knights Corner ISA have to be known. Every
failed attempt during this work was a violation of one of them, and the
symptoms were misleading enough that the suite had recorded the wrong
conclusion for a while. This report is the one to read first.

Source of truth: the *Phi ISA Reference Manual* (725 pp., extracted to text and
searched instruction by instruction), plus measured compiler behaviour on the
port's toolchain — k1om GCC 5.1.1, `k1om_cxx` wrapper from `tests/lib/common.sh`.

## Constraint 1 — the k1om backend never auto-vectorises

There is no `-O` level that produces 512-bit code from C. Measured by compiling
a plain `i-k-j` GEMM loop under eight flag combinations and counting `vfmadd`
with the SDK's own `k1om-mpss-linux-objdump`:

| Flags | `vfmadd` |
|---|---|
| `-O2` (default arch) | 0 |
| `-O3` | 0 |
| `-O3 -march=knc` | 0 |
| `-O3 -march=knc -mno-avx512vl` | 0 |
| `-O2 -fopenmp` / `-O3 -fopenmp` / `-O3 -march=knc -fopenmp` | 0 |
| `-O3 -mavx512f` | *assembler error* |
| `-O3 -march=knc -mavx512f` | *assembler error* |

`-march=knc` sets `-march` to `knc` but leaves the feature flags off —
`k1om_cxx -march=knc -Q --help=target` reports `-mavx512f [disabled]`, and
`-march=knc` defines only `__KNC__`, `__k1om`, `__k1om__`. So the vectoriser
has nothing to target and declines.

**Consequence.** The only route to 512-bit code is explicit `_mm512_*`
intrinsics. This is why the 6.39 GFLOPS intermediate version in
[N3](N3-t9-gemm-explicit-vectorisation.md) looked plausible and was not: it had
tiling, restrict pointers, aligned allocation and `#pragma omp simd`, and still
executed one scalar operation at a time.

## Constraint 2 — `-mavx512f` is required for intrinsics, and it poisons scalar FP

The intrinsics live in `avx512fintrin.h`, guarded by `#pragma GCC target("avx512f")`
and gated on `__AVX512F__`. Without `-mavx512f` every call fails with
`inlining failed in call to always_inline ... target specific option mismatch`.
With it, the whole translation unit is retargeted to **generic AVX-512F**, a
target that has xmm and ymm registers. KNC does not.

So the moment any *scalar* floating-point arithmetic remains in the file, GCC
emits its natural xmm form and the k1om assembler rejects it:

```
Error: `vcvtsi2ss' is not supported on `k1om'
Error: `vaddss'    is not supported on `k1om'
Error: `vmulsd'    is not supported on `k1om'
Error: bad register name `%xmm3'
```

This is not a corner case. It is triggered by `sin()`/`cos()`, by a `double`
accumulator for a checksum, by `double t = walltime()`, and by the host-facing
GFLOPS computation. A first attempt that mixed `_mm512_fmadd_ps` with a `float`
checksum failed on conversion instructions alone.

**Consequence — the iron rule of this kernel: no scalar floating point
anywhere in the card-side file.** Concretely, in `gemm_vec_sink.cpp`:

| Normal thing to write | What replaces it |
|---|---|
| `A[i] = 1.0f + sinf(i * 0.001f)` | load a 64-byte-aligned slice of a `.rodata` table |
| `double dt = (t1 - t0) * 1e-6` | return `int64_t` microseconds; the host divides |
| `float cs = 0; for (...) cs += C[i];` | `acc = _mm512_add_ps(acc, ...)`, store 16 lanes, host sums them |
| `(float)(i % 64) * 0.015625f` | not needed — see the table above |

## Constraint 3 — the ISA gaps ordinary C walks straight into

Checked one by one against the manual's instruction index:

| Missing on KNC | Nearest thing that exists | Where it bites |
|---|---|---|
| `VCVTDQ2PS` (int32→float32) | `VCVTFXPNTDQ2PS` — *fixed-point* convert, different intrinsic family | any `(float)i` |
| `VPADDQ`, `VPSLLQ`, `VPSUBQ` | nothing — KNC's integer unit is 32-bit lanes | 64-bit index/bit arithmetic; a first attempt at building `double` bit patterns this way failed on `vpaddq` |
| `VMOVUPS`, `VMOVUPD` — **no unaligned vector access at all** | `VLOADUNPACK{L,H}P{S,D}` / `VPACKSTORE*` | any vector load/store GCC cannot prove 64-byte aligned |
| `CMOVA` and the rest of `cmov` | nothing — branch instead | **any `? :` operator** (Constraint 4) |

The unaligned-access gap is the one that produces the least obvious errors,
because GCC emits `vmovups` from ordinary-looking code — for example a vector
store into a stack temporary that GCC placed at `-120(%rbp)`, or a table load
whose index it could only bound to `0..63`, not to a multiple of 16. The fixes
are, respectively, "put the vector field first in a `__attribute__((aligned(64)))`
struct so the store target is provably aligned" and "assert alignment on the
table slice with `__builtin_assume_aligned`". Both are visible in
`gemm_vec_sink.cpp` (`VResult::lanes` first; the `TABP` macro).

## Constraint 4 — `? :` is unusable

The smallest constraint and the most expensive to discover:

```c
/* Fails with: Error: `cmova' is not supported on `k1om' */
const uint32_t js1 = (js0 + JSB < njs) ? (js0 + JSB) : njs;
```

GCC compiles the ternary to a conditional move, KNC has none, and the assembler
stops. There is no warning and no fallback. Every clamp, every `min`, every
"take the smaller of" in this kernel became an `if`/`else`, and loop bounds were
restructured so no clamp is needed at all — the inner bound is `js0 + JSB` with
divisibility of N guaranteeing it cannot overrun (see
[N7](N7-t9-gemm-jblocking-and-tuning.md)).

This also explains the historical failure that started all of it: the first
attempt to build a sink with `-O2 -mavx512f` died with
`` Error: `cmovbe' is not supported on `k1om' `` and three `cmova` errors, from
tiling code that clamped block edges with ternaries.

## What this changes about the suite's earlier conclusion

The acceptance-suite README used to say *"a card-side sink can only be built
with `-O0`"*, on the evidence that `t7`'s N-body program crashes at `-O1`/`-O2`
(card-side `segfault`, faulting ip on `vpackstorelpd`). The observation is
correct; the generalisation is not.

The real rule is narrower and checkable:

> `-O2` on this toolchain emits scalar floating-point instructions and `cmov`,
> neither of which KNC has. The assembler rejects some; the ones it accepts
> (masked compress stores) fault at run time. A card-side source that keeps
> every floating-point operation inside a 512-bit intrinsic, and contains no
> `? :`, builds and runs correctly at `-O2 -mavx512f` — and is 38× faster than
> the `-O0` baseline.

`t7` still builds at `-O0`, because its source *is* scalar. That is now a
property of the source, not of the toolchain.

One thing does **not** change: the disassembly is not a usable criterion.
Counting `vpackstore`/`vscatter` gives 12 for the working `-O0` N-body build and
11 for each crashing `-O1`/`-O2` build. What *is* usable for a vectorised kernel
is the pair of gates in [N1 §3](N1-t9-gemm-optimization-overview.md) —
`vfmadd > 0` and `xmm/ymm == 0` — because for that kernel the failure mode is
"silently scalar", which those two counts detect directly.

## Checklist for a new vectorised card-side program

1. Build with `-O2 -mavx512f -fopenmp -rdynamic`.
2. No `float`/`double` scalar variable, literal arithmetic, or `math.h` call.
3. No `? :` anywhere.
4. Every vector load/store provably 64-byte aligned
   (`_mm_malloc(...,64)` + `__builtin_assume_aligned`, table slices via `TABP`).
5. Data from `.rodata` tables; timing as integer microseconds; reductions as
   vector accumulators stored to memory.
6. Gate the build on `vfmadd > 0` **and** `xmm/ymm == 0`.
