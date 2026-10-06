# CHANGELOG — Self-test tool (05-miccheck)

 This file records **functional updates** to this package. Each entry is one functional update. Entries run **oldest first — the newest update is at the bottom**.

---

## 2026-10-05 — Self-test migrated to Python 3 and green on real hardware

- **Changed** Ported from Python 2 to Python 3, in four categories: the `except X, e:` syntax; the shebang; `subprocess.communicate()` returning bytes (which must be decoded before comparison); and `create_string_buffer().value` also being bytes. Under Python 3 the original code raised exceptions in several places, so the self-test never reached a result.
- **Fixed** External-command lookup: paths such as `/sbin/…` were hard-coded, which produced spurious failures across distributions. The code now tries the hard-coded path first and falls back to searching `PATH`.
- **Added** Two build-time constants, `MPSS_FLASH_VERSION` and `SMC_FW_VERSION`, written at `make` time with the flash and SMC versions of the card under test (defaults taken from the card used in validation). If the self-test reports a version mismatch after swapping cards, re-run `make install` with the new values — no code change needed.
- **Verified** On real hardware `miccheck` reports `Status: OK` across the board. The one item that goes over SCIF — "ras daemon available" — needs device permissions (or root): an ordinary user sees that item fail, which follows from the default permissions on `/dev/mic/scif` and is not a porting defect.

---

## See also

- Per-change porting notes and acceptance criteria: `docs/E-porting-patches.md`
- Device permissions and the privilege boundary: `docs/E-porting-patches.md` §E.7
- Acceptance tests (T2 card access; self-test and device permissions): `tests/`
