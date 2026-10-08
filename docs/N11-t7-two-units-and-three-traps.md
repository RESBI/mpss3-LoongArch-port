# Appendix N11 — T7: Two Translation Units, and Three Traps Only the Hardware Revealed

The vector kernel of [N10](N10-t7-gemm-fp64-no-transcendentals.md) is correct
arithmetic. Getting it to *run* took two structural decisions and three
diagnoses that no amount of reading the source would have produced.

## 1. Why the sink is now two translation units

The card-side program needs two mutually incompatible things at once:

| Part of T7 | Needs | Why |
|---|---|---|
| initial conditions (`sin`, `cos`), kinetic energy, checksum, timing | **scalar floating point** | they are inherently scalar and must match the host reference |
| `accel()` and the potential sum | **`-mavx512f`** | that is the only way to get 512-bit instructions at all ([N2](N2-t9-gemm-isa-constraints.md)) |

Those cannot coexist in one file. `-mavx512f` retargets the whole translation
unit to generic AVX-512F, so any scalar floating point in it becomes an xmm
instruction, and KNC has no xmm. Conversely, leaving scalar code at `-O2` outside
`-mavx512f` produces the `vpackstorelpd` form that faults on the card (§3).

So:

```
nbody_vec.cpp    -O2 -mavx512f -fopenmp -c     -> nbody_vec.o
nbody_sink.cpp   -O0          -fopenmp -c     -> nbody_sink.o
k1om_cxx -fopenmp -rdynamic nbody_vec.o nbody_sink.o -lcoi_device -lgomp ... -> nbody_sink
```

The interface between them is deliberately narrow and **contains no floating
point at all** — only `int` and `double *`:

```c
extern "C" void nbody_accel(int n, const double *x, const double *y,
                            const double *z, const double *m,
                            const double *soft2p,
                            double *ax, double *ay, double *az);
```

`soft2` is passed by pointer rather than by value, so no `double` crosses the
ABI boundary in a register. Given the two units are compiled to different
targets, that is worth the extra parameter.

**This split is reusable.** Any card-side program that needs both scalar
transcendentals and vector math should be built this way rather than trying to
find flags that reconcile them; there are none.

## 2. Trap 1 — `calloc` is not aligned enough, and KNC has no unaligned load

First vectorised build: the card-side process died with
`COI_PROCESS_DIED(23)`. The card's kernel log:

```
nbody_sink[...] general protection ip:40142f
```

Disassembling that address:

```
40142f:  62 31 f9 08 28 0c 19    vmovapd (%rcx,%r11,1),%zmm9
```

`vmovapd` requires **64-byte alignment**; `(%rcx,%r11)` is `x + i0·8` with `i0` a
multiple of 8, so the index contributes a multiple of 64 — the base pointer was
the problem. `x` came from `calloc`, which guarantees 16.

KNC has **no unaligned vector access** — no `vmovupd`, no `vmovups`; the only
unaligned forms are the `VLOADUNPACK`/`VPACKSTORE` pair, which are exactly the
instructions that fault elsewhere (§3). So an aligned load on an unaligned
address is not a performance question, it is a fault.

Fix: allocate every array passed to the vector kernel with 64-byte alignment.

```c
static double *alloc64(int n)
{
    void *p = NULL;
    if (posix_memalign(&p, 64, (size_t)n * sizeof(double)) != 0) return NULL;
    if (p) memset(p, 0, (size_t)n * sizeof(double));
    return (double *)p;
}
```

Note the failure mode for the reader: the *symptom* was "the process died", with
no message from the card. The diagnosis came entirely from the card-side kernel
log plus the fault address, which is why `t7_nbody.sh` greps that log and fails
the stage on any hit.

## 3. Trap 2 — `VCMPPD`'s predicate is 3 bits, and GCC encodes 5

With alignment fixed, the fault changed character:

```
nbody_sink[...] trap invalid opcode ip:4014ec
```

```
4014ec:  62 f1 e9 08 c2 cf 0d    vcmppd $0xd,%zmm7,%zmm2,%k1
```

Immediate `0xd` = 13. That is the standard AVX-512 encoding of `_CMP_GE_OS`,
which is what the source asked for (`_mm512_cmp_pd_mask(k, threshold,
_CMP_GE_OS)`). The ISA manual, §6.3:

> **Immediate Format** — Comparison Type `I2 I1 I0`: eq 000, lt 001, le 010,
> unord 011, neq 100, nlt 101, nle 110, ord 111
> … `{gt}` A > B — **Swap operands, use LT**; `{ge}` A >= B — **Swap operands, use LE**

KNC's `VCMPPD` uses only `IMM8[2:0]`. There is no `ge` predicate; the manual's
own substitution is to swap the operands and use `le`. GCC emitted the 5-bit
AVX-512 form regardless, and the hardware reported the instruction as invalid.

Fix — write the comparison in the form the manual prescribes:

```c
/* "k >= threshold"  ==  "threshold <= k" */
mk = _mm512_cmp_pd_mask(NK(0), k, _CMP_LE_OS);
```

which encodes as immediate 2. All four scaling steps were changed; the whole
stage then passed.

**This is a general hazard, not a one-off.** Wherever KNC's MVEX encoding differs
from AVX-512 — and §1 and [N10](N10-t7-gemm-fp64-no-transcendentals.md) §2 show
several such places — GCC will happily emit the AVX-512 form because the
`avx512fintrin.h` it is compiling against describes AVX-512. The assembler
catches the cases where the instruction is simply unknown; it does **not** catch
the cases where the instruction exists but the immediate or operand encoding
means something else. `VCMPPD` with an out-of-range predicate is the latter, and
only the hardware catches it.

The practical rule: for any instruction whose behaviour is selected by an
immediate (compares, rounds, conversions, shuffles on KNC), check the manual's
table rather than trusting the intrinsic's name.

## 4. Trap 3 — the `vpackstorelpd` that has been crashing T7 all along

This one predates the optimisation, and the work here finally explains it. T7
has been pinned to `-O0` for a long time because `-O1`/`-O2` crash. The
card-side log for a `-O2 -fno-tree-vectorize` build:

```
segfault at 0 ip 00000000004017b1 error 6
```

and at that address:

```
4017b1:  62 d2 f9 0a d1 44 24 02    vpackstorelpd %zmm0,0x10(%r12){%k2}
```

`vpackstorelpd` is KNC's *pack and store unaligned* instruction — the masked
compress store. Comparing the working and failing builds instruction by
instruction:

| Build | `vpackstore`/`vscatter` count | Addressing |
|---|---|---|
| `-O0` (works) | 12 | **all** `-0xNN(%rbp)` |
| `-O2 -fno-tree-vectorize` (crashes) | 11 | ten `(%rsp)`, **one `0x10(%r12)`** |

The failing one is a **single-double store** — `kmov $1, %k2` immediately before
it, so it writes 8 bytes — to a base register other than `rbp`/`rsp`. With `rbp`
or `rsp` the encoding is forced to use a 32-bit displacement; with `r12` the
assembler used KNC's compressed `disp8*N` form. The manual flags exactly this
family as special:

> Note that some instructions work at element granularity instead of full vector
> granularity at memory level, and hence should use the "element level" column …
> (namely VLOADUNPACK, VPACKSTORE, VGATHER, and VSCATTER instructions)

So for `VPACKSTORE` the displacement scales by the **element** size (8 bytes for
a double), not by the vector size. Whatever the precise mechanism, the
observation is solid and reproducible: the `rbp`/`rsp` form runs, the
`r12`+`disp8*N` form faults.

Two practical consequences:

- **The count really is not the criterion** — 12 works and 11 crashes — which
  confirms the note the suite has carried for months. The *addressing form* is
  what differs.
- **The way out is not to fix `vpackstorelpd` but to stop generating it.** The
  vector kernel emits **zero** of them, because it never stores a scalar double
  from a vector register: results leave the kernel as full 8-lane
  `_mm512_store_pd` into 64-byte-aligned arrays, and the scalar unit does the
  rest. That is why `nbody_vec.cpp` can be built `-O2 -mavx512f` and run, while
  `nbody_sink.cpp` must stay `-O0`.

## 5. The verification loop that made this tractable

Each trap was found the same way, and the sequence is worth reusing:

1. Run the stage; it fails with `COI_PROCESS_DIED(23)` and nothing else.
2. Read the **card-side** kernel log (`/var/log/messages` on the card; `dmesg`
   on the card alternates between hosts and garbles lines under 240 threads).
   It names the fault class and the instruction pointer.
3. Disassemble **the built sink**, not the object — the addresses in the log are
   final link addresses.
4. Identify the instruction, then ask the manual what it actually requires.
5. Fix the *source* so that instruction is not generated, then re-check that the
   disassembly contains no `%xmm`/`%ymm` and, where relevant, no
   `vpackstore`/`vscatter`.

Step 2 is the one that is easy to skip and impossible to substitute for.

## 6. Rule

> When a card-side program needs both scalar transcendentals and vector math,
> split it into two translation units and give each the flags it needs — the
> interface between them carries pointers and ints, never `double`.
> Then assume the debugger is the card's kernel log: `general protection` means
> an alignment-requiring instruction met an unaligned address (allocate 64-byte),
> and `trap invalid opcode` means an encoding the hardware does not implement —
> quite possibly because GCC emitted the AVX-512 form of an instruction whose
> KNC form differs. Check the manual's immediate-value tables, not the
> intrinsic's name.
