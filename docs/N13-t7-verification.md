# Appendix N13 — T7: What the Tests Actually Verify

This report exists because a question — *is the computation actually verified?* —
turned out to have the answer **no, not in the way it appeared to**. The T7 suite
had a checksum comparison against a host reference implementation that passed
bit-for-bit, an energy-conservation check, and a clean kernel log. It looked
thorough. It could not detect a force law that was wrong by a factor of a
hundred, or absent entirely.

The finding, the reason, and the three-layer verification that replaced it.

## 1. The experiment

T7's card-side kernel computes one thing that matters: the gravitational
acceleration

```
a_i = sum_{j != i} m_j (r_j - r_i) / |r_j - r_i|^3
```

So the acceleration was deliberately broken in four ways, each a one-line edit
to `inv_cubed_pd`, and the full tier-4 test was run on each:

| Variant | Physics | GFLOPS | Energy error | Host reference check |
|---|---|---|---|---|
| A | correct | 74.09 | 2.999e-05 | **pass**, diff 2.96e-16 |
| B | `inv = m·x·0.5` — meaningless | 159.36 | 2.054e-06 | **pass**, diff 1.16e-14 |
| C | `inv = 0` — **no force at all** | 223.55 | 1.192e-06 | **pass**, diff 1.58e-14 |
| D | force × 2 | 82.06 | 1.809e-04 | **pass**, diff 2.96e-15 |

**Removing the force entirely made every check pass**, and the energy error got
*smaller* (1.19e-06 against 3.00e-05), because a system that does not accelerate
conserve energy trivially.

## 2. Why

Two compounding reasons, and both are worth stating plainly.

**The dynamics were not force-dominated.** Over 4 steps at `dt = 0.002` the
particles move mostly ballistically: the displacement from the initial velocity
is ~8e-4, while the displacement *caused by* the acceleration is some orders of
magnitude smaller. The checksum is a sum over final positions, so the force
enters it as a small correction on top of a large ballistic term.

**And the force was nearly zero to begin with.** The initial conditions were a
regular lattice with a 1% jitter:

```c
const double jitter = 0.01 * sin(12.9898 * i + 78.233);
x[i] = (ix + 0.5) * inv + jitter * inv;
```

A regular lattice has no net gravitational force by symmetry — every pull
cancels against another. Only the jitter breaks the symmetry, so the true
acceleration was tiny and replacing it with zero changed almost nothing. The
checksum comparison was, in effect, verifying the initial conditions and the
integration bookkeeping, and testing the force only in the fourth decimal place
of an already-small quantity.

**Neither check was wrong; they were answering a different question than the one
they appeared to answer.** "The checksum matches to 0.000e+00" read as "the
physics is right". It meant "the physics is no more than a rounding error away
from being irrelevant to this observable".

This is the failure mode [N2](N2-t9-gemm-isa-constraints.md) warns about in a
different guise: a verification that is *consistent* is not thereby
*discriminating*.

## 3. The fix, in three layers

Each layer answers a question the others cannot.

### Layer 1 — the transcendental, tested directly

`rsqrt_max_rel_err`: 512 log-uniform samples spanning the documented design range
`x ∈ [2^-12, 2^4)`, computed by the card's vector `rsqrt_pd` and compared
point-by-point against the scalar `1.0/sqrt(x)`.

```
卡上 rsqrt 最大相对误差 : 2.556e-16
```

That is ~1 ulp. This is the only way to verify the hand-built reciprocal-square-
root ([N10](N10-t7-gemm-fp64-no-transcendentals.md)) — the checksum cannot
localise an error to one function, and this function is the one piece of
arithmetic that was invented rather than ported.

### Layer 2 — the force kernel, tested against a scalar reference

`accel_max_rel_err`: the vector kernel is run on the first 64 bodies and its
output compared against a straightforward scalar loop using libm `sqrt` and `/`,
computed in the scalar translation unit *on the card*.

```
受力核最大相对误差 : 6.340e-16
```

Cheap (64² pairs), definitive, and it is the layer the original suite was
missing entirely. Re-running the four broken variants against it:

| Variant | Force-kernel error | Verdict |
|---|---|---|
| A correct | 6.340e-16 | pass |
| B `inv = m·x·0.5` | **1.001e+00** | **fail** |
| C `inv = 0` | **1.000e+00** | **fail** |
| D force × 2 | **1.000e+00** | **fail** |

Every one is caught. The end-to-end checksum caught none of them.

**The self-test itself had a hole, and the same experiment found it.** The first
version ran the kernel on 64 bodies — 8 vector blocks — which the parallelism
guard ([N12](N12-t7-nbody-headroom.md) §3) judges too few to fill 240 threads, so
it fell back to the single-block tail loop. The two-block main loop, which
computes essentially all of the physics, was therefore **never exercised**. It
showed: replacing *its* reciprocal-square-root with a constant left the
self-test still reporting 6.340e-16, because it was testing different code.

The kernel now exposes `nbody_accel_force2()` so the self-test can force the
two-block path, and runs both:

| | layer 2 result |
|---|---|
| correct kernel, both paths | 6.340e-16, pass |
| two-block path broken | **1.001e+00, fail** |

> **Rule.** A unit test that reaches the code by a path of its own choosing may
> not be testing the code that runs. Verify that the test exercises the branch
> you care about — here by breaking that branch and confirming the test notices.

### Layer 3 — the end-to-end trajectory, at the small tiers by default

> **Scope.** This layer is the expensive one — O(N²)·steps — and it is **off by
> default for N > 16384**: running it at all six tiers takes the suite from 32 s
> to 227 s. `T7_REF=1` forces it everywhere, and the driver records a skipped
> tier as **SKIP**, never as PASS. Layers 1 and 2 are *not* optional and run at
> every tier, which is deliberate: they are the layers with actual discriminating
> power (§1), and they cost milliseconds.

The host reference is compared by default at the first three tiers.
It is O(N²), so it was parallelised — but **over `i` only**, with each thread
running a complete sequential `j` loop, because that reproduces the shape of the
card kernel (one lane accumulates one body's `j` loop in order). Parallelising
over `j` or reducing across threads would change the summation order and make the
comparison meaningless.

```
默认（N <= 16384）： 档位 1 差 0.000e+00 · 档位 2 差 0.000e+00 · 档位 3 差 1.482e-16
T7_REF=1（全部六档）：档位 4 差 2.960e-16 · 档位 5 差 2.963e-16 · 档位 6 差 1.481e-16
```

The six-tier forced run passes 19/19 in 227 s; the default run passes 16 with 3
SKIPs in 32 s. **A skipped check is reported as skipped, not as a pass** — the
whole point of §1 is that a check which did not run must not be counted as
evidence.

### And the initial conditions

The lattice was replaced by an integer hash:

```c
static double hash01(unsigned k)
{
    k = (k ^ 61u) ^ (k >> 16);
    k *= 9u;
    k = k ^ (k >> 4);
    k *= 0x27d4eb2du;
    k = k ^ (k >> 15);
    return (double)(k & 0xFFFFFFu) * (1.0 / 16777216.0);
}
```

Two properties matter. It is **irregular**, so the net force is no longer
symmetry-cancelled and the dynamics genuinely depend on the force law. And it is
**bit-reproducible across the host and the card** — integer arithmetic plus one
multiplication by an exact power of two — which `sin`/`cos` are not, since those
depend on each side's libm.

The unscaled velocities still use `sin`/`cos` and still match; that is a property
of this toolchain pair, not something to rely on for the positions, where the
hash removes the question.

## 4. What this changes about the reported numbers

The old headline, "checksum matches bit-for-bit", was true but much weaker
evidence than it looked. The new suite reports **17 checks**, of which three are
independent correctness layers:

| | |
|---|---|
| Tiers verified against the host reference | 5 / 5 |
| `rsqrt` max relative error | 2.556e-16 |
| Force kernel max relative error | 6.340e-16 |
| Kernel exceptions | 0 |

The performance numbers were also re-measured on the corrected initial
conditions, against the scalar `-O0` build **on the same inputs**, both passing
the reference check — see [N9](N9-t7-nbody-overview.md) §1. The speedup is larger
than previously reported (31.6× at N=65536) because the force-dominated workload
penalises the scalar libm `sqrt`/division path more, which is the honest
comparison.

## 5. Rules

> **A passing test proves the code is consistent with the test, not that the
> test is capable of failing.** Before trusting a numerical check, break the
> thing it is supposed to protect and confirm the check notices. Here, delibrately
> deleting the entire gravitational force left every existing check green.

> **Verify the parts you invented, directly and separately.** An end-to-end
> comparison over a chaotic system is a good integration test and a poor unit
> test: it cannot tell you *which* component is wrong, and — as this case shows —
> it may not be sensitive to a component at all. A 512-point sweep for the
> transcendental and a 64-body comparison for the force kernel cost
> milliseconds and are conclusive.

> **Check that the workload exercises the code under test.** A benchmark whose
> dominant term is a term the answer does not depend on will report the right
> number for the wrong reason. The lattice initial conditions made the force a
> 4th-decimal correction to the observable; the hash makes it the observable.

## 6. Related

- The transcendental being verified: [N10](N10-t7-gemm-fp64-no-transcendentals.md)
- The two-unit architecture the force self-test relies on:
  [N11](N11-t7-two-units-and-three-traps.md)
- Revised results and the corrected scaling curve:
  [N9](N9-t7-nbody-overview.md), [N12](N12-t7-nbody-headroom.md)
