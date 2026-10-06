# Chapter 4 — Host User Space: A Layer of C Code With Almost No x86 in It

> This chapter is about the **user-space** half of MPSS as installed on the host: `mpssd`, `micctrl`, `libmpssconfig`, `libscif`, `micinfo`/`micflash`, and so on.
> Unlike the kernel module, the question for this layer is not "will it compile" but "what is still missing once it compiles".
> The previous chapter is [Chapter 3 — Anatomy of the Host-Side Kernel Module](03-mpss-kernel-module.md), and the next is [Chapter 5 — x86 Coupling Audit](05-x86-coupling-audit.md).
> The line counts and item counts in this chapter come from unpacking and inventorying all 98 installation packages in the delivery one by one (the inventory output is in `_work/findings/rpm-contents/`).

---

## 4.1 What This Layer Actually Delivers

The Linux deliverable of MPSS 3.8.6 is **98 RPMs**, scattered across several subdirectories:

| Location | Contents | Count |
|---|---|---|
| Root directory | Main body of the host user-space binary packages | 48 |
| `dbg/` | DebugInfo, **with source embedded** | 10 |
| `modules/` | Kernel modules (the subject of Chapter 3) | 22 |
| `ofed/` | OFED / DAPL / libibscif / ibpd | 8 |
| `src/` | srpm | 4 |
| `perf/` | `micperf` | 2 |
| `psm/` | PSM | 2 |
| `ganglia/`, `relmon/` | one each | 2 |
| **Total** | | **98** |

The overwhelming majority of the ELFs in these packages fall into two classes: **x86-64**, 349 of them, and **k1om** (ELF `e_machine` = 181), 250 of them (plus 4 of other architectures, one each of SPARCv9/SPARC/PowerPC/i386, recorded in the architecture-statistics section of `_work/findings/_inv_elf.txt`). The latter run on the card and are left entirely untouched here — which is also why the delivery contains a 27,363-file, 659,630,329 B k1om SDK (a prebuilt cross toolchain plus the card-side sysroot); none of it has anything to do with LoongArch.

**Not everything comes with source**, and this has to be sorted out first, or the effort estimate will be wrong. What has source is the main body of the host user space; what does not is a batch of GUI, diagnostic, RAS and PSM components, listed one by one in §4.7.

---

## 4.2 The Parts With Source

The delivery includes a source-archive directory `mpss-src-3.8.6/…/src/` containing **28 `.tar.bz2` files** in total. Of those, the ones the host user space **actually needs recompiled** are these:

| Archive | Size | Artifacts | Porting value |
|---|---|---|---|
| `mpss-daemon-3.8.6.tar.bz2` | 111.5 KB | `/usr/sbin/mpssd` (1,564 lines), `/usr/sbin/micctrl` (17,395 lines), `libmpssconfig.so.0.0.1` (7,306 lines), `micctrl_passwd` (493 lines) | ★★★★★ |
| `libscif-3.8.6.tar.bz2` | 32.2 KB | `libscif.so.0.0.1` (**385 lines**), `/usr/include/scif.h` (a 1,561-line header) | ★★★★★ |
| `mpss-micmgmt-3.8.6.tar.bz2` | 8.63 MB | `micinfo`/`micflash`/`flash1`/`mpssflash`/`micsmc`, `libmicmgmt.so.0.0.2` (21,488 lines) | ★★★★★ |
| `mpss-coi-3.8.6.tar.bz2` | 550.8 KB | COI offload framework, 65,174 lines of C++ | ★★★★ |
| `mpss-myo-3.8.6.tar.bz2` | 484.5 KB | MYO offload/shared-memory abstraction, 32,570 lines | ★★★★ |
| `micperf-3.8.6.tar.bz2` | 308.2 KB | Performance tests, 5,312 lines | ★★ |

That totals **152,913 lines**. Archive by archive: 26,423 (`mpss-daemon`) + 1,946 (`libscif`: 385 lines of `.c` plus 1,561 lines of `.h`) + 21,488 (`mpss-micmgmt`) + 65,174 (`mpss-coi`) + 32,570 (`mpss-myo`) + 5,312 (`micperf`). Counting convention: **all lines** of the `.c`/`.h`/`.cpp` files in the unpacked trees of the six archives, blank lines and comments included, measured file by file with `ReadAllLines` and reproducible item by item (for the command see Appendix B, §B.5, item 5). This excludes `mpss-miccheck`, and also excludes the handful of Python scripts inside `micperf` and all man/UserGuide text.

There is one thing here that looks intimidating by sheer size but is actually light work: **the kernel-module layer is a single module, `mic.ko`**; the user-space layer has a lot of lines, but it is all ordinary C/C++, with no privileged code and no kernel-API binding — changing architecture just means changing compilers.

---

## 4.3 x86 Coupling Is Essentially Absent From User Space

This is the most convenient conclusion in the chapter. The entire user-space source tree was scanned item by item for x86-specific constructs, with the following hits:

| Construct | Where it hits | Notes |
|---|---|---|
| `__x86_64__` conditional compilation | `mpss-myo/src/include/myobasictypes.h:53`, `mpss-coi/src/api/sysinfo/sysinfo_common.cpp:64,88`, `micperf/gemm/bench.c:89,108` | 10 in total, all of the form "if this is x86, print/take the time a certain way"; a rewrite-level change |
| x86 SIMD intrinsics | `mpss-myo/src/consistent/myodiff.c:75,127,128,278` (SSE2), `micperf/gemm/utils.c:31,107,118` (`immintrin.h`) | **Only two files**, and the only genuine instruction-set coupling in the user space |
| Hardcoded x86 paths | **zero hits** | What is hardcoded is all distribution paths and external commands, unrelated to the processor |
| `#include <asm/…>` | **zero hits** | User space includes no kernel headers |
| References to `arch/x86` | **zero hits** | — |

Those two SSE2 sites are not to be ported but replaced: `myodiff.c` uses SSE2 for consistency diffing, and LoongArch's LSX/LASX is a different set of intrinsics written quite differently; `micperf` is a performance-testing tool that can simply be dropped or rewritten as scalar code. Neither sits on the main path (the path where a user program calls SCIF/COI/MYO).

What does need attention is which external commands this layer's code depends on. See §4.5.

---

## 4.4 The Host-to-Kernel Interface Itself Contains Nothing Processor-Specific

This point matters, because it is the entire reason the host user space can be reused so extensively.

The contract between host user space and `mic.ko` takes only three forms:

| Contract | Concrete form | Defined where | Processor-related? |
|---|---|---|---|
| sysfs text attributes | `/sys/class/mic/micN/*`, read and written as line-oriented text | `host/linsysfs.c`, `micscif/micscif_sysfs.c` | No |
| `/proc` files | `/proc/mic_ramoops`, `/proc/mic_vmcore` | `host/uos_download.c`, `host/vmcore.c` | No |
| Character device + `ioctl` | `/dev/mic/*`; `libscif.so.0` is a thin wrapper over it | `host/linux.c`, `include/scif_ioctl.h` | No |

`libscif` is especially telling: the whole library is **385 lines**, and all it does is open `/dev/mic/scif` and then issue `ioctl`s. It contains no architectural assumptions, because it is in no position to have any — it is nothing but an argument packer for `ioctl`.

This contract is the part that **must be frozen** during the port; for the item-by-item inventory see §A.7 and §A.8 of [Appendix A — Interface Contracts](A-interface-contracts.md).

The one place that looks like an x86 check but which **must be kept**: `mpss-daemon/libmpssconfig/verify_bzimage.c` validates that a kernel image is k1om before pushing it to the card — it checks `0x55aa`@510, `"HdrS"`@514, byte 529 equal to 1, `0x1f8b`@530, and ELF `e_machine` equal to `0x3e` or `0xb5`. Here `0x3e` is x86-64 and `0xb5` is `EM_K1OM`, and **what is being checked is the card's kernel, not the host's**. After porting to LoongArch this validation needs not a single character changed; if anything, it is a safety net that prevents pushing the wrong image.

---

## 4.5 Where the Real Work Is: External Commands and What Becomes of Them on the New Platform

The great majority of the changes in this layer land here. One by one:

| External command / path | Where it appears | What it is now | What becomes of it in the LoongArch New World |
|---|---|---|---|
| `/usr/sbin/brctl` or `/sbin/brctl` | `micctrl/network.c:113-145` (path table), `:4031,4061,4090,4119` | Creates the `micbr0` bridge | **`brctl` is deprecated on new kernels** → must become something like `ip link add name micbr0 type bridge` |
| `/sbin/ifconfig` | `micctrl/network.c:4001` | Reads host NIC status | Change to `ip link` and parse it yourself |
| `/sbin/ifup` | `micctrl/network.c:3939` | Brings a host NIC up | The path usually still exists, but `ip link set up` is more robust |
| `/sbin/ifdown` | `micctrl/network.c:3970` | Takes it down | As above |
| `/etc/rc.d/network` | `micctrl/network.c:3908` | Restarts networking on the SUSE branch | Change to `systemctl restart` on the corresponding service |
| `/bin/gzip` | `libmpssconfig/genfs.c:1683,1688,1700,1706`, `verify_bzimage.c:178-186` | Decompresses the card kernel image | The command itself is architecture-independent; it can stay |
| `/bin/cpio` | `libmpssconfig/genfs.c:1719-1737` | Extracts and writes files from the card initramfs | The command is architecture-independent; it can stay |
| `/usr/bin/ssh-keygen` | `micctrl/user.c:2872` | Generates SSH host keys for the card | Architecture-independent; it can stay |
| `systemctl` / `service` | `mpss-micmgmt/apps/mpssdebug/micdebug.sh:607,611` | The debug script grabs service status | The two are interchangeable; keep one |
| `lspci` / `pciutils` / `modprobe` / `setpci` | **zero hits** | All 31 source files of `mpss-daemon` and the `mpss-micmgmt` source have been audited | Nothing to do |

There is one more detail in that last row: many porting assessments budget effort on the assumption that "the management tools will certainly call `lspci` or `modprobe`". This code **does not**. `micctrl` gets its information from sysfs and `/etc/mpss/micN.conf`, not from command output.

---

## 4.6 Python 2.7 Is an Island

The user space contains a body of **Python 2.7** code: 39 `.py` files, 378,128 bytes. These are not peripheral scripts but several formal components:

| Component | Language | Size | Status |
|---|---|---|---|
| `mpss-micmgmt-python` | Python 2 + `libmicmgmt` bindings | 2,198 lines | Must be ported to Py3 or dropped |
| `mpss-miccheck` | Python 2 + `ctypes` | included in the 8,603 lines | Must be ported to Py3 or dropped |
| `micperf` helper scripts | Python 2 | 19 `.py` files | As above |
| `mpss-sysmgmt-micpython` | Python 2.7 scripts + a C extension | 20,319 B + 314,776 B | **The C extension has no source** → not portable |
| `mpss-miccheck-bin` | **a frozen Python 2.6 executable**, 4.73 MB | binary | Drop it outright |

Distributions in the LoongArch New World will not have Python 2.7. This code has only two destinations: rewrite it as Python 3 (`miccheck` is a self-test tool, so the rewrite has value), or drop it together with its component (`miccheck-bin` and `micpython` fall into that class).

One more note on the SDK: `mpss-sdk-k1om` contains 259 Python 2.7 standard-library files. Those are an appendage of the toolchain as Intel packaged it, not MPSS's own code, and they play no part in host-side functionality, so they are left untouched here.

---

## 4.7 The Parts Without Source: The Ones That Can Only Be Dropped

A sizeable batch of components in the delivery **ship as binaries only, with no source**. They are not "hard to port" but "impossible to port", and they must be listed explicitly as drop items in the plan, or they will be mistaken for liabilities:

| Component | Form | Why it is dropped |
|---|---|---|
| `mpss-micsmc-gui` | 13.5 MB C++ GUI depending on `libSDL-1.2` | No source, and it depends on SDL 1.2 |
| `mpss-psm` / `mpss-psm-dev` | PSM (the MPI matching layer over InfiniBand) | No source, and it depends on Mellanox drivers |
| `mpss-mpm` | Remote debugging service | No source (only a shell wrapper) |
| `mpss-sysmgmt-micdiagnostic` | `MicDiag` hardware diagnostics + 4 k1om workloads | No source, and it depends on `libSettings`/`libODMDebug` |
| `mpss-sysmgmt-micras` | `micrasd` RAS daemon, 428 KB | No source |
| `mpss-sysmgmt-relmon` | `relmond` reliability monitor, 407 KB | No source |
| `glibc2.12pkg-libmicaccesssdk0` | `libMicAccessSDK.so` | No source |
| `glibc2.12pkg-libodmdebug0` | `libODMDebug.so` | No source |
| `glibc2.12pkg-libsettings0` | `libSettings.so` | No source |
| `mpss-miccheck-bin` | frozen Python 2.6 | No source, and Py2 is dead |
| `ofed-ibpd` | OFED's ibpd | No source |
| `dapl` / `libibscif` / OFED | DAPL over SCIF, IB verbs provider | Source exists, but LoongArch has no Mellanox drivers → not actually feasible |
| `mpss-ganglia-web` | Ganglia card-metrics collection | The code is on the **card side**, unrelated to the host architecture |
| `mpss-sdk-k1om` | 659 MB k1om toolchain and sysroot | Its target is the card, not the host |
| `mpss-eclipse-cdt-mpm` | Eclipse plug-in | Unrelated to the host architecture |
| `meta-mpss-3.8.6.tar.bz2` | Supposed to be the Poky MPSS layer | **The archive is 0 bytes** — a delivery defect |

The mapping of what is missing and what stands in for it, item by item, is in §E.5 of [Appendix E](E-porting-patches.md). To record the conclusion here first: this inventory carries one positive implication — **nowhere on the host-side main path (`mpssd` / `micctrl` / `libscif` / `libmpssconfig`) is there an irreplaceable third-party proprietary x86 library**. All 96 SONAMEs in the ELF dynamic sections were checked one by one, and everything that stands in the way is in the drop list.

---

## 4.8 Source-Level Re-check: What the Tooling Side Actually Has to Change

This section takes the §4.3 judgment down to specific files: the host-side source packages in the MPSS 3.8.6 delivery were unpacked and scanned one by one. The result is cleaner than expected.

| Component | Source lines | Language | Architectural coupling |
|---|---|---|---|
| `libscif` | 1,826 | C | Only `.symver` (an ELF symbol-version directive, unrelated to the instruction set) |
| `libmpssconfig` | approx. 6,000 | C | None |
| `micctrl` | approx. 13,000 | C | None |
| `mpssd` (host side) | approx. 1,500 | C | None |
| `mpssd` (card side, `mpss-micdaemon`) | 1,777 | C | Runs on the card, unrelated to the host architecture |
| `libmicmgmt` and `mpssinfo`/`mpssflash` | approx. 20,000 | C++ | No SIMD, no `cpuid` |
| `miccheck` | 1,330 | Python | Depends on `libmicmgmt.so.0` |
| `mpss-coi` | 57,578 | C++ | 31 uses of `cpuid`, 1 of `rdtsc` |
| `mpss-myo` | 29,741 | C++ | SSE2 intrinsics (`consistent/myodiff.c`), 2 uses of `__x86_64__` |

The scanning convention was to search these six source trees for `__x86_64__`, `__i386__`, `cpuid`, `_mm_`, `MSR`, `iopl`, `/dev/cpu`. **In the core four (`micctrl`, `mpssd`, `libmpssconfig`, `libscif`) there is not a single hit**; all the hits are concentrated in two places — COI's `src/include/internal/_SysInfo.h` (using `cpuid` to get the APIC ID and topology) and `src/api/perf/perf_common.cpp` (using `rdtsc` for timing) — plus MYO's consistency-checking file. This is exactly where the 10 conditional-compilation sites and 2 SIMD files from §4.3 sit in the source packages: COI and MYO account for the overwhelming majority of them, and they affect only the on-card offload programming interface, not whether this card can be managed at all.

One more piece of structural evidence supports the "it is only a recompile" judgment: `libscif`'s Makefile already carries a cross-compile test.

```makefile
TARGET_ARCH := $(shell $(CC) -dumpmachine | sed -n 's/.*\b\([lk]1om\)\b.*/\1/p')
ALL_CFLAGS_k1om = -D_MIC_SCIF_
```

`-D_MIC_SCIF_` takes effect only when the target triple contains `k1om` or `l1om` (that is, the card side). On LoongArch, `gcc -dumpmachine` is `loongarch64-…`, which falls automatically into the host branch — the same design as the `HOST` split in the kernel module.

Along the way, one interface that had never been examined closely was cleared up. When micctrl creates users on the card and injects keys, it goes through the SCIF management channel: `libmpssconfig/libmpsscommon.h:43` through `:78` is the complete opcode table (`MICCTRL_ADDUSER` = 8, `AU_FILE` = 11, `AU_DONE` = 12, `AU_ACK` = 13, `CHANGEPW` = 28, `SYSLOG_FILE` = 31), the port number `MPSSD_MICCTRL` is 164 (`libscif/scif.h:178`), and the receiving end on the card is the `mpssd` that starts with the card image (`mpss-micdaemon-3.8.6/mpssd.c`). The key step can be done in two ways: when the card boots from a filesystem directory on the host, micctrl edits `etc/passwd`/`etc/shadow`/`home/<user>/.ssh/authorized_keys` in that directory directly; when the card boots from a RAM root and is already booted, it sends those contents over SCIF to the card's `mpssd` using the opcodes above, and the card side writes them to disk. **The card-side server is already running on the card; all the host side lacks is `libscif` plus a client that sends these opcodes** — this is the shortest path to getting the management plane back.

### Results as Measured on Hardware: Everything in That Table Has Been Built and Run

The "how much is expected to change" judgment above was carried through on LoongArch as a complete round of building and runtime verification. The conclusion: **every component listed in the table was built, and four of them ran successfully on the spot**. The item-by-item evidence and the full change list are collected in [Appendix E](E-porting-patches.md); only the conclusion is kept here.

| Component | Result |
|---|---|
| `libscif`, `libmpssconfig`, `mpssd`, `micctrl` | All built; `micctrl --status` reports `mic0: online`, and `micctrl --initdefaults` generates the configuration and the card image directory |
| `libmicmgmt`, `mpssinfo`, `mpssflash` | All built; `mpssinfo` reads back the card's `Vendor 0x8086`/`Device 0x225c`/`Family 0x0b`/`Stepping C0`/`SKU C0PRQ-7120 P/A/X/D` |
| `miccheck` | Ported to Python 3 and working; of the four default host-side tests, the first three pass |
| `libcoi_host.so` | Built; an external program links against it by its published ABI name and succeeds |
| `libmyo-client.so.0` | Built, linked and run; the call enters the library's internal code path |

The distribution of the changes is worth noting: **the core four (roughly 24,000 lines of C) together required touching only three places in the source** — one distribution detection, one piece of undefined behavior in the original code, and one comparison of a pointer against 0. Almost all the remaining changes fall into the "stricter toolchain" category (new binutils' restrictions on `.symver`, GCC 14 promoting implicit function declarations and incompatible pointer types to errors, GCC 10's `-fno-common` default), and those have nothing to do with the instruction set — building this 2016 code with today's x86-64 toolchain means facing exactly the same ones. This in turn confirms the §4.3 judgment: **this layer has no porting problem, only a recompilation problem**.

The conclusion on porting order: the first four rows of the table above (the core management plane, roughly 24,000 lines of C) can proceed on a "recompile plus fix compile-time friction" basis, and the acceptance anchors are already available (`micctrl --status` goes through sysfs; `--sshkeys` goes through SCIF, and the card-side `mpssd` prints `[UserAdd] … Success`); COI and MYO come later, to be done as needed.

## 4.9 Chapter Conclusions

Three sentences.

First, the host user space **does not need porting**, only recompiling. In the whole tree, only 10 conditional-compilation sites and 2 files that use SIMD intrinsics relate directly to x86; there are no hardcoded x86 paths, no SSE/AVX spread, and no inline assembly.

Second, what actually needs work is the two areas of **external commands** and **Python 2.7**: `brctl`/`ifconfig` are deprecated or discouraged on new kernels and must be rewritten using the `ip` family; those 39 Python 2.7 scripts must either be rewritten as Py3 or dropped along with their components.

Third, a batch of components in the delivery has no source; they are not a "deal with it later" item but something to **cross off from the start**. Only once the drop items are crossed off does the remaining effort become visible: the real scope of change in the host user space is the networking and path portions of the roughly 27,000 lines of C in `micctrl`/`libmpssconfig`, plus a few test/self-check tools.

The risk level of this layer is completely different from that of the kernel module: its failure mode is **a path not found, a command erroring out** — visible and fixable — whereas the several F-class risks registered in Chapter 5 are silent. So from an engineering-order standpoint, the user-space layer should be done later — first get the card to light up, then deal with these tools. For the reasoning see [Chapter 8 — Migration Roadmap](08-migration-roadmap.md).
