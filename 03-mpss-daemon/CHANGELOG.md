# CHANGELOG — Host daemon and management CLI (03-mpss-daemon)

 This file records **functional updates** to this package, covering `libmpssconfig.so`, the host daemon `mpssd`, the management CLI `micctrl`, and the systemd units and device-permission rules shipped with it. Each entry is one functional update. Entries run **oldest first — the newest update is at the bottom**.

---

## 2026-10-05 — Porting fixes

- **Fixed** Distribution detection gained an AOSC / LoongArch branch. Without it, environment initialisation fails outright and every management command becomes unusable.
- **Fixed** Undefined behaviour in `parse_shadow`: `*lastd[1]` should be `(*lastd)[1]`. It happened not to crash on x86, but is not safe to carry to another architecture.
- **Fixed** `getcwd() < 0` changed to `== NULL` — the original test has the wrong semantics even on the success path.
- **Changed** Card-side SSH host keys switched from `rsa1` / `dsa` to `ed25519` / `rsa` / `ecdsa`. The former are removed or disabled by default in current OpenSSH, so an image built the old way leaves the card's `sshd` unable to start.

## 2026-10-05 — Management paths proven on LoongArch

- **Added** `micctrl` is installed setuid root. This is a deliberate design choice: reading sysfs needs no privileges, but anything that goes over SCIF must first open `/dev/mic/scif` (root-only by default), and `micctrl` does both.
- **Verified** On real hardware: `micctrl --status`, `mpssinfo`, `miccheck` and SSH login to the card all work; `micctrl --useradd=<user>` works provided the host `mpssd` is listening when the card-side daemon starts, after which the card's `/var/log/mpssd` shows `[UserAdd] '…' Success`.
- **Recorded** A timing constraint: when the card-side `mpssd` starts it first connects to port 160 on the host `mpssd` and sends `MONITOR_START`, and **only creates the thread that listens on port 164 after that handshake succeeds**. If the card starts before the host daemon, every operation that uses port 164 connects to a listener that does not exist; restarting the card-side `mpssd` after the host daemon is up fixes it.

## 2026-10-05 — System integration: the stack starts itself on current distributions

- **Added** `mpss.service` runs `mpssd -l` with `Type=simple`. `mpssd` forks by default and the parent then calls `pause()` and never exits, so `Type=forking` leaves systemd waiting for a PID file until it times out. Installation triggers `daemon-reload` and `enable`, and starts the service if the kernel module is already loaded.
- **Added** `mic0-net.service` and `/usr/libexec/mpss/mic0-up.sh`: as soon as the `mic0` interface appears, the script applies the address and MTU from the `Network` line of `/etc/mpss/mic0.conf`. The reason for the script is that `modhost=yes` edits the *distribution's* network configuration (Debian's `/etc/network/interfaces`, or Red Hat's `ifcfg-*`), which does not exist on AOSC-style systems; the script configures the interface itself while keeping MPSS configuration as the single source of truth.
- **Added** A modern udev rule relaxes `/dev/mic/scif` and `/dev/mic/ctrl` to mode 0666. The original rule's legacy `NAME=` form is ignored by systemd-udev, leaving the devices root-only. The rule file is installed by the kernel-module package.
- **Verified** On real hardware `micctrl --status` reports `mic0: online`; `systemctl status mpss` is healthy; `mic0` comes up automatically with the expected address.

---

## See also

- Per-change porting notes and acceptance criteria: `docs/E-porting-patches.md`
- Installation and verification steps: `docs/11-verification.md`
- Acceptance tests (T1 install and start-up): `tests/`
