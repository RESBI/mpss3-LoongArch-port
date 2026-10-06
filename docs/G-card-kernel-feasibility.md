# Appendix G — Giving a KNC Card a Newer Kernel: A Feasibility Study

The main body of this report is entirely about the **host side**. This appendix answers a different question: **can the card itself be given a Linux kernel newer than 2.6.38.8.** The conclusion first: "technically sound, in practice a project in its own right, and not recommended as a first step" — the reasoning and the evidence follow.

---

## G.1 What k1om Really Is

k1om is not an architecture of its own; it is a **processor family** inside `arch/x86`. This is not speculation, it is how Intel's own tree writes it:

| Fact | Source |
|---|---|
| `k1om` uses x86's source directory | the `ifeq ($(ARCH),k1om)` branch in the card-side kernel tree's top-level `Makefile` sets `SRCARCH` to `x86` |
| compiler prefix | `x86_64-k1om` in `arch/x86/Makefile` |
| a separate ELF machine type | from `include/linux/elf-em.h:36`: `EM_L1OM` 180, `EM_K1OM` 181 |
| processor-family config symbol | `config MK1OM` at `arch/x86/Kconfig.cpu:290` |
| platform config symbol | `config X86_MICPCI` at `arch/x86/Kconfig:348`, which does `select MK1OM` |

The `MK1OM` help text in `arch/x86/Kconfig.cpu` puts it more clearly still, and is worth recording as it stands: this processor is an in-order design with four hardware threads, **largely compatible with x86-64 but with a different vector unit**, and those differences **force an incompatible calling convention**, which is why GCC and binutils give it a separate ELF machine type; and it states explicitly that Intel's later products will stay inside the x86 ecosystem and **will not be descendants of this incompatible branch**.

**That "incompatible calling convention" is the key to this whole appendix**: `EM_K1OM` has never appeared in an upstream kernel, so an upstream kernel cannot even execute a k1om user-space program.

## G.2 What Upstream Mainline Did with the MIC Family

Laying the timeline out is the only way to judge what "swapping in a newer kernel" would mean:

| Version | Event |
|---|---|
| 3.13 | `drivers/misc/mic/` enters mainline (two commits, the host driver and the card driver, September 2013, merged by Greg Kroah-Hartman) |
| 4.3 to 4.6 | the SCIF bus, COSM and VOP (virtio over PCIe) are added one after another |
| **5.10** | **the entire MIC driver family is deleted**, with a one-line reason in the commit: "the devices concerned have been discontinued". 66 files and about twenty-one thousand lines are removed, including `include/linux/scif.h`, `samples/mic/mpssd/` and `Documentation/misc-devices/mic/` |
| 2022 | **binutils removes k1om and l1om**: `bfd/cpu-k1om.c`, `k1om_elf64_vec` and the rest go with them |

One point is easy to misjudge and has to be called out: **the `drivers/misc/mic/card/` in mainline is not what runs on this card.** Its `module_init` hard-checks that the CPU signature is "family 11, model 1", which identifies the **VASP / SDV simulator**; real KNC silicon is "family 6, model 11", and Intel's out-of-tree production driver tests for that too. In other words mainline holds only the cognate **x86_64 simulated version**; **the real card-side kernel has never been in mainline, in any version.**

## G.3 Has Anyone Run a Newer Card Kernel

**No precedent was found.** Specifically:

- Intel itself never changed it: MPSS 3.4 through 3.8.6 are all on the 2.6.38.x line, and MPSS 4.x supports Knights Landing only (KNL is ordinary x86_64, so that line ends there of its own accord).
- The single public attempt (the 2015 Stack Overflow question "Booting custom kernel on xeon-phi") died at the bootloader: the error reads verbatim `mykernel.img is not k1om Linux bzImage`, because the build produced `elf32-i386` rather than `elf64-k1om`. The answerer also explained the root cause: MPSS modifies the kernel, and one of those modifications is that **a larger register set on the card must be supported in order to save context**.
- The community MPSS forks (jjkeijser, quantum-ml-geek and others) **all touch the host side only**: they drag `mpss-modules`, that is the host `mic.ko`, onto newer kernels. One common misunderstanding needs clearing up here — `make MIC_CARD_ARCH=k1om` merely **selects the card-side code paths inside the host module**; it does not produce a card kernel. This agrees with the findings in Chapters 3 and 6 of this report: that tree is one tree with two ends.
- Searching `lore.kernel.org` for k1om turns up toolchain noise only; **there has never been a k1om port commit under `arch/x86`**.

## G.4 The Decisive Hardware Fact: There Is No SSE2

No amount of software gets around this one: in k1om's CPUID, **MMX, SSE and SSE2 are all 0**, and there are no XMM registers at all; in their place are 32 512-bit vector registers, 8 16-bit mask registers, and a private set of 4-byte prefixes.

The problem is that **SSE2 is the architectural baseline of x86-64**: the SysV AMD64 calling convention passes floating-point arguments in XMM registers, and modern x86 kernels and compilers assume it exists everywhere. So k1om **is not a legitimate x86-64 target**, however the kernel configuration is written. Intel handled it the same way — giving it a separate ELF machine type and a separate toolchain rather than squeezing it into x86-64.

(One circulating claim corrected in passing: KNC is not an AVX-512 implementation; its vector instructions use a private encoding. "The AVX-512F/CD/ER/PF subset" is a name retrofitted later, and the word EVEX does not appear even once in the card-side ISA manual.)

## G.5 If It Were Really Done: the Scope of a Forward Port

Split the card-related material in Intel's tree into four blocks; the magnitudes are as follows (line and file counts taken from the local card-side kernel tree and host module tree):

| Work block | Content | Magnitude |
|---|---|---|
| Architecture personality | the `arch/x86` changes related to `MK1OM` / `X86_MICPCI`, including the SBox clock source, cache flushing, the `i387` / XSAVE paths (one known erratum among them is that `MXCSR.DUE` must be forced set), CPU probing, entry and ptrace branches, and two sets of linker-script layout | about 3 to 5 thousand lines (the 28 files and 63 lines counted earlier in this report are only the part that explicitly carries the string "k1om"; the real implementation files are a different count altogether) |
| Platform drivers | `drivers/micpm/`, including idle, cpufreq, TMU and MCA, plus `michvc` and `ramoops` | about 7 thousand lines |
| Card-side transport and services | `micscif`, `dma`, `pm_scif`, `vnet`, `vcons`, `mpssboot`, `ras`, `ramoops`, `virtio`, `blcr` | 24 files, 20,232 lines |
| Boot contract | the `bootparams` structure shared by host and card, plus the card-side `mpssboot` reporting boot completion to the host over the SCIF port `MIC_NOTIFY` (defined as 161 at `include/scif.h:175`), with the host side binding the same port at `host/acptboot.c:152` | the contract is small, but get one thing wrong and it never reaches `online` |

The good news is that **the third block is the same body of source as the host side**: the twenty-odd patches this report applied when porting the host side, and that "one tree, two ends" build split, can largely be copied over to the card side, which makes this block lower risk than the architecture block. The real difficulty lies in blocks one and four.

## G.6 Effort and Risk

With "it boots under MPSS and answers a ping" as the goal, for one developer with kernel experience, the card to hand and a working host environment:

| Workstream | Estimate |
|---|---|
| bring `MK1OM` + `X86_MICPCI` + `micpm` + clock sources + floating point / XSAVE up on 4.18 / 4.19 | 6 to 10 person-weeks |
| port the card-side drivers (SCIF, DMA, vnet, vcons, ras, virtio, blcr, mpssboot) | 4 to 8 person-weeks |
| re-establish the `mpssd` / bootparams / port 161 handshake and the image packaging, so the state machine runs through `ready→booting→online` | 3 to 6 person-weeks |
| user-space and initramfs adaptation | 1 to 3 person-weeks |
| total | **about 4 to 7 person-months** |

The recommended target version is **4.18 / 4.19**: that is the generation the community dragged this same driver family to, and the patterns of missing interface patches are closest there. 5.4 adds another 2 to 4 person-months, and 5.10 and above double it — because the code mainline deleted back then has to be added back first, and the port then done on top of it.

The risks, in order of size:

1. **The boot contract is a black box.** The card's boot ROM and the MPSS loader verify the image, and changing the compile-time layout (linker scripts, symbols such as `MIC_KERNEL_VERSION_20626`) can reduce an otherwise correct kernel to a single `not k1om Linux bzImage` or to plain silence. That one public attempt died right here.
2. **SSE2 is missing.** Modern x86 code and its toolchain keep adding new baseline assumptions; this is a running battle.
3. **Undocumented silicon errata.** The only known one is the comment about forcing `MXCSR.DUE`; the rest are scattered through `#ifdef`s in MCE, MTRR, TSC, APIC, ptrace and process switching, which amounts to reverse-engineering Intel's internal knowledge.
4. **No upstream, no review, no future.** Mainline has deleted the family on the grounds that "the devices have been discontinued", and binutils has removed k1om as well, so any port is a permanent private branch.
5. **The hardware is fragile.** GDDR5 ECC, temperature and the PCIe link are all managed by firmware, and if the new kernel's power and idle management fights with the firmware, the card's boot state can be damaged.

## G.7 Recommendations

Two cases:

- **If the goal is "it works"**: do not touch the card kernel. Take one of two lightweight routes — first, backport into 2.6.38.8 the several `micscif` / SCIF / VOP defects that upstream fixed later, and rebuild; second, **give the card a new user space** (build a new BusyBox and OpenSSH with the k1om toolchain, redo MicDir, and let `mpssd` pack it into an initramfs), which clears the security debt of that 2019 software without going anywhere near the kernel. The second shares its toolchain prerequisites with Appendix F.
- **If the goal is "research"**: start with a **two-to-three-week time-boxed experiment** that verifies exactly one thing — "can an `ARCH=k1om` 2.6.38.8 kernel, rebuilt with a toolchain you built yourself, boot normally under MPSS 3.8.6". That step can confirm or refute the biggest unknown (the boot contract), after which you decide whether to invest in the 4.18 port. **Do not skip it and go straight to 4.18.**

## G.8 How This Relates to the Rest of the Report

This appendix does not contradict the main body; the two corroborate each other: the main body shows that porting the host side does not require touching the card (the card runs its own Linux, and the host merely loads an image and rings a doorbell), while this appendix shows that **the moment the card side has to be touched, the cost is far higher than on the host side** — because the host side faces interface drift (enumerable, clearable grind) whereas the card side faces architectural incompatibility (`EM_K1OM` and the missing SSE2) plus a black-box boot contract.

In other words: the results of this project's port **do not lose value because the card's kernel is old**; quite the opposite — once the host side works, the card can go on serving as a stable compute device for many years, and the part that genuinely needs updating (the user-space software stack) has nothing to do with the kernel.
