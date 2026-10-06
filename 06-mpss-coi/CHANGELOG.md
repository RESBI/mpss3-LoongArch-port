# CHANGELOG — Offload runtime: host-side COI (06-mpss-coi)

 This file records **functional updates** to this package, whose main artefact is the host-side `libcoi_host.so`. A final section, "Companion items", covers the k1om toolchain and card-side runtime that this package needs in order to work at all (they are not code from this package, but without them it is unusable). Each entry is one functional update. Entries run **oldest first — the newest update is at the bottom**.

---

## 2026-10-05 — Build and ABI handling

- **Added** The package builds and installs on loongarch64 (library, headers, and host-side tools such as `micnativeloadex`).
- **Recorded** The card-side `libcoi_device.so.0` and `coi_daemon` ship inside the card image; this package contains no card-side binaries.

## 2026-10-06 — Host-side COI working end to end

- **Added** The full path works on real hardware: enumerate engines → `COIProcessCreateFromFile` returns `COI_SUCCESS(0)` → create a pipeline → resolve a function handle by name → enqueue a call → wait for the completion event → collect results → destroy pipeline and process.
- **Fixed** Three defects that blocked process creation: (1) the 26,496-byte creation command was copied with a wrong per-page stride, so only the first page arrived correctly; (2) the peer window length was computed in host pages instead of being converted to the 4 KiB protocol page; (3) the card-side program was linked without `-rdynamic`, so the exported symbol never entered the dynamic symbol table and the card's `dlsym` could not find it (the host saw `COI_DOES_NOT_EXIST(5)`).
- **Changed** How public ABI names are produced: non-x86 hosts no longer emit `.symver` directives (newer binutils restricts them) and instead generate identically-named link-time aliases. As a result, lookups by version name such as `dlvsym(handle, "…", "COI_1.0")` still resolve — which matters for existing consumers such as `liboffloadmic`.
- **Fixed** The two x86-specific instructions `cpuid` and `rdtsc` were replaced by equivalents (`sched_getcpu()` and `CLOCK_MONOTONIC`).
- **Added** A `loongarch64` branch in the build system; previously any unknown host architecture was simply reported as unsupported.
- **Verified** On the card: a 2×10⁸-term OpenMP reduction across 240 hardware threads with a relative error of −3.35e-16; an N-body gravity example whose checksum matches the host reference implementation **bit for bit** (difference 0.000e+00) across all three problem sizes. Covered by `tests/t5_offload.sh` (17 checks) and `tests/t7_nbody.sh` (9 checks).
- **Recorded** Two measured limitations, documented in the offload programming manual: `COIBufferCreate` returns `COI_OUT_OF_MEMORY(13)` on this port, and `COIEngineGetInfo` fails its struct-size check. Neither affects engine enumeration, process creation, function-handle lookup or enqueuing calls.

---

## Companion items (not code from this package, but required to use it)

- **2026-10-05 — k1om cross toolchain usable on LoongArch**: the x86_64 k1om compiler shipped with MPSS now runs on LoongArch (through a user-space emulation layer) and produces binaries whose `readelf -h` reports `Intel K1OM`; the invocation is frozen into a wrapper script. Three obstacles were resolved: the RPM extractor only unpacked the inner payload (switched to `bsdtar`), the compiler's internal programs were missing `libmpc` / `libmpfr` / `libgmp`, and the SDK's `as` / `ld` are symlinks with absolute targets that break when the tree moves.
- **2026-10-05 — self-built card-side `libgomp`**: MPSS's compiler configuration hard-codes `--disable-libgomp`, so neither the SDK nor the card image contains it. Built separately in cross mode: a 720,422-byte `libgomp.so.1.0.0` (`Machine: Intel K1OM`), installed into the compiler's runtime directory so `-fopenmp` works out of the box.
- **2026-10-05 — card-side worker `offload_target_main`**: built with the same toolchain (90,073 bytes, card ELF); all dependencies resolve and it loads on the card and enters its own main flow.
- **2026-10-06 — one acceptance criterion retracted**: the presence of `vpackstore` / `vscatter` instructions in a disassembly is **not** a valid predictor of card-side crashes. The `-O0` binary that runs fine contains 12 of them, while the crashing `-O1` / `-O2` builds contain only 11 each. The rule is now "measure the optimization level per source file".

---

## See also

- Programming manual (COI API usage and reference): `docs/OFFLOAD_GUIDE.md`
- Full record of the bring-up, with measured numbers: `docs/H-offload-field-notes.md`
- Route comparison (why compiler-generated offload does not work): `docs/F-coi-and-openmp.md`
- Acceptance tests: `tests/`
