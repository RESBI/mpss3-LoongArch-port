# Appendix N9 — T7 N-Body: Overview and Results

Second sub-series of Appendix N. Where N1–N8 took the T9 GEMM from 0.33 to 260
GFLOPS by writing its inner loop in IMCI intrinsics, this series does the same
for T7's N-body integrator — and runs into a harder problem, because **KNC has
no hardware FP64 square root, division, reciprocal or reciprocal-square-root at
all.** The scalar path therefore calls `sqrt()`/`/`, which on the card are
software routines costing roughly **285 cycles per particle pair**. That, and
not the algorithm, was the whole of T7's 2.7 GFLOPS.

- **N9 — Overview and results** (this document)
- [N10](N10-t7-gemm-fp64-no-transcendentals.md) — What the ISA manual says KNC does *not* have, and how to build `1/sqrt(x)` without it
- [N11](N11-t7-two-units-and-three-traps.md) — The two-translation-unit architecture, and three traps only the hardware revealed
- [N12](N12-t7-nbody-headroom.md) — Where the remaining 95 % is, with the measurement that localises it

Code: `release/tests/src/nbody_vec.cpp` (vector unit, `-O2 -mavx512f`),
`release/tests/src/nbody_sink.cpp` (scalar unit, `-O0`),
`release/tests/t7_nbody.sh` (driver).

## 1. Results

Six tiers, and — this matters — **the "before" column is not historical**. It
is the scalar `-O0` implementation rebuilt with `-DNB_SCALAR_BASELINE=1` and run
on the *same* initial conditions, so the two columns differ only in how the
acceleration is computed. Both pass the host-reference check. See
[N13](N13-t7-verification.md) for why that care is necessary. Step counts are
20/10/5/4/4/4 — the integration is never shorter than 4 steps, so the force has
time to matter.

Two rates are reported, and they are the same measurement in two units:
**GFLOPS** (20 counted FLOP per pair) and **MPairs/s** — million physics pairs
`n(n−1)/2` per second. MPairs/s is exactly 50 × GFLOPS, so nothing is hidden by
the change of unit; it is there because "pairs per second" is the figure N-body
practitioners compare across codes. The GFLOPS column is the measured quantity;
the MPairs/s column is derived.

| Tier | N | steps | scalar `-O0` | vector `-O2 -mavx512f` | speedup | vector MPairs/s |
|---|---|---|---|---|---|---|
| 1 | 1024 | 20 | 0.52 GFLOPS | 2.38 | 4.6× | 119 |
| 2 | 8192 | 10 | 2.09 | 20.38 | 9.8× | 1019 |
| 3 | 16384 | 5 | 2.65 | 56.48 | 21.3× | 2824 |
| 4 | 32768 | 4 | 2.97 | 86.72 | 29.2× | 4336 |
| 5 | 65536 | 4 | 3.02 | 105.05 | 34.8× | 5253 |
| 6 | 131072 | 4 | 3.07 | 104.72 | 34.1× | 5236 |

Two things the extra tiers reveal that three could not.

**The scalar build is flat; the vector build is not.** From tier 3 upward the
scalar implementation sits at ≈3 GFLOPS regardless of problem size — it is bound
by the software `sqrt` and division, whose per-pair cost does not depend on N.
The vector build climbs to 105 GFLOPS. The earlier three-tier picture therefore
understated the result: at N=16384 the honest figure is 21×, not the ~35× the
large tiers reach.

**Throughput saturates at N ≈ 65536.** Tiers 5 and 6 land on the same number
(105.05 and 104.72), so beyond about 2¹⁶ particles nothing improves — the kernel
has reached its steady-state rate, 8.7 % of the card's ≈1208 GFLOPS FP64 peak.
Tier 6 exists mainly to *show* that plateau rather than to set a record.

Run-to-run spread on this card remains large — the same build moved between 97.6
and 109.3 GFLOPS at tier 6, and between 42 and 62 at tier 3, across a session —
so individual figures should be read as ±10 %, and the T9 series' rule stands:
quote ranges, not single runs.

**Large tiers skip the host reference by default.** The O(N²)·steps reference
costs ~5.2 × 10¹⁰ pair evaluations at N=131072, and running it at every tier took
the suite from 32 s to 227 s. It now runs by default only for N ≤ 16384, with
`T7_REF=1` forcing it everywhere; the driver records the large tiers as **SKIP**,
not PASS. What is *not* optional is the pair of kernel self-tests — the force
kernel against scalar libm and the reciprocal-square-root against `1/sqrt` —
which are cheap and run at every tier regardless. See [N13](N13-t7-verification.md).

### Correctness is unchanged, and that is the point

T7's strongest criterion is not the energy check (`rel < 1e-3`, loose) but the
tier-1 checksum, which must agree with the host reference implementation to
**1e-9 relative**. It agrees to **0.000e+00 — bit for bit**, on a 20-step
integration of a chaotic system, with the reciprocal-square-root replaced by a
completely different algorithm.

That is not luck, and it is worth stating why, because it is the design decision
that made the rest easy (see [N11](N11-t7-two-units-and-three-traps.md)):

> The kernel is vectorised **over `i`**, with `j` broadcast. Each lane therefore
> accumulates its own `j` loop **in the same order, with the same number of
> roundings, as the scalar reference**. The only arithmetic that differs is
> `1/sqrt` itself — and that converges to within ~1 ulp. Vectorising over `j`
> instead (the more obvious choice) would have reduced across lanes and changed
> the summation order, making a bit-exact match impossible in principle.

Energy conservation also holds: 1.957e-04 / 8.468e-05 / 4.016e-05 against the
1e-3 criterion — the same values the unoptimised version reported, to the
digits shown.

## 2. What actually changed

Four things, in order of how much they mattered:

1. **Replaced software `sqrt` + division with a hand-built vector
   `1/sqrt(x)`.** KNC has no FP64 transcendental instructions, so this had to be
   constructed from `VGETEXPPD`/`VGETMANTPD`, a minimax polynomial and two
   Newton iterations ([N10](N10-t7-gemm-fp64-no-transcendentals.md)). This is
   the whole speedup.
2. **Split the card-side sink into two translation units with different
   optimisation levels** — because `-mavx512f` poisons scalar floating point and
   plain `-O2` poisons vector code ([N11](N11-t7-two-units-and-three-traps.md)).
3. **Vectorised the potential-energy sum too**, not just the force loop. The
   energy is O(N²) and is called twice per run, so at tier 3 it is 2/7 of the
   work; optimising only `accel` would have capped the win at 1.4×.
4. **Aligned every array to 64 bytes.** `calloc` gives 16-byte alignment, and
   `vmovapd` requires 64 — KNC has no unaligned vector access, so the first
   vectorised build died with a general-protection fault on the load.
   ([N11](N11-t7-two-units-and-three-traps.md) §2.)

## 3. How the numbers are checked

- **Checksum, bit for bit, at tier 1** — host reference recomputed on the host
  from the identical initial conditions, `rel < 1e-9` required.
- **Energy conservation** at every tier, computed on the card, `< 1e-3`.
- **No kernel exceptions**: the script greps the card-side kernel log for
  `segfault` / `general protection` / `BUG` and fails the stage if any appear.
  This gate is not ceremonial — it is what caught both crashes in
  [N11](N11-t7-two-units-and-three-traps.md).
- **`vpackstore`/`vscatter` count is reported, not used as a criterion** — the
  suite's long-standing note stands: the working `-O0` build has 12, the
  crashing `-O2` build has 11, and the working *vectorised* build has 11.

## 4. Caveats

- **The absolute GFLOPS figures are tier-dependent**, and the small tier is
  dominated by fixed costs: at N=1024 the whole O(N²) work is 20 steps × 5×10⁵
  pairs, which at 240 threads is a few milliseconds against a ~0.1 s offload
  startup. Only tier 3 is a meaningful throughput number. This is the same
  effect diagnosed in [N4](N4-t9-gemm-fixed-overhead.md).
- **Tier 1's spread (0.79–2.84) is not only noise — it was a real bug**, found
  while measuring: processing two `i` blocks per `j` iteration halves the number
  of parallel work items, and N=1024 only has 64 such groups against 240 threads,
  so more than half the threads idled. The kernel now falls back to one block per
  iteration when the work items cannot fill the machine. See
  [N12](N12-t7-nbody-headroom.md) §2.
- **The 20-FLOP-per-pair convention flatters the implementation slightly**: the
  reciprocal-square-root is not 1 FLOP of hardware work, it is ~30 vector
  instructions per 8 pairs. The useful comparison is against the previous
  implementation of the same algorithm on the same metric, which is what the
  table gives.
- **4.3 % of peak is not "maxed out".** [N12](N12-t7-nbody-headroom.md) shows
  where the other 95 % is and gives the measurement that localises it.

## 5. Reproducing

```bash
cd release/tests
bash t7_nbody.sh                    # all three tiers, ~2 min at tier 3
```

Logs land in `logs/run-<timestamp>/T7-tier{1,2,3}.log`, and the compiled
`nbody_vec.o` / `nbody_sink.o` next to them, so the two optimisation levels can
be checked independently.

## 6. Related documents

- The GEMM sub-series, and the four toolchain constraints this one builds on:
  [N1](N1-t9-gemm-optimization-overview.md), [N2](N2-t9-gemm-isa-constraints.md)
- ISA manual cross-check methodology: Appendix [D](D-manual-crosscheck.md)
- Offload and OpenMP field notes: Appendix [F](F-coi-and-openmp.md), [H](H-offload-field-notes.md)
