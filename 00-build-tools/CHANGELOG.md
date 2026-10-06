# CHANGELOG — Build helper tools (00-build-tools)

 This file records **functional updates** to this package. Each entry is one functional update: what changed, why, and how it was verified. Entries run **oldest first — the newest update is at the bottom**.

 The package ships three build-time tools: `gen-symver-map` (generates symbol-versioning scripts), `gen-defsym.py` (generates symbol aliases / defsym files from a shared library), and `mpss-metadata` (version and provenance metadata).

---

## 2026-10-04 — Tools packaged with the release

- **Added** The package installs nothing by itself; it is invoked by the build of the other packages. Its wrapper Makefile follows the same shape as every other package (`build` / `install` / `clean`, honouring `DESTDIR` and `PREFIX`).
- **Verified** On the LoongArch machine (Python 3.14) the tools run as part of each package's build; the version and commit recorded in `mpss-metadata` match `build_scmver` reported by `modinfo` on the built module.

## 2026-10-05 — Two paths for symbol versioning and aliases

- **Added** `gen-symver-map` was ported to Python 3. The original implementation required Python 2 and was unusable on current distributions; this tool generates the `.symver` scripts that pin the public ABI names exported by `libscif`, `libcoi_host` and `libmyo-client` (such as `SCIF_1.0`, `COI_1.0`, `MYO_1.0`). Command line and output format are unchanged, so build scripts need no edits.
- **Added** `gen-defsym.py`: after binutils tightened its handling of `.symver`, non-x86 hosts needed an equivalent way to produce the same public ABI names. This tool emits them as link-time aliases instead of `.symver` directives, so lookups such as `dlvsym(..., "COI_1.0")` still resolve. Builds on x86 hosts may keep using `.symver`.
- **Verified** All three libraries build with the new implementation; `objdump -T` shows the expected versioned symbol names, and a host program can resolve them by version name.

---

## See also

- Per-change porting notes and acceptance criteria: `docs/E-porting-patches.md`
- Release overview: `README.md`
