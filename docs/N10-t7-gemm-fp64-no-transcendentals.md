# Appendix N10 — T7: KNC Has No FP64 Transcendentals, So Build One

This is where the T7 optimisation was won or lost. The N-body kernel needs
`1/sqrt(r²)` — once per particle pair, and the pairs are the whole workload.
On any normal x86 there is an instruction for it. **On Knights Corner there is
not, for doubles**, and the substitute had to be assembled by hand.

## 1. What the manual says is missing

Method: the *Phi ISA Reference Manual* was extracted to text and its instruction
index (164 entries) filtered by suffix.

**Every FP64 vector instruction that takes a suffix `PD`:**

| Present | Absent |
|---|---|
| `VADDPD`, `VADDNPD`, `VSUBPD`, `VSUBRPD`, `VMULPD`, `VFMADD*PD`, `VGMAXPD`, `VGMINPD` | **`VSQRTPD`** |
| `VCMPPD`, `VBLENDMPD`, `VPCMPD`, `VFIXUPNANPD` | **`VDIVPD`** |
| `VMOVAPD`, `VLOADUNPACK*PD`, `VPACKSTORE*PD`, `VGATHERDPD`, `VSCATTERDPD` | **`VRSQRTPD`** (any revision) |
| `VCVTDQ2PD`, `VCVTPS2PD`, `VCVTUDQ2PD` | **`VRCPPD`** (any revision) |
| **`VGETEXPPD`, `VGETMANTPD`** — extract exponent / mantissa | **`VEXPPD`, `VLOGPD`** |
| `VRNDFXPNTPD` — round | `VCVTPD2PS`, `VCVTPD2DQ` (down-conversion) |

The FP32 side *does* have them: `VRSQRT23PS`, `VRCP23PS`, `VEXP223PS`,
`VLOG2PS`. Note the naming — **"23", not the "28" of later revisions**; the
first search for `VRSQRT28PD` found nothing, and the assembler confirms the
manual: it accepts `vrsqrt23ps` and rejects `vrsqrt28ps`.

So KNC's FP64 pipeline has multiply, add, FMA, compare, and exponent/mantissa
extraction — and nothing else. `sqrt()` and `/` on a `double` compile to library
calls.

## 2. What the toolchain adds

The intrinsics are declared in the toolchain headers, but that is not the same as
being usable. Compile-and-disassemble probe, `-O2 -mavx512f -mavx512er`:

| Intrinsic | Result |
|---|---|
| `_mm512_sqrt_pd`, `_mm512_div_pd` | **assembler error** — `vsqrtpd` / `vdivpd` "not supported on k1om" |
| `_mm512_rsqrt28_round_pd`, `_mm512_rcp28_round_pd` | assembler error — `no such instruction: vrsqrt28pd` |
| `_mm512_rsqrt14_pd`, `_mm512_rcp14_pd` | assembler error — `no such instruction: vrsqrt14pd` |
| `_mm512_cvtpd_epi32` | assembler error — GCC emits `vcvtpd2dq`, KNC spells it `VCVTFXPNTPD2DQ` |
| `_mm512_roundscale_pd` | assembler error — GCC emits `vrndscalepd`, KNC spells it `VRNDFXPNTPD` |
| `_mm512_cvtpd_ps` / `_mm512_cvtps_pd` | unusable — the down-conversion returns `__m256`, and KNC has no ymm |
| **`_mm512_getexp_pd`, `_mm512_getmant_pd`** | **work** — `vgetexppd` / `vgetmantpd` |
| `_mm512_cmp_pd_mask`, `_mm512_mask_mul_pd`, `_mm512_mask_sub_pd`, `_mm512_fmadd_pd`, `_mm512_fnmadd_pd` | work |

Note the pattern in the failures: **GCC names several of these instructions the
AVX-512 way while the k1om assembler only knows the KNC name.** That is a
toolchain gap, not a hardware one — but with no way to spell the instruction, it
is a gap all the same.

Two consequences worth separating:

- The FP32 `vrsqrt23ps` **is** accepted by the assembler. The natural plan —
  down-convert `r²` to FP32, take the hardware reciprocal-square-root, refine in
  FP64 — is blocked only by the missing down-conversion. Both available routes
  (`_mm512_cvtpd_ps` and `_mm512_cvtpd_epi32`) fail above. This is the one place
  where a small amount of inline assembly might yet pay.
- Everything needed for a *software* FP64 `1/sqrt` is present: exponent
  extraction, mantissa extraction, FMA, masked multiply/subtract, compare.

## 3. Building `1/sqrt(x)` from what is left

`x = m · 2^e` with `m ∈ [1,2)`, so

```
1/sqrt(x) = q^e · m^(-1/2)        where q = 1/sqrt(2) = 2^(-1/2)
```

Two independent sub-problems.

### 3.1 `q^e` for an integer exponent — with no integer conversions

The obstacle is not the arithmetic, it is that `e` arrives as a **double** and
KNC offers no double→int conversion that the assembler accepts, so the usual
bit-twiddling recipes are unavailable. The way round it is to test the value
directly: since `r² ≥ soft2 = 1e-3`, the exponent satisfies `e ∈ [-10, 1]`, so

```
k = e + 12          ∈ [0, 15]        (covers e ∈ [-12, 3], i.e. r² ∈ [2.4e-4, 16))

r = 1
if (k >= 8) { r *= q^8;  k -= 8; }   q^8 = 2^-4
if (k >= 4) { r *= q^4;  k -= 4; }   q^4 = 2^-2
if (k >= 2) { r *= q^2;  k -= 2; }   q^2 = 2^-1
if (k >= 1) { r *= q^1;  k -= 1; }   q^1 = 2^-0.5
```

Four binary steps, **branchless**: each becomes a compare-to-mask plus a masked
multiply plus a masked subtract — 12 vector instructions, no branches, no
integer conversion. `r = q^(e+12)`, so the constant `2^6` is folded into the
polynomial below to cancel `q^12`.

### 3.2 `m^(-1/2)` — a degree-5 minimax polynomial

Fitted numerically over `m ∈ [1,2)` with relative-error weighting, then verified
on a 10⁶-point grid independently of the fitting code (the first attempt at
verification used the wrong variable and "proved" a 100 % error — worth the
extra five minutes):

| degree | max relative error |
|---|---|
| 3 | 4.80e-04 |
| 4 | 7.41e-05 |
| **5** | **1.17e-05** |
| 6 | 1.86e-06 |

Degree 5, coefficients pre-multiplied by 64:

```
p = ((((b5·m + b4)·m + b3)·m + b2)·m + b1)·m + b0
    b0 =  1.458420823144776079e+02     b3 = -6.957543232001837907e+01
    b1 = -1.706815358164975293e+02     b4 =  1.833312835738097135e+01
    b2 =  1.420908449938614524e+02     b5 = -2.009833734602153754e+00
```

### 3.3 Two Newton iterations

`y ← y · (1.5 − 0.5·x·y²)`, three instructions each with `0.5x` hoisted out.
Convergence, worst case (`ε' = 1.5ε²`):

```
seed 1.17e-05  ->  2.04e-10  ->  6.24e-20
```

Two iterations take it below double precision; the result is correctly rounded
to ~1 ulp, which is why the T7 checksum still matches the host's libm-based
reference **bit for bit**.

### 3.4 Total cost

`getexp` + `getmant` + 12 (exponent scaling) + 5 FMA (polynomial) + 1 mul
(combine) + 9 (two Newton iterations) ≈ **30 vector instructions per 8 pairs**,
against roughly 285 *cycles per pair* for the software `sqrt` and division it
replaces.

## 4. The two compiler traps inside this function

Both cost a debugging round and neither is about the algorithm.

**Trap 1 — `(double)EXP_OFF`.** Writing the exponent offset as an integer cast
makes GCC emit `vcvtsi2sd` — a scalar int→double conversion, i.e. an xmm
instruction. It must be a literal (`12.0`).

**Trap 2 — constants staged through xmm.** This is the subtle one. GCC hoists
loop-invariant broadcasts, and when it runs short of vector registers it
materialises a constant with

```
vmovsd       .LC0(%rip), %xmm10      <- no xmm on KNC
vbroadcastsd %xmm10, %zmm4           <- no register-form broadcast on KNC
```

which the assembler rejects. It chose that path for exactly the constants used
as operands of **masked** operations, and for the polynomial coefficients —
because an FMA can take only one memory operand, so the other had to live in a
register.

The fix is to stop expressing them as scalars at all. Every constant is written
as an **8-lane array in `.rodata`** and loaded:

```c
static const double NB_K[11][8] __attribute__((aligned(64))) = { ... };
#define NK(i) _mm512_load_pd(NB_K[i])
```

A full-width load becomes `vmovapd`, which is legal. Six constants were
converted this way (four thresholds, four multipliers, three Newton constants,
six polynomial coefficients); the illegal `vmovsd`/`xmm` output went to zero.

This is the same lesson as [N6](N6-t9-gemm-row-stride-padding.md) in a different
guise: **on this toolchain you do not persuade the compiler, you arrange for the
code it generates to be expressible.** The check is mechanical — disassemble and
count `%xmm`/`%ymm`; if the count is not zero, the card will not run it.

## 5. Rule

> Before porting a numerical kernel to KNC, ask the ISA manual what the FP64
> pipeline actually has. Here it has no square root, no divide, no reciprocal
> and no reciprocal-square-root — only FMA, compare, and
> `VGETEXPPD`/`VGETMANTPD`. A correctly-rounded `1/sqrt` is still reachable:
> split `x = m·2^e`, handle `q^e` with four branchless masked scaling steps
> driven by value comparisons (no integer conversion exists), fit `m^(-1/2)`
> with a degree-5 minimax polynomial, and take two Newton iterations.
> ~30 vector instructions per 8 values, and accurate to 1 ulp.
