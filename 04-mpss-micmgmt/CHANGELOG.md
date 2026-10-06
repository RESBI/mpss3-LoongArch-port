# CHANGELOG — Card management library and CLI tools (04-mpss-micmgmt)

 This file records **functional updates** to this package, covering `libmicmgmt.so` plus `mpssinfo`, `mpssflash` and `micsmc`. Each entry is one functional update. Entries run **oldest first — the newest update is at the bottom**.

---

## 2026-10-05 — Library and tools build, install and work on loongarch64

- **Added** `libmicmgmt.so` and the three command-line tools build on loongarch64 and install (library into `$(PREFIX)/lib64`, tools into `$(PREFIX)/bin`, headers into `$(PREFIX)/include`).
- **Added** `mpssinfo` reads the card's SKU, serial number, core count, temperature and flash version. Those values come from two sources: device enumeration, SKU and POST codes are read over sysfs (no privileges needed); the serial number, UUID, memory/core information, temperature and RAS data are read over SCIF, which requires the ability to open `/dev/mic/scif` (root-only by default, available to ordinary users once the udev rule is installed).
- **Fixed** Removed bogus `inline` declarations: several places declared a function `inline` without ever defining it, which has been an error since GCC 14.
- **Fixed** The package's `EXTRA_CFLAGS` no longer carries `-Werror`, so warnings from a newer toolchain do not abort the build. The warnings are still printed, which keeps them visible for later cleanup.
- **Verified** On real hardware `mpssinfo` prints the full set of card information, and `mpssflash` reads the flash version used for comparison against the constants in `miccheck`.

---

## See also

- Per-change porting notes and acceptance criteria: `docs/E-porting-patches.md`
- Size and structure of the host user-space code: `docs/04-host-userspace.md`
- Acceptance tests (T2 card access and device permissions): `tests/`
