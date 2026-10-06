# CHANGELOG — MYO runtime (07-mpss-myo)

 This file records **functional updates** to this package, covering the host-side `libmyo-client.so` (its counterpart, the card-side `libmyo-service.so`, ships inside the card image). Each entry is one functional update. Entries run **oldest first — the newest update is at the bottom**.

---

## 2026-10-05 — Host library builds on loongarch64 and works

- **Fixed** `INTEL64` detection gained a LoongArch case: the build script decided pointer width and target platform from a hard-coded set of architecture checks, and an unknown architecture fell into the wrong branch, producing incorrect pointer-width and alignment compile options.
- **Fixed** `rdtsc` (the x86 timestamp counter) replaced by a monotonic clock (`CLOCK_MONOTONIC`), preserving the original semantics of measuring an interval.
- **Fixed** The `lock; xaddl` atomic increment replaced by `__atomic_add_fetch`, so the inline assembly no longer breaks compilation on non-x86 targets.
- **Fixed** `popcount32` and `nlz32` are now defined unconditionally. The original code provided inline-assembly implementations only under certain architecture branches, leaving the symbols undefined elsewhere.
- **Changed** In the signal context, the x86 `REG_ERR` field is treated as a write. On x86 that field encodes a fault error code (including the read/write direction bit); other architectures have no equivalent register, so MYO's signal handler has to fall back to a conservative default.
- **Changed** The SSE2 difference layer now uses the scalar fallback the source already provides (`-DMYOI_DIFF_I64`). The original implementation used SSE2 intrinsics for vectorised differencing, which has no counterpart on non-x86 targets; the scalar path produces correct results, with performance that depends on the data size.
- **Verified** The host `libmyo-client.so` builds, installs and its call path works on real hardware (alongside COI, over SCIF to the card). The host-side SCIF/COI channel is covered by `tests/t5_offload.sh`.

---

## See also

- Per-change porting notes and acceptance criteria: `docs/E-porting-patches.md`
- The two ways to use the card (native OpenMP, hand-written COI/MYO): `docs/F-coi-and-openmp.md`
- Programming manual (API and usage): `docs/OFFLOAD_GUIDE.md`
