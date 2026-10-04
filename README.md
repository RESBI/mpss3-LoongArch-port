# MPSS 3.8.6 Host-Side Toolchain — LoongArch Port, v0.1

This release ports the **host-side toolchain** of Intel MPSS 3.8.6 to LoongArch (loongarch64). Each package lives in its own directory and is installed with a plain `make install`; the only ordering rule is the obvious one — install a library before the programs that link against it. Nothing else requires manual steps.

Validated on: AOSC OS 13.3.1, kernel `7.1.13-aosc-main-16k`, gcc 15.3.0, glibc 2.42, Python 3.14, systemd 259, with a Xeon Phi 7120P card (`8086:225c`). The porting work itself — every individual change, plus the on-hardware acceptance records — is documented in chapters 4, 6 and 8 and appendix E of the accompanying report.

![A LoongArch host seeing the Xeon Phi card, and an SSH session into it](images/screenshot.png)

The screenshot above is the machine this port was validated on. The upper half shows `lspci` picking up `04:00.0 Co-processor: Intel Corporation Xeon Phi coprocessor SE10/7120 series` on a Loongson-3A6000 host running AOSC OS with kernel `7.1.13-aosc-main-16k` and 32 GiB of RAM. The lower half is an `ssh root@171.31.1.2` session on the card: `uname -a` reports the card's kernel, `2.6.38.8+mpss3.8.6` (k1om), and `/proc/cpuinfo` reports 61 cores. Between them, the picture covers the whole chain: LoongArch host, Xeon Phi over PCIe, Linux inside the card.

---

## Copyright and License

**Upstream components.** The MPSS 3.8.6 sources, the card-side boot images and the documentation bundled in this release are copyright Intel Corporation and are distributed under the licenses shipped with them. The `COPYING`, `COPYING.LIB`, `COPYING.BSD` and `COPYING.LGPL` files in each package directory are the authoritative license texts; where they and this file disagree, those files govern. The host kernel module `mic.ko` and its sources are distributed under GPL-2.0.

**Port and packaging.** The modifications made to get the above code running on loongarch64 with a current toolchain, the way this release is organized (the per-package wrapper Makefiles, the helper tools under `00-build-tools`, and this document), are copyright the LoongArch port contributors and are released under the same terms as the files they modify.

**Trademarks.** Intel, Xeon Phi, Many Integrated Core (MIC) and Knights Corner are trademarks or registered trademarks of Intel Corporation. This project is not affiliated with, endorsed by, or supported by Intel.

## Disclaimer

This release is provided **"as is"**, without warranty of any kind, express or implied, including but not limited to the warranties of merchantability, fitness for a particular purpose and non-infringement. You use it at your own risk.

A few points deserve to be spelled out:

1. **This is an experimental port, not an Intel product.** Upstream MPSS 3.8.6 dates from 2016, and Intel discontinued support for the Xeon Phi (Knights Corner) family and for MPSS long ago. The port contains only the changes needed to make the code build and run on LoongArch with a modern kernel and toolchain. No security hardening and no completeness audit were performed.
2. **Hardware and data can be damaged.** This release includes a host kernel module and card-side boot images that perform PCIe device resets, DMA, and firmware interaction on the card. Misuse — loading the module on unsuitable hardware, booting a mismatched image, or issuing flash operations — can damage the device or destroy data. Validate in a non-production environment first, and keep backups.
3. **Limited validation.** All testing was performed on a single machine (the environment listed above) and covers the basic working paths of `micctrl`, `mpssd`, `mpssinfo`, `mpssflash`, `miccheck`, COI, MYO and the kernel module. It does **not** cover multiple cards, large-scale MPI workloads, sustained full load, power management, or hot-plug.
4. **Matching versions is your job.** The card-side images, and constants such as the flash and SMC firmware versions baked into `miccheck`, were taken from the card under test. With a different card, rebuild with the values that card reports.
5. **No liability for consequential loss.** To the maximum extent permitted by applicable law, the authors and contributors accept no liability for any direct, indirect, incidental, special or consequential damages arising from the use of this release.

If you do not agree to these terms, do not use this release.

## About This Port

The porting work was carried out with the assistance of **DeepSeek-V4.1 Flash**: diagnosing and patching the upstream code for loongarch64, tracking down and rewriting the kernel API drift, remastering the card-side initramfs, writing and running the on-hardware acceptance scripts, and compiling the measurements recorded in the accompanying report. Every change was judged against what the real machine actually did — commands that ran, `mpssd` logs from the card, sysfs readings — and each one is recorded in appendix E of that report.

The packaging of this release (the per-package wrapper Makefiles, `00-build-tools`, and this document) was produced under the same assistance. You should still review the changes that matter for your own use case.

---

## 1. Packages and Install Order

| # | Directory | Contents | Installed to | Depends on |
|---|---|---|---|---|
| 0 | `00-build-tools` | Build helpers: `gen-symver-map` (Python 3), `gen-defsym.py`, `mpss-metadata` | — (called by other packages) | — |
| 1 | `02-libscif` | User-space SCIF library `libscif.so`, plus `scif.h` / `scif_ioctl.h` | `/usr/lib64`, `/usr/include` | 00 |
| 2 | `03-mpss-daemon` | `libmpssconfig.so`, host daemon `mpssd`, management CLI `micctrl` (setuid root) | `/usr/lib64`, `/usr/sbin`, `/usr/include/mic` | 02 |
| 3 | `04-mpss-micmgmt` | `libmicmgmt.so`, `mpssinfo`, `mpssflash`, `micsmc` | `/usr/lib64`, `/usr/bin`, `/usr/include` | 02 |
| 4 | `05-miccheck` | Self-test tool `miccheck` (Python 3) | `/usr/bin`, `/usr/src/miccheck` | 04 |
| 5 | `06-mpss-coi` | Offload library `libcoi_host.so` (public ABI names `@@COI_1.0`) | `/usr/lib64`, `/usr/include/intel-coi` | 02 |
| 6 | `07-mpss-myo` | Offload library `libmyo-client.so` (public ABI names `@@MYO_1.0`) | `/usr/lib64`, `/usr/include` | 02 |
| 7 | `08-mic-module` | Host kernel module `mic.ko`, modprobe/udev configuration, kernel headers | `/lib/modules/$(uname -r)`, `/etc`, `/usr/include/mic` | — |
| 8 | `09-boot-images` | Card boot images `bzImage-knightscorner`, `initramfs-knightscorner.cpio.gz` | `/usr/share/mpss/boot` | — |

## 2. Installing the Packages

Each package directory holds two Makefiles: `Makefile.mpss` is the upstream file with the port patches already applied, and `Makefile` is this release's wrapper — build, then install into the paths MPSS expects. **The intended usage is simply to enter a directory and run `make install`:**

```bash
cd 02-libscif        && sudo make install
cd ../03-mpss-daemon && sudo make install
cd ../04-mpss-micmgmt && sudo make install
cd ../05-miccheck    && sudo make install
cd ../06-mpss-coi    && sudo make install
cd ../07-mpss-myo    && sudo make install
cd ../08-mic-module  && sudo make install
cd ../09-boot-images && sudo make install
```

Three rules apply everywhere:

- **Staged installs.** Every package accepts `make install DESTDIR=/tmp/stage`. In that mode neither `ldconfig` nor `depmod` runs, and root is not required.
- **Custom prefix.** `make install PREFIX=/usr/local` (the default is `/usr`). Note that `micctrl` is installed setuid root, so choose a sensible combination of `DESTDIR` and `PREFIX`.
- **Kernel headers.** The module package needs them; it defaults to `/lib/modules/$(uname -r)/build`, overridable with `make install KERNEL_SRC=...`.
- **A two-step flow is recommended.** Run `make` as your normal user first, then `sudo make install`. Installing straight with `sudo make install` performs the build as root and leaves root-owned object files in the source tree; a later rebuild as your own user then fails with `Permission denied` (`can't create objs/xxx.o`) until you run `sudo make clean` or `sudo chown -R $USER .`.

The top level also has a convenience `Makefile` that installs everything in the order above (`sudo make install`, roughly 150 seconds). For debugging, install packages one at a time instead.

## 3. After Installing

### 3.1 Generate the configuration and the card image directory

```bash
sudo micctrl --initdefaults
```

This writes `/etc/mpss/mic0.conf` and builds the card-side filesystem tree (the *MicDir*) under `/var/mpss/mic0/`. The tree already contains `etc/passwd` (host user accounts are merged in), `etc/network/interfaces` for the card's interface, SSH host keys under `etc/ssh`, and each user's `.ssh/authorized_keys`.

### 3.2 Adjust two lines for your machine

```bash
sudo sed -i 's|^Network .*|Network class=StaticPair micip=171.31.1.2 hostip=171.31.1.1 netbits=24 modhost=no modcard=yes mtu=64512|' /etc/mpss/mic0.conf
sudo sed -i 's|^BootOnStart .*|BootOnStart Enabled|' /etc/mpss/mic0.conf
```

`modhost=no` means you configure the host-side interface yourself — recommended, since it keeps MPSS from rewriting your distribution's network configuration. `modcard=yes` lets MPSS write the card-side network configuration into the MicDir. Adjust the addresses to match your subnet.

### 3.3 Start the daemon

Under systemd, use a `Type=simple` unit. **Do not use `Type=forking`**: `mpssd` forks and then has the parent call `pause()` forever, so systemd would always time out.

```ini
# /etc/systemd/system/mpss.service
[Unit]
Description=Intel(R) MPSS control service (LoongArch port)
After=network.target

[Service]
Type=simple
ExecStart=/usr/sbin/mpssd -l
TimeoutSec=60

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload && sudo systemctl enable --now mpss
```

Without systemd, `sudo /usr/sbin/mpssd -l &` works just as well — `-l` keeps it in the foreground and logs to the terminal.

### 3.4 Bring up the host-side interface

```bash
sudo ip addr add 171.31.1.1/24 dev mic0 && sudo ip link set mic0 up
```

The driver creates `mic0` (MTU 64512) when the card reaches the `online` state.

## 4. Verifying the Installation

```bash
micctrl --status                 # expect: mic0: online (mode: linux image: ...)
mpssinfo                         # card SKU, serial, core count, temperature, flash version, ...
miccheck                         # self-test; prints "Status: OK" when everything passes
ssh root@171.31.1.2              # the card's address; the boot image already carries authorized keys
```

To create a user on the card and inject its keys using MPSS's own tooling — the full management path, in which the controller talks to the card's `mpssd` over SCIF:

```bash
sudo micctrl --useradd=<username>   # takes effect both in the host-side MicDir and on the running card
```

What to look for: `[UserAdd] '<username>' Success` in the card's `/var/log/mpssd`, after which you can `ssh` in as that user.

## 5. What the Port Changes

Code written in 2016 runs into two classes of problem on today's toolchain: **architecture coupling** (x86 inline assembly, SSE2) and **stricter tooling** (new binutils rejecting certain `.symver` uses, GCC 10 defaulting to `-fno-common`, GCC 14 promoting implicit function declarations and incompatible pointer types to errors). Every change, with its location, is in appendix E of the accompanying report. The essentials:

| Package | Main changes |
|---|---|
| `03-mpss-daemon` | Added an AOSC/LoongArch branch to the distribution probe (without it, environment initialization fails outright); fixed undefined behaviour in `parse_shadow` that the original code carried (`*lastd[1]` should be `(*lastd)[1]`; it happened not to crash on x86); `getcwd() < 0` → `== NULL`; card-side SSH host keys changed from `rsa1`/`dsa` to `ed25519`/`rsa`/`ecdsa` |
| `04-mpss-micmgmt` | Removed a bogus `inline` declaration (declared but never defined; an error since GCC 14); overrode the `-Werror` buried in `EXTRA_CFLAGS` |
| `05-miccheck` | Ported Python 2 → 3 (`except X, e:`, the shebang, `communicate()` returning bytes, `create_string_buffer().value` being bytes); falls back to `PATH` lookup for commands hard-coded as `/sbin/...` |
| `06-mpss-coi` | Added the `WHAT_loongarch64 := HOST` build branch; replaced `cpuid`/`rdtsc` with equivalents (`sched_getcpu()`, `CLOCK_MONOTONIC`); stopped emitting `.symver` on non-x86 and established the public ABI names through link-time aliases instead |
| `07-mpss-myo` | Added LoongArch to the `INTEL64` detection; `rdtsc` replaced with a monotonic clock; `lock; xaddl` replaced with `__atomic_add_fetch`; `popcount32`/`nlz32` now defined unconditionally; the x86 `REG_ERR` in the signal context is treated as a write; the SSE2 diff layer uses the scalar fallback the source already provides (`-DMYOI_DIFF_I64`) |
| `08-mic-module` | The kernel-module port documented in chapters 3 and 6 of the report (a batch of interface updates: `MAX_ORDER`, `del_timer_sync`, `from_timer`, `get_user_pages`, `tty_alloc_driver`, and others) |
| `09-boot-images` | The card-side initramfs now carries a static `auto mic0` interface configuration and authorized keys. The original delivery had neither; in a normal MPSS flow the host-side `mpssd` supplies them |

## 6. Known Issues and Caveats

1. **Privilege boundary.** Everything backed by sysfs — device enumeration, SKU, POST code, Family/Model — works as an ordinary user. Anything that goes over SCIF — serial number, UUID, memory and core information, temperature, RAS — must first open `/dev/mic/scif`, which is `crw------- root root` by default. This is exactly why `micctrl` is installed setuid root.
2. **Handshake ordering with the card's `mpssd`.** When the card's `mpssd` starts, it connects to port 160 on the host's `mpssd` and sends `MONITOR_START`; **only after that handshake succeeds does it create the thread that listens on port 164**. The host `mpssd` must therefore already be running at the moment the card's daemon starts. If you booted the card by hand before `mpssd` was up, restart the card's `mpssd` once the host daemon is running and the handshake completes. Otherwise every operation that uses port 164 (such as `micctrl --useradd`) will connect to a listener that does not exist.
3. **A failed SCIF connection retries forever inside the kernel.** In `micscif/micscif_api.c`, the connection wait loop takes a `goto retry` when the peer does not answer and the device is still alive. The process never returns to user space, so neither `SIGTERM` nor `SIGKILL` can stop it; only resetting the card or unloading the module ends it. This is upstream behaviour and was not changed by the port.
4. **`System.map` is an empty placeholder.** The card kernel's symbol table is not available, so `mpssd` logs `mmap of System.map failed`. It has no effect on booting.
5. **Man pages.** The `.1`/`.3` man pages require `a2x` (asciidoc) to generate; the wrapper Makefiles do not build them.
6. **`05-miccheck`'s version numbers are build-time constants.** They are written at `make` time from `MPSS_FLASH_VERSION` and `SMC_FW_VERSION`, defaulting to the values the card under test reported (flash `391`, SMC `1.17.6900`). If the self-test reports a version mismatch on another card, rebuild with that card's values.

## 7. Uninstalling

The install paths for every package are listed in the table in section 1; removing those files is enough. For the kernel module, delete `/lib/modules/$(uname -r)/extra/mic.ko*` (or `updates/`, depending on the kernel version) and run `sudo depmod -a`.
