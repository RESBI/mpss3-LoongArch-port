# CHANGELOG — Card-side boot images (09-boot-images)

 This file records **functional updates** to this package, covering the card-side `bzImage` and initramfs (including card network configuration, authorised keys and service start-up entries). Each entry is one functional update. Entries run **oldest first — the newest update is at the bottom**.

---

## 2026-10-04 — Image inventory review

- **Verified** The image contents and ELF headers were checked item by item: `coi_daemon`, `libcoi_device.so.0`, `libmyo-service.so.0` and `libscif.so.0` are all present (all k1om ELF); glibc 2.21 and `ld-linux-k1om.so.2` form the card-side ABI.
- **Recorded** What the image does **not** contain: any OpenMP runtime (`libgomp` must be built separately), any COI application, and any compiler — the card cannot compile, only receive binaries.
- **Recorded** `coi_daemon` starts automatically at boot: the image contains `/etc/init.d/coi` and the `rc?.d` start links, and `pidof coi_daemon` reports a PID once the card is `online`. An earlier COI probe that reported "0 engines" was run against a hand-modified image that lacked this start script — that was the actual root cause.
- **Recorded** `System.map` is an empty placeholder. Without a card kernel symbol table, `mpssd` logs an `mmap of System.map failed` warning; it does not affect booting.
- **Recorded** The card's root file system is a RAM disk (tmpfs, visible in `mount`) and is wiped on every power-up. Offload needs nothing pre-placed there; only native execution (bypassing COI) requires re-deploying binaries each time.

## 2026-10-05 — Card image made usable

- **Added** The initramfs now carries an `auto mic0` static interface configuration. The original delivery had none (MPSS normally pushes it from the host during boot), so with a manual bootstrap — or whenever the host-side push fails — the card has no network reachability and everything above it (`ssh`, `scp`, SCIF-based management) is unreachable.
- **Added** Authorised keys are pre-installed in the initramfs so the host can `ssh` straight into the card.
- **Recorded** The card's `sshd` is OpenSSH 7.4 from 2019 and cannot negotiate with a current `scp`, which reports `Connection closed`. To copy a file to the card use `ssh card 'cat > /tmp/file' < local-file`. Offload itself does not need this step, since COI delivers the program and its dependencies.
- **Verified** On real hardware the card boots, `ssh` login works, `uname -a` reports the card kernel `2.6.38.8+mpss3.8.6`, and `/proc/cpuinfo` shows 61 cores. Covered by `tests/t2_ssh.sh`.

---

## See also

- Card-side runtime inventory and image review: `docs/F-coi-and-openmp.md`
- Boot and card-state checks: `docs/11-verification.md`
- Acceptance tests (T1 image install, T2 card access): `tests/`
