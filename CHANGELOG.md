# CHANGELOG — MPSS 3.8.6 LoongArch port

 This is the **project-level** change log: a dated summary of functional updates, each entry pointing at the CHANGELOG of the sub-project that holds the details. The Chinese edition is [CHANGELOG_CN.md](CHANGELOG_CN.md).

 Convention: one entry per functional update; the heading date is the day the update landed; entries run **oldest first — the newest update is at the bottom**, so appending is always the last step.

## Sub-projects

| Sub-project | Directory | CHANGELOG |
|---|---|---|
| Build helper tools | `00-build-tools/` | [CHANGELOG.md](00-build-tools/CHANGELOG.md) |
| User-space SCIF library | `02-libscif/` | [CHANGELOG.md](02-libscif/CHANGELOG.md) |
| Host daemon and management CLI | `03-mpss-daemon/` | [CHANGELOG.md](03-mpss-daemon/CHANGELOG.md) |
| Card management library and tools | `04-mpss-micmgmt/` | [CHANGELOG.md](04-mpss-micmgmt/CHANGELOG.md) |
| Self-test tool | `05-miccheck/` | [CHANGELOG.md](05-miccheck/CHANGELOG.md) |
| Offload runtime (COI) | `06-mpss-coi/` | [CHANGELOG.md](06-mpss-coi/CHANGELOG.md) |
| MYO runtime | `07-mpss-myo/` | [CHANGELOG.md](07-mpss-myo/CHANGELOG.md) |
| Host kernel module | `08-mic-module/` | [CHANGELOG.md](08-mic-module/CHANGELOG.md) |
| Card-side boot images | `09-boot-images/` | [CHANGELOG.md](09-boot-images/CHANGELOG.md) |
| Documentation and offline site | `docs/` | [CHANGELOG.md](docs/CHANGELOG.md) |
| Acceptance test suite | `tests/` | [CHANGELOG.md](tests/CHANGELOG.md) |

---

## 2026-10-02

- **Static audit completed** The report body — 12 chapters plus appendices A–D — was finalised: the port is feasible, 86 lines of kernel API debt were identified, only two places are genuinely tied to x86, and the one item that could veto the whole project is whether the firmware grants an 8 GiB prefetchable window above 4 GiB. → [docs](docs/CHANGELOG.md)

## 2026-10-04

- **Host kernel module built and probed on LoongArch for the first time** `mic.ko` is a LoongArch ELF with readable `modinfo`; BAR0 receives 16 GiB above the 4 GiB boundary, both DMA masks are 64-bit, the card reaches `online` 26 seconds after the boot command, and host-to-card ICMP shows 0% loss. → [08-mic-module](08-mic-module/CHANGELOG.md)

## 2026-10-05

- **Entire user-space tool side builds and the management plane works on real hardware** `libscif`, `libmpssconfig`, `mpssd`, `micctrl`, `libmicmgmt`, `mpssinfo`, `mpssflash`, `micsmc` and `miccheck` all land on loongarch64; `micctrl --status`, `mpssinfo`, `miccheck`, SSH login and creating card users over SCIF were exercised on the spot. → [02-libscif](02-libscif/CHANGELOG.md), [03-mpss-daemon](03-mpss-daemon/CHANGELOG.md), [04-mpss-micmgmt](04-mpss-micmgmt/CHANGELOG.md), [05-miccheck](05-miccheck/CHANGELOG.md)
- **System integration** The host daemon starts itself through a systemd unit (`Type=simple` plus foreground operation); the `mic0` interface is configured automatically by the shipped unit and script from MPSS configuration; device permissions are relaxed by a modern udev rule so ordinary users can work. → [03-mpss-daemon](03-mpss-daemon/CHANGELOG.md), [08-mic-module](08-mic-module/CHANGELOG.md)
- **k1om toolchain usable on LoongArch (companion)** The k1om compiler shipped with MPSS now runs on LoongArch and is frozen into a wrapper script; the card-side `libgomp` that MPSS never provided is built from source; the card-side offload worker is built and loaded on the card. First real computation on the card: a 200-million-term reduction, 36.3× faster on 61 threads. → [06-mpss-coi](06-mpss-coi/CHANGELOG.md), "Companion items"
- **MYO host library** `libmyo-client.so` builds and runs on loongarch64, with each x86-specific implementation (`rdtsc`, `lock; xaddl`, the SSE2 difference layer) replaced by an equivalent. → [07-mpss-myo](07-mpss-myo/CHANGELOG.md)
- **Card-side boot image made usable** The initramfs gained a static interface configuration and authorised keys (the original delivery had neither, since MPSS normally pushes them from the host). → [09-boot-images](09-boot-images/CHANGELOG.md)
- **Two paths for symbol versioning and aliases** The symbol-version map generator moved to Python 3; a new tool generates link-time aliases from a shared library so public ABI names (`COI_1.0` and friends) survive on non-x86 hosts. → [00-build-tools](00-build-tools/CHANGELOG.md)
- **Release packaging (v0.1)** Nine packages, each installed with a plain `make install`, with staged installs and prefix override supported; the top-level Makefile installs them all in dependency order. → [README.md](README.md)

## 2026-10-06

- **Page-size and cross-side struct-layout fixes** Two classes of defect on hosts whose page size is not 4 KiB were fixed together: registration length is validated in 4096-byte protocol pages with conversion in both directions, and the misaligned spinlock inside a `packed` struct was resolved by turning the wait queues into pointers plus equally-sized padding while keeping the layout byte-identical to the card side. The data path was confirmed by the card itself, byte for byte. → [08-mic-module](08-mic-module/CHANGELOG.md)
- **Offload works end to end** Host-side COI can now create a process on the card, resolve a function by name, run card-side OpenMP and bring results back. Three blocking defects were fixed (creation-command copy, peer window length, missing `-rdynamic` on the card side). Measured: 240 threads, relative error −3.35e-16, and an N-body example whose checksum matches the host reference bit for bit. → [06-mpss-coi](06-mpss-coi/CHANGELOG.md)
- **Acceptance test suite created and green on real hardware** T0–T8 plus two helper scripts; 72 passed, 0 failed, 1 skipped. T4 covers the data path across seven lengths, T5 covers COI end to end, T7 covers heavy compute and bit-for-bit agreement. → [tests](tests/CHANGELOG.md)
- **Test suite decoupled from the environment and shipped with the tree** The test project became a top-level directory of the release tree, and every path now resolves as "environment variable → `tests/config.sh` → autodetection", skipping explicitly when something is missing and telling the user which variable to set. Measured: a single line of configuration is enough for a green run. → [tests](tests/CHANGELOG.md)
- **Offload programming manual rewritten** Reorganised from project chronicle into a manual built around the **COI API**: seven parts plus six appendices, every API explained function by function (all 63 public functions declared in the headers are covered), with a "find it by question" index, and no dependence on any particular machine layout. → [docs](docs/CHANGELOG.md)
- **Report write-back and a self-correction** The report gained appendix J (acceptance test suite); appendix H gained the COI bring-up record and a **retracted criterion** (masked compressed stores are not a valid crash predictor); section I.11.4 changed from open item to resolved, with its earlier attribution marked wrong. → [docs](docs/CHANGELOG.md)

---

## 2026-10-06 — Bilingual documents and final directory layout

- **Added** English editions for the entire document set: twelve chapters, appendices A–K, the report guide and the *KNC offload Programming Manual* — 26 English documents. The naming convention is a pair (Chinese `*_CN.md`, English the same name without `_CN`), and cross-references point at the edition in the same language. → [docs](docs/CHANGELOG.md)
- **Changed** Document layout settled: `docs/` (report and programming manual) and `tests/` (acceptance test suite) sit at the release-tree root alongside the nine packages, and both sub-projects carry bilingual READMEs and CHANGELOGs. → [docs](docs/CHANGELOG.md), [tests](tests/CHANGELOG.md)

## 2026-10-06 — T8 (bulk transfer) and the second batch of page-size fixes

- **Added** A new acceptance stage, **T8**: 4 GiB pushed to the card by default, with a card-side large `malloc`, per-window verification, whole-buffer checksums on both sides and a bandwidth measurement. All three sizes pass on real hardware, 4 GiB is **bit for bit** identical on both sides, at 325.9 MB/s wire-side and 98.8 MB/s end-to-end. → [tests](tests/CHANGELOG.md)
- **Fixed** The second batch of page-size defects in the driver (the host's own window counts scaled twice; the two branches of `micscif_get_dma_addr` disagreeing about the page size while peer counts were divided into zero). Before the fix, ten seconds after T4 the SMPT refcount underflowed and the RMA path ended up wedged beyond recovery, with a reboot the only way out. → [08-mic-module](08-mic-module/CHANGELOG.md)
- **Changed** The report gained section I.12 (the defect chain and its criteria) and appendix J's T8 measurements plus two more baked-in pitfalls. → [docs](docs/CHANGELOG.md)

## Convention

- This file records functional updates only, not refactoring or wording changes. The criteria and evidence for an update live in the sub-project's CHANGELOG.
- Each sub-project maintains its own CHANGELOG; this file carries a one-sentence summary and a link.
- To add an entry, append a new dated section at the **bottom** (several sections may share a date) and put the details in the sub-project's CHANGELOG.

## 2026-10-07 — A defence against malformed peer descriptions, diagnostics behind a switch, RMA ceiling calibrated, three new specification appendices

- **Fixed**　The host gained a window-description integrity check, and `micscif_get_dma_addr()` now returns `RMA_ERROR_CODE` instead of `BUG_ON(1)`: the same bad input (a 32 MiB card window) goes from "kernel BUG plus a process in `D` state plus a reboot" to "a failure that states its reason". Details in [08-mic-module/CHANGELOG.md](08-mic-module/CHANGELOG.md).
- **Changed**　All diagnostic prints moved behind the `MIC_DEBUG` switch (silent by default) while failure reasons stay always on. See [08-mic-module/CHANGELOG.md](08-mic-module/CHANGELOG.md).
- **Calibrated**　A single `scif_writeto` is now **verified up to 1 MiB** (26496 B then 64/128/256/512 KiB and 1 MiB, checksums matching at every step), where 26496 bytes had been a conservative guess. Details in [tests/CHANGELOG.md](tests/CHANGELOG.md).
- **Added**　Three appendices: K (user-side constraints for memory movement), L (per-API determinism rules) and M (portability and compatibility); Appendix F gained **F.9**, a constraint checklist that points back at K and L. `docs/` holds 44 matched Chinese/English pairs. Details in [docs/CHANGELOG.md](docs/CHANGELOG.md).
- **Added**　Tests: `tests/extra/`, a fine-grained case set separate from `run_tests.sh`, with generic probes and cases x01-x04. Details in [tests/CHANGELOG.md](tests/CHANGELOG.md).
- **Corrected**　Appendix I's I.12.6 argument about a "twelve-bit page ceiling" was **disproved by source and measurement** and rewritten as the measured verdict: the card writes only the first page of its chunk table (512 entries). Rebuilding the card's kernel module is now recorded as an **optional path** (Appendix K, K.6, with recipe and cost). Details in [docs/CHANGELOG.md](docs/CHANGELOG.md).
- **Checked**　Every file in `patches/` is md5-identical to the source being compiled on the Loongson machine, and the shared release tree matches byte for byte.

## 2026-10-07 — T9 GEMM floating-point benchmark added to the acceptance suite

- **Added**　A new acceptance stage, **T9**: matrix multiplication benchmark at three scales (256²/1024²/2048²), both FP32 and FP64, with host-side reference verification. Integrated into `run_tests.sh` after T8. Real hardware (7120P @ 61 threads) delivers 0.33-0.49 GFLOPS depending on scale and precision. → [tests](tests/CHANGELOG.md)
- **Fixed**　T9's performance extraction regex now matches the actual output ("卡端算力" instead of "卡端性能"), so GFLOPS numbers display correctly in the summary.

## 2026-10-07 — fix the build order and the intra-package self-dependency of 04-mpss-micmgmt (exposed by make uninstall)

- **Top-level order**　`PKGS` now lists `08-mic-module` before `04-mpss-micmgmt`, with a header comment explaining why: 04's build needs the `mic/io_interface.h`, `mic/micras_api.h` and friends that 08's `dev_install` provides. Since `build` does not produce them, a fresh tree — or one that has just run `make uninstall` — must first run `sudo make -C 08-mic-module install`.
- **Intra-package self-dependency**　04's `apps/*` used to find `miclib.h` through `$(PREFIX)/include` and `libmicmgmt.so` through `$(PREFIX)/lib64`, both of which only exist once 04 itself is installed, making `install: build` a chicken-and-egg. They now take both from the **source tree**: a new `APPSEARCH` (`CPLUS_INCLUDE_PATH=$(CURDIR)/miclib/include`, `LIBRARY_PATH=$(CURDIR)/miclib/libs`) is used for the three apps, while `miclib` keeps `SEARCH_ENV` so that `/usr/include` is not added to `C_INCLUDE_PATH` twice — which would break libstdc++'s `#include_next <stdlib.h>`. `APPFLAGS` also gains `-I$(CURDIR)/miclib/include`.
- **Why, and the evidence**　After `make uninstall` removed `/usr/include/mic/*.h` (08's `dev_install`) and `/usr/include/miclib.h` (04's install), a plain `make -j8` reported, in turn, that `<mic/io_interface.h>`, `<mic/micras_api.h>`, `<miclib.h>` and finally `-lmicmgmt` could not be found — two **pre-existing implicit dependencies** that had been masked by an already-installed system. With this change 04 builds without being installed first.

