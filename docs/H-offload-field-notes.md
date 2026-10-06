# Appendix H — Offload Field Notes: the k1om Toolchain, libgomp and On-Card OpenMP

Appendix F covers offload's three routes and their criteria; this appendix records **what was actually done by hand and what numbers came out of it**. Every step was carried out on real hardware — the LoongArch host with the card installed — and the logs are kept in `~/XeonPhiX100-LoongArch/offload-logs/`, so each step can be re-run.

---

## H.1 Goal and Starting Point

The goal was "to compile and link the offload-related parts with k1om's own cross compiler". The starting point was three things already on hand:

| Asset | Location | Contents |
|---|---|---|
| MPSS k1om SDK | `mpss-sdk-k1om-3.8.6-1.x86_64.rpm` (198 MB) | k1om `gcc`/`g++`/`as`/`ld` (GCC 5.1.1), a **sysroot of 1366 headers and 237 libraries**, and card-side `libcoi_device`/`libmyo-service`/`libscif` |
| GCC 5.1.1-knc source | `gcc-5.1.1-knc-master.zip` (158 MB) | the k1om backend patches **plus** upstream's `liboffloadmic`/`intelmic-mkoffload`/`t-intelmic` offload machinery |
| Card and host | Ready | The card is `online` and `coi_daemon` is running; the ported host-side `libcoi_host` can already enumerate engines (Appendix F.1) |

## H.2 Getting the x86_64 k1om Compiler to Run on LoongArch

The compiler in the SDK is an **x86_64 ELF** and cannot be executed directly on LoongArch. The host has **box64** installed with `binfmt_misc` registered, so x86_64 programs — including the `cc1`/`as`/`ld` the compiler invokes internally — run transparently. Three snags came up in practice and were dealt with:

| Symptom | Root cause | Fix |
|---|---|---|
| No toolchain in the extracted directory, header count 0 | `7z` only unpacks an RPM as far as the inner cpio payload | Switched to **`bsdtar`**, which extracts the full 705 MB tree in one step |
| `cc1` reports `Loading needed libs in elf .../cc1` the moment it starts | `cc1` depends on the SDK's own `libmpc`/`libmpfr`/`libgmp` | Point `LD_LIBRARY_PATH` at the SDK's `x86_64-mpsssdk-linux/{lib,usr/lib}` |
| `as: unrecognized option '--march=k1om'` | The SDK's `as`/`ld` are **absolute symlinks pointing into `/opt/mpss/...`**; move the tree and they dangle, so gcc fell back to invoking the system LoongArch assembler | Rewrite the tree's 11 absolute links as **relative links** (script `fix_sdk_symlinks.py`), then point gcc at them with `-B<SDK>/bin -B<libexec>` |

The invocation, once settled (the `k1om-cc` script is exactly this, ready for `configure`/`make` to use directly):

```bash
SROOT=~/XeonPhiX100-LoongArch/k1om-sdk/opt/mpss/3.8.6/sysroots/x86_64-mpsssdk-linux
SYSR=~/XeonPhiX100-LoongArch/k1om-sdk/opt/mpss/3.8.6/sysroots/k1om-mpss-linux
LD_LIBRARY_PATH=$SROOT/lib:$SROOT/usr/lib:$SROOT/usr/lib64 \
$SROOT/usr/bin/k1om-mpss-linux/k1om-mpss-linux-gcc \
  -B$SROOT/usr/bin/k1om-mpss-linux/ \
  -B$SROOT/usr/libexec/k1om-mpss-linux/gcc/k1om-mpss-linux/5.1.1/ \
  --sysroot=$SYSR ...
```

Verification was simply to have it compile a program and look at the ELF header:

```text
compile: [OK] 1896 bytes    Type: REL  Machine: Intel K1OM
link:    [OK] 10888 bytes   Type: EXEC Machine: Intel K1OM   interpreter ld-linux-k1om.so.2
```

## H.3 The First Program on the Card (the First Closed Loop of the Self-Built Toolchain)

The executable linked above was sent to the card and run. There was one small snag in the transfer: the card's `sshd` is OpenSSH 7.4 from 2019 and fails to negotiate with the host's newer `scp` (`scp: Connection closed`), so the file was sent with `ssh ... 'cat > /tmp/file'` instead. The result on the card:

```text
/tmp/hello_k1om: ELF 64-bit LSB executable, Intel Xeon Phi coprocessor (k1om),
                 version 1 (SYSV), dynamically linked, for GNU/Linux 2.6.32, not stripped
hello from k1om, built on LoongArch via box64
exit code=0      on-card uname: k1om / 2.6.38.8+mpss3.8.6
```

## H.4 A Self-Built k1om libgomp: The One Piece MPSS Never Shipped

OpenMP on the card and the card-side worker of offload both depend on k1om's `libgomp`, and MPSS's build configuration says `--disable-libgomp` — it is in **neither** the SDK nor the card image. It was built separately from the `libgomp` sources of GCC 5.1.1-knc:

- configure runs in **cross mode** (`--build=loongarch64…`, `--host=k1om-mpss-linux`), so it does not try to run test programs;
- `CC` points at the `k1om-cc` wrapper from above;
- `libbacktrace` has to be unpacked as well (a hard dependency of libgomp);
- the documentation target `stamp-build-info` (which only runs `makeinfo`) fails for want of files such as `gpl_v3.texi`; `make MAKEINFO=true` lets it no-op, and it has nothing to do with the library itself.

The artifacts (target architecture confirmed):

```text
libgomp.so.1.0.0   720,422 bytes   Machine: Intel K1OM   SONAME libgomp.so.1
libgomp.a        1,444,550 bytes   omp.h 4,355 bytes
NEEDED: libdl.so.2 libpthread.so.0 libc.so.6
GOMP_parallel family symbols: 27, OMP_* entry points: 161
```

It was then installed into **the compiler's own runtime directories** (one copy in the install directory reported by `-print-search-dirs`, one in the sysroot libdir reported by `-print-file-name=crtbegin.o`), so that `-fopenmp` and `#include <omp.h>` work out of the box with no `-I`/`-L` at all.

## H.5 The First Real Computation on the Card: an OpenMP Reduction

The test program is a 200-million-item parallel reduction (`#pragma omp parallel for reduction(+:sum)`), compiled on LoongArch with `-O2 -fopenmp` and sent to the card together with `libgomp.so.1.0.0`. Measured:

| `OMP_NUM_THREADS` | Actual threads | Elapsed | Speedup |
|---:|---:|---:|---:|
| 1 | 1 | 8.451 s | 1.0 |
| 61 | 61 | 0.233 s | **36.3×** |
| 244 | 244 | 0.282 s | 30.0× |

`omp_get_num_procs()` reports **244** (61 cores × 4-way SMT), matching the card's hardware. The correctness of the result can be checked too: the program prints `66666666.166666656733`, while the analytic value of the discrete sum `(n-1)n(2n-1)/(6n²)` (`n = 2×10⁸`) is `66666666.1666666667` — they agree. At 61 threads the speedup is close to linear; at 244 threads (all four SMT ways open) it is slightly slower instead, which is what KNC's in-order cores lead you to expect.

**What this step means**: the OpenMP capability on the card, which until now only Intel's own ICC could use, is now delivered by the combination "on a LoongArch host, using MPSS's k1om compiler, with a self-built libgomp", and all of it is reproducible.

## H.6 The Card-Side Offload Worker: `offload_target_main` Built and Loaded Successfully on the Card

This is where offload lands on the card side: a worker that `coi_daemon` starts up, and that receives and executes the offload image sent by the host. It was built with the same k1om compiler, and the key was **to use the target-side flags the tree itself gives** (the line in `liboffloadmic/plugin/Makefile.am`):

```text
-DLINUX -DCOI_LIBRARY_VERSION=2 -DMYO_SUPPORT -DOFFLOAD_DEBUG=1 -DSEP_SUPPORT -DTIMING_SUPPORT -DHOST_LIBRARY=0
```

A manual attempt that omitted `-DOFFLOAD_DEBUG=1` and `-DTIMING_SUPPORT` produced errors about symbols such as `OFFLOAD_DEBUG_TRACE` and `OffloadHostTimerData`; with the official flags, every target-side source file compiled. Composition and results:

| Part | Contents |
|---|---|
| Target-only sources | `runtime/coi/coi_server.cpp`, `compiler_if_target.cpp`, `offload_myo_target.cpp`, `offload_omp_target.cpp`, `offload_target.cpp`, `offload_timer_target.cpp` |
| Shared sources | `offload_common.cpp`, `offload_env.cpp`, `offload_table.cpp`, `offload_trace.cpp`, `offload_util.cpp`, `liboffload_error.c`, `liboffload_msg.c`, `ofldbegin.cpp`, `ofldend.cpp` and so on (21 objects in all, archived into `liboffloadmic_target.a`, 159,444 bytes) |
| Excluded | `*_host.cpp`, `cean_util.*`, `dv_util.*`, `offload_orsl.*`, `offload_engine.cpp` (host side, not needed for linking) |

The linked artifact:

```text
offload_target_main   90,073 bytes   Type: EXEC   Machine: Intel K1OM
NEEDED: libcoi_device.so.0 libmyo-service.so.0 libgomp.so.1 libpthread.so.0 libdl.so.2
        librt.so.1 libstdc++.so.6 libm.so.6 libgcc_s.so.1 libc.so.6 ld-linux-k1om.so.2
.OffloadEntryTable. section: 1
```

Once on the card every dependency resolved (`libcoi_device.so.0`, `libmyo-service.so.0`, `libscif.so.0` and `libstdc++.so.6` are already in the image; `libgomp.so.1` is the one built here), and running it directly produces its own runtime message:

```text
offload error: wait for process shutdown failed on device -1 (error code 1)
```

This error is the normal reaction when the worker is started on its own (no host offload runtime alongside it, incomplete arguments); **it proves that the binary loads successfully on the card, that every dynamic link resolves, and that it reaches its own main flow**. With that, **every card-side component of offload is in place**: `coi_daemon`, `libcoi_device`, the self-built `libgomp`, and `offload_target_main`.

## H.7 The Other Offload Route: Hand-Written COI, No x86_64 Required

This section answers a question the previous one can easily mislead you into asking: "so without x86_64, is offload impossible?" — **No.** The host-side trio discussed above (a host compiler with offload, the `liboffloadmic` host library, and `libgomp-plugin-intelmic`) belongs to the route where **the compiler generates the offload code for you** (route B in Appendix F), and that route is indeed stuck on x86_64 and the GCC version. But offload's **runtime model** itself does not need any of them:

COI is the interface offload actually lands on (Intel's own `micnativeloadex` goes through COI as well). The host uses COI to create a process on the card, put data into a shared buffer, and call a function the card side exports — that is offload. Everything required is already in hand:

| Component | Status |
|---|---|
| Card-side `coi_daemon` | In the image, starts at boot |
| Card-side `libcoi_device.so.0` | In the image |
| Card-side OpenMP runtime | **Built here** (H.4) |
| Card-side offload program | **Built here**: a program compiled with the k1om compiler and linked against `libcoi_device` (the compile path opened up in H.3) |
| Host-side COI | the **ported `libcoi_host`**, with engine enumeration and handles already verified on LoongArch (F.1) |

So the demonstration was built as two halves:

- **Card side, `offload_sink.cpp`**: written on the skeleton from the COI tutorial (`COIPipelineStartExecutingRunFunctions()` + `COIProcessWaitForShutdown()`), exporting a `COINATIVELIBEXPORT` function `CardReduce()` whose **body runs a 200-million-item reduction with `#pragma omp parallel for`**, writing the result, the card's hardware thread count, the actual thread count and the on-card elapsed time back into the return area. Measured artifacts: 13,117 bytes, `Machine: Intel K1OM`, `NEEDED` includes `libcoi_device.so.0` and `libgomp.so.1` (self-built), and the `CardReduce` symbol is exported.
- **Host side, `offload_host.cpp`**: compiled on LoongArch (`Machine: LoongArch`); the flow is enumerate engines → `COIProcessCreateFromFile("/tmp/offload_sink")` → `COIPipelineCreate` → `COIProcessGetFunctionHandles("CardReduce")` → `COIPipelineRunFunction` (pass the problem size in, get the result back) → compare against the analytic value → destroy the pipeline and the process.

One self-inflicted error was fixed along the way: `COIPipelineRunFunction` takes **12 arguments** (besides the buffers and access flags, there are the dependency event count and the dependency array), and omitting arguments produces type-mismatch compile errors; the prototype in the tree's headers is authoritative.

This route **runs entirely between LoongArch and the card, with no x86_64 step anywhere**, and the card-side function is still OpenMP-parallel inside. Its only difference from route A (native execution on the card) is "who initiates it and how the data gets across": route A is a person logging into the card and running the program, route C is a host program initiating it and fetching the result back.

## H.8 What Compiler-Generated Offload (Route B) Still Lacks: the Host-Side Runtime

The host side needs three things: a host compiler with offload, the `liboffloadmic` host library, and `libgomp-plugin-intelmic.so.1`. They have to be built in an **x86_64** environment, for the same reasons as in Appendix F.3.1 (`liboffloadmic/configure.tgt` rejects non-x86 hosts; GCC 5.1.1 cannot have loongarch64 as its host; offload requires host and target compilers from the same source at the same version so that they can exchange LTO intermediate code). There are two workable approaches:

1. **Run an x86_64 user-space environment on the LoongArch machine under box64**, and build GCC 5.1.1's "host + k1om offload" compiler pair and the host runtime inside it. Everything is emulated, therefore slow, but no extra machine is needed.
2. **Use an x86_64 Linux machine** (or a virtual machine) for the same build, then connect the artifacts to the card.

Worth noting: **the target-side compiler does not have to be rebuilt.** The SDK's k1om GCC is 5.1.1 and ships `lto1` and `liblto_plugin.so`, the same version as the host compiler still to be built, which satisfies the LTO version-match requirement; together with this appendix's target-side `libgomp` and worker, there is no gap left on the card side.

Until the host side is finished, the paths already usable between LoongArch and the card are: SCIF on the management plane (`micctrl`/`mpssd`), host-side COI's engines and handles (Appendix F.1), and the "compile with k1om → send to the card → native execution/OpenMP" chain opened up here.

## H.9 Offload Run Log: Two Root Causes, and Where It Is Stuck

Once the H.7 route (hand-written COI) was standing end to end, the actual offload call still failed. This section records the complete troubleshooting chain from "failure" to "root cause", and what is still left.

### H.9.1 Starting Point: a Vague Error Code

The host program enumerated engines and fetched handles successfully, but `COIProcessCreateFromFile` returned only `COI_ERROR(1)` (unspecified) ✓. Peeling it back layer by layer:

| Approach | What it told us |
|---|---|
| Parameter experiments (changing the proxy, the library search path) | With a library path the error becomes `COI_BINARY_AND_HARDWARE_MISMATCH(22)`; without one it is `COI_ERROR(1)` — 22 means "a dependent library has the wrong machine type" |
| Reading COI's source, `shared_library_finder.cpp:235` | The host reads out **every dependency** of the sink and validates its ELF Machine; all must be `EM_K1OM`. On that basis a host-side k1om dependency directory was built (all 7 dependencies `Intel K1OM`) |
| Re-running the experiment | 22 disappeared and `COI_ERROR(1)` was back — so the failure point lies after the dependency check |
| `strace` | The failure is in SCIF's `SCIF_REG` (number 8): `ioctl(3, _IOC(READ\|WRITE, 0x73, 0x8, 0x8), …) = -1 EINVAL` |
| Driver `pr_debug` (`module mic +p`) | `SCIFAPI register: ep … Connected len 0x1000 offset 0x0 prot 0x3 map_flags 0x0` immediately followed by `scif_register err -22` — **every argument is legal, yet it is rejected** |

### H.9.2 Root Cause One: a 4 KiB Registration Granularity Hits a 16 KiB Kernel Page

That silent `return -EINVAL` in the driver comes from the page-alignment check in `__scif_register`:

```c
if ((!len) || (align_low((uint64_t)addr, PAGE_SIZE) != (uint64_t)addr) ||
              (align_low((uint64_t)len,  PAGE_SIZE) != (uint64_t)len))
        return -EINVAL;
```

The local kernel is `7.1.13-aosc-main-16k` with `PAGE_SIZE = 16384`, while MPSS's SCIF user space registers in **4096**-byte units (`len 0x1000`) — `4096` is not an integer multiple of 16384, so it is silently rejected. This was verified with a runtime interceptor that rounds `len` up to the actual page size: the control group still got `errno=22`, while the experimental group passed the check with `len 0x4000`. **Root cause confirmed.**

### H.9.3 Root Cause Two: a Spinlock Inside a packed Struct

Once past the alignment check, the kernel reported an exception immediately and the process turned into a zombie. Cross-locating the `strace` scene against the kernel report:

```text
Unhandled kernel unaligned access[#1]:
  ERA: _raw_spin_lock_irqsave+0x44/0xf0      ← the kernel spinlock itself is what faulted
  BADV: 90000001c7fd8096                      ← an address that is not 4-byte aligned
Call Trace:
  _raw_spin_lock_irqsave → prepare_to_wait_event → micscif_prep_remote_window [mic]
```

That is, in the queue `micscif_prep_remote_window` waits on, a `spinlock_t` sits at an unaligned address. The reason is in `include/mic/micscif_rma.h`: `struct reg_range_t` (which contains a `wait_queue_head_t`) is marked `__attribute__ ((packed))`. x86 tolerates unaligned locks (it is merely slower); the LoongArch kernel `die`s outright. A whole-module scan confirmed that the struct which is "packed and contains a lock/wait queue" is **this one and no other**; removing the attribute drove the `unaligned` count to zero and shrank the function body from `0x8f0` to `0x520`. **This one is a genuine porting defect**, and it has gone into the released source tree as a patch.

### H.9.4 Where It Is Stuck Now: Page-Size Assumptions Run Through the Whole RMA Layer

With packed fixed, registration got deeper, but another class of exception appeared right away:

```text
ESTAT: 00480000 [ADEM]   BADV: e0000e4800000000
ERA: micscif_prep_remote_window+0x198   (source line micscif_rma.c:1161)
Code: … <380c3b7e> …    ← ldx.d  $s7, $s4, $t2
```

`0xe000…` is a LoongArch non-cached/physical window address, which says that the result of a `scif_ioremap` is bad. Reading on, the problem is **page-count semantics**: on the host side `window->num_pages[j] << PAGE_SHIFT` and `nr_pages = len >> PAGE_SHIFT` are all in **host pages**, while the card side has 4 KiB pages. The host computes `nr_pages = 1` (16 KiB pages) and the card reads that as 4 KiB — the card maps only 4 KiB while the host has pinned 16 KiB, so the window descriptor it gets back does not match reality and the host ends up with a bad address.

This is two faces of the same root as H.9.2: **MPSS's SCIF takes the "host page" as its protocol unit.** On x86 both host and card are 4 KiB, so it never shows; on a 16 KiB-page LoongArch machine it shows everywhere. Fixing it means **converting between the protocol unit (4 KiB) and the host page (16 KiB)**, and `PAGE_SHIFT` appears in more than a dozen places in that layer (`micscif_map.h:44/201-211`, `micscif_rma.h:666/675/717/733/756-761`, `micscif_rma.c:557` and others), while the DMA mapping layer (`mic_map`/`dma_map_page`) has page assumptions of its own.

### H.9.5 Two Ways Out

| Route | What it involves | Cost and risk |
|---|---|---|
| **Switch to a 4 KiB-page kernel** | MPSS's page assumptions hold by themselves, and SCIF/COI/offload work as designed | Requires building a 4 KiB-page kernel for LoongArch (LoongArch supports 4/16/64 KiB pages) and switching away from the current 16 KiB system; the risk is concentrated in building and booting the kernel, and the rest of the chain is untouched |
| **Deep rework of the host SCIF RMA layer** | Fix the protocol unit at 4 KiB, pin by host page, expand the physical address table in 4 KiB units | Touches a dozen places, each of which can produce a new ALE/ADEM; the DMA layer has to be checked as well; an hours-scale iterative job |

Whichever way it goes, the results already achieved this round are unaffected: the complete port of the MPSS tool side to LoongArch, a usable k1om cross compiler on LoongArch, the self-built k1om `libgomp`, native OpenMP on the card (61 threads, 36× speedup), the card-side `offload_target_main`, and the two root causes recorded in this appendix.

### H.9.6 Correction: Separate "Genuine Defect" from "Side Effect of the Diagnostic"

Above, H.9.4 attributed `ADEM` to "MPSS's protocol unit tangled into the RMA layer". After checking the shape of the address, that conclusion **overreaches** and should be narrowed:

| Symptom | Faulting address | Nature |
|---|---|---|
| `ALE` caused by packed | `page-aligned base + 150` (150 ≡ 2 mod 4, and it is a compile-time member offset — the packed disassembly hard-codes `+150`) | **Genuine defect**: independent of the runtime page size |
| `ADEM` caused by a page-count mismatch | `0xe0000e4800000000` (a LoongArch physical/non-cached window) | **Very likely an artifact of the diagnostic** |

The reasoning: in H.9.2, an `LD_PRELOAD` interceptor raises the registration length from 4096 to 16384 to get past the driver's page-alignment check. That amounts to **lying to the driver about the length** — the host computes `nr_pages = 1` from 16 KiB pages, and that one page is not equivalent, in protocol terms, to the card's 4 KiB page, so the window descriptor does not match reality and `scif_ioremap` returns a bad address. With the proper fix — "make the driver accept 4 KiB and get the conversion right" — the `ADEM` need not appear.

So **only one original defect has been proven**: on a 16 KiB-page kernel the driver validates page alignment by `PAGE_SIZE` and thereby silently rejects the 4096-byte registrations that MPSS user space always uses. The pervasive use of `PAGE_SHIFT` in the RMA layer is a risk that is **still to be assessed**, not a proven second defect.

Two further items also have to be labeled honestly:

- **Disabling PC3/PC6 is an unverified hypothesis**: it was done to explain `micscif_nodeqp_send … error -19`, but `-19` still appeared after it was disabled, which shows that the power state is not the cause of that error. Keeping it should count only as an explicit configuration choice, not as a fix.
- **The `COIEngineGetInfo` struct-size mismatch is still unexplained**: passing 5200 bytes yields `COI_ERROR(1)` and other sizes yield `COI_SIZE_MISMATCH(12)`, which says the ported header and the library disagree about this struct. It has nothing to do with page size and is a separate leftover issue.

## H.10 Page-Size Alignment: Dependency Survey and Fix Options

H.9 has pinned the symptom down to "16 KiB host pages vs. the 4 KiB MPSS assumes". This section completes the dependency survey and lists the available fixes.

### H.10.1 The Key Structure: Both Ends Share One Source, and the Protocol's "Page" Is the Sender's Own PAGE_SIZE

The MPSS kernel module is **one tree building both ends**: `Kbuild` decides between host and card from `MIC_CARD_ARCH` (empty means card). In other words the host-side and card-side SCIF **are the same source**, and "page" in that source is always written `PAGE_SIZE`/`PAGE_SHIFT`. On x86 both ends are 4 KiB, so the protocol lines up by itself; when one end is 16 KiB, the same field means different things at the two ends — that is the structural reason for the `ADEM` in H.9.4.

### H.10.2 Page-Size Dependencies by Layer

| Layer | Unit | Evidence |
|---|---|---|
| COI user space | **4096 hard-coded** | `_MemoryRegion.h:71 #define PAGE_SIZE (4096)`, `_Message.h:76 #define COI_PAGE_SIZE 4096` |
| libscif | pass-through, no page constants | `scif_api.c` only fills the caller's `len` into the ioctl struct |
| Host driver (same source as the card) | `PAGE_SIZE` (16 KiB here) | alignment checks `micscif_api.c:1940/2251/2526/2662`; counts `1946/2255/2558/2566/2674`; offset conversions `micscif_rma.h:44/733/757`; DMA mapping `micscif_map.h:201-211` |
| Host DMA/SMPT layer | **fixed constants**, unrelated to this problem | `micscif_smpt.h:79 #define MIC_SYSTEM_PAGE_SHIFT 34`, `micbaseaddressdefine.h:103 MIC_SYSTEM_PAGE_SIZE 0x0400000000` |
| Card side | same source, but its kernel has `PAGE_SHIFT = 12` | the `MIC_CARD_ARCH` branch in `Kbuild` |

The fact that the DMA/SMPT layer uses fixed constants matters: **only `micscif`'s bookkeeping has to change**; the DMA descriptor layer need not be touched.

### H.10.3 Four Fixes and Their Costs

**Option 1: change the host driver to "4 KiB protocol units + host-page pinning"**

- Switch the quantities that **cross to the card side** to fixed 4 KiB units: the window's `nr_pages`/offset/len, `num_pages[]`, and the offset→page conversions in `micscif_get_dma_addr`/`get_phys_addr`;
- Host-side memory management stays in host pages (pin after `ALIGN(len, PAGE_SIZE)`);
- About 10 to 15 mechanical changes; **zero changes on the card side and in user space**;
- Medium risk: many points, but each has a clear unit to compare against and can be verified one at a time.

**Option 2: register by host page in user space + change only the protocol boundary in the driver**

- In user space, replace `PAGE_SIZE` in `_MemoryRegion.h` with `sysconf(_SC_PAGESIZE)` (**do not touch** `COI_PAGE_SIZE` in `_Message.h` — that one is message framing);
- The driver must still convert protocol quantities to 4 KiB, otherwise it ends in the "lying about the length" outcome of H.9.3;
- Smaller scope (1 place in user space + 4 to 6 in the driver), but it touches COI's own buffer-offset assumptions and needs extra verification.

**Option 3: switch to a 4 KiB-page host kernel**

- The LoongArch kernel supports 4/16/64 KiB pages; build a kernel with `CONFIG_PAGE_SIZE_4KB` and **not one line of MPSS code changes**;
- It makes **all** 4 KiB assumptions hold at once — not only the registration path but also the as-yet-untouched `scif_mmap` (`vm_pgoff << PAGE_SHIFT`, `micscif_api.c:2887/2941`) and the chunking in `readfrom/writeto` (`MAX_PAGE_ORDER + PAGE_SHIFT`, same file `1646/1711`);
- The cost is building and switching kernels (about an hour), and the module has to be rebuilt once against the new kernel; the risk is concentrated in building and booting the kernel.

**Option 4: change the card side to 16 KiB protocol units** — requires rebuilding the card kernel and image and changing the wire protocol, while the card's own pages are still 4 KiB; the cost is out of proportion to the benefit, so it is not taken.

**Option 5: bypass SCIF memory registration** — COI's very first registration (the 4096-byte signal page) already fails, so there is no way around it; not taken.

### H.10.4 Recommended Order: 3 First, Then 1

There are no unknowns on Option 3's path, and it has independent value: it can **test the very judgment that "page size is the only obstacle"**. If offload runs straight through on a 4 KiB-page kernel, the diagnosis closes; if 16 KiB systems must then be supported, Option 1 can be implemented afterwards, and by then there is a working baseline to compare against, which makes debugging far cheaper. Doing Option 1 first means that every iteration has to choose between "a newly introduced bug" and "not finished converting yet", which is expensive and unlikely to converge.

Whichever route is taken, **the packed fix from H.9.3 is required** (it has nothing to do with page size), and that fix is already in the released source tree.

A fuller investigation of page-size alignment (how to tell the symptoms apart, the layer-by-layer dependency survey, the list of conversion points and the trade-offs among the five fixes) is in [Appendix I](I-page-size-alignment.md).

**Later measurement (see H.13)**: after Option 1 was implemented on both the registration and the copy paths, **offload runs end to end on a 16 KiB-page host**, so there is no longer any need to switch to a 4 KiB-page kernel — the "3 first, then 1" above was the recommendation at the time and has been superseded by the actual result.

## H.11 Reproduction Checklist

| Step | Script (under `~/XeonPhiX100-LoongArch/`) | Log (under `offload-logs/`) |
|---|---|---|
| Unpack the SDK, get the k1om compiler working | `off_11_sdk2.sh`, `fix_sdk_symlinks.py` | `sdk-*.log` |
| Build the first program and run it on the card | `off_13_build.sh`, `off_15_run.sh`, `off_16_cardrun.sh` | `k1om-run-*.log`, `card-run-*.log` |
| Build and install k1om libgomp | `off_21_libgomp.sh`, `off_22_libgomp.sh`, `off_24_libgomp_fin.sh`, `off_32_ompfinal.sh` | `libgomp*.log`, `omp-final-*.log` |
| Measure OpenMP on the card | `off_32_ompfinal.sh` | `omp-final-*.log` |
| Hand-written COI offload demo | `off_60_demo.sh`, `off_61_run.sh` | `offload-demo-*.log`, `offload-run-*.log` |
| Build the card-side offload worker | `off_43_target2.sh`, `off_44_target3.sh`, `off_45_workercard.sh` | `offld-target*.log`, `worker-card-*.log` |
| End-to-end acceptance (T0–T8) | release tree `tests/run_all.sh`, `tests/run_tests.sh`, `tests/t7_nbody.sh` | `tests/logs/run-<timestamp>/` (see [Appendix J](J-acceptance-tests.md)) |

Toolchain wrapper script: `k1om-cc` (it doubles as `k1om-cxx`); staging prefix for the self-built runtime: `k1om-sysroot-extra/`.

## H.12 Correspondence with Appendix I: the Driver Layer Is Closed

This round's offload troubleshooting finally converged on two independent defects; the page-size-related part is in I.10 and I.11 of [Appendix I](I-page-size-alignment.md). Only the conclusions and the current state are recorded here:

| Item | Status |
|---|---|
| Registration rejected because of 16 KiB host pages | **Fixed** (length validated by protocol page + conversion on the sending side); measured allocation requests now carry `nr_pages=4` |
| Unaligned atomic access caused by a wait queue embedded in a `packed` struct | **Fixed** (the four wait queues turned into pointers); measured `unaligned` count is zero |
| Struct layouts on the two ends disagreeing | **Fixed** (`packed` restored); measured descriptor-area `magic` equals `SCIFEP_MAGIC` |
| Host-to-card bulk write | **Verified byte-exact** (card side checks 4096 bytes, 0 mismatches) |
| `COIProcessCreateFromFile` blocking | **Resolved** (H.13); the earlier verdict "this belongs to the COI application layer and has nothing to do with page size or struct layout" has to be corrected to: it was caused precisely by the protocol page count and by the sink's missing `-rdynamic` |

The two small programs used for verification (card-side `rma_srv`, host-side `rma_cli`) and the associated scripts are left in the server's working directory; the scripts are named with the `rma_` prefix.

## H.13 Addendum: COI Process Creation Works (Three Fixes and the Measured Numbers)

The unresolved item in H.12 is now resolved. It turned out not to be a "COI application layer" problem but three concrete defects:

| # | Defect | Fix | Criterion |
|---|---|---|---|
| 1 | The 26496-byte creation command was computed with the wrong page stride on the RMA copy path, so only the first page was correct | Convert on the copy path by protocol page/host page ([Appendix I](I-page-size-alignment.md) I.10) | A seven-step length sweep in `t4` plus a full 26496-byte check: 0 bytes mismatched |
| 2 | The peer window length was computed in host pages | Convert by $P_h / P_c$ before sending | The kernel log shows `nr_pages=4` (1 host page of 16 KiB = 4 peer pages of 4 KiB) |
| 3 | The card-side sink was not linked with `-rdynamic`, so the exported symbol was not in `.dynsym` | Add `-rdynamic` at link time | `readelf --dyn-syms` shows `CardReduce`; without it the host gets `COI_DOES_NOT_EXIST(5)` |

With all three fixed, the end-to-end path runs through item by item (`t5`, 17 of 17 passing):

| Step | Measured |
|---|---|
| Enumerate engines / fetch handles | `1) engines = 1`, `2) got handle for engine 0` |
| Create a process on the card | `COI_SUCCESS(0)` (previously `COI_ERROR(1)` or `COI_BINARY_AND_HARDWARE_MISMATCH(22)`) |
| Create a pipeline, fetch a function by name | `COIPipelineCreate` succeeds, `CardReduce` found |
| Computation on the card | 2×10⁸-item OpenMP reduction, **240 threads**, relative error −3.35e-16 |
| Teardown | Pipeline and process destroyed normally; card-side `coi_daemon` still there |
| Kernel side | 0 unaligned exceptions, 0 kernel exceptions |

The same machinery was then used for a "heavy computation + large data" example (N-body gravity, $O(N^2)$, three problem sizes): the card side generates the initial conditions itself from a deterministic formula, the results come back through the return area, and the host compares them against the same checksum computed by its own reference implementation.

| Case | N | Steps | Threads | Checksum comparison | Relative energy error | On-card time | Throughput |
|---|---:|---:|---:|---|---:|---:|---:|
| 1 | 1024 | 20 | 240 | **diff 0.000e+00** | 1.96e-04 | 0.40 s | 0.62 GFLOPS |
| 2 | 8192 | 10 | 240 | — (large problem, only energy conservation checked) | 8.47e-05 | 3.1 s | 2.10 GFLOPS |
| 3 | 16384 | 5 | 240 | — | 4.02e-05 | 5.1 s | 2.68 GFLOPS |

Two limitations remain, both derived from measurement and both written into the guide shipped with the release:

| Limitation | Symptom | Workaround in use |
|---|---|---|
| `COIBufferCreate` unusable | Returns `COI_OUT_OF_MEMORY(13)` whether with host memory, page-aligned memory or `NULL` (library-allocated) | Inputs go through `miscData` (≤64 KiB) and results through the return area (measured reliable at 64 bytes) |
| `COIEngineGetInfo` struct size mismatch | Passing 5200 bytes yields `COI_ERROR(1)`; other sizes yield `COI_SIZE_MISMATCH(12)` (already recorded in F.1) | Do not use this interface — enumeration, process creation and handle fetching do not need it |

The full list of "how to write a card-side sink and which styles crash on the card" is in the *KNC offload Programming Manual* shipped with the release (`OFFLOAD_GUIDE.md`, organized item by item around the COI API); the acceptance script that runs it through is [Appendix J](J-acceptance-tests.md).

## H.14 Correction: Masked Compressed Store Is Not a Criterion (One Self-Check Rule Disproved)

This section disproves a rule **this report itself put forward earlier**. It used to run: a card-side program crashes because the toolchain emits **masked compressed store instructions** (`vpackstorelpd`), so the self-check is "`objdump -d | grep -c vpackstore` must be 0". The rule made it into the guide and into earlier conclusions of this appendix. On review on 2026-10-06 it does not hold.

The occasion was that the N-body example ran under `-O0` but always crashed under `-O1`/`-O2` (card-side log `segfault at 0 ip … in nbody_sink`). All three optimization levels were built and counted one by one with the k1om objdump that **actually exists** (`sysroots/x86_64-mpsssdk-linux/usr/bin/k1om-mpss-linux/k1om-mpss-linux-objdump`):

| Binary | `vpackstore`/`vscatter` | Result |
|---|---:|---|
| N-body sink (`-O0`) | **12** (all writing to stack, e.g. `-0x90(%rbp)`) | **Runs through**, checksum bit-for-bit identical to the host reference |
| N-body sink (`-O1`) | 11 (one of them writing `0x10(%r12)`) | Crashes, and the crash ip is exactly that one |
| N-body sink (`-O2`) | 11 (same as above) | Crashes, and the crash ip is exactly that one |
| Reduction sink (`-O2`, the H.7 example) | 3 | Runs through |

Three criteria hold at once, and the rule is therefore disproved:

1. **The instruction is itself a legal KNC instruction** — KNC has its own 512-bit vector registers and 16-bit mask registers, and `vpackstorelpd` is part of its ISA;
2. **The count has nothing to do with success or failure** — the build that runs has 12, the ones that crash have 11;
3. **The earlier "0 under `-O0`" was a false pass** — it came from an objdump path that does not exist (`sysroots/k1om-mpss-linux/usr/bin/…`), and when the command fails `grep -c` still returns 0.

The rule that remains is a single one, and it is measured and **per source file**: **the optimization level has to be settled by running it yourself.** The two samples in this suite are exact opposites — the N-body sink always crashes under `-O1`/`-O2` and only works at `-O0` (table above), while the reduction sink built with `-O2` passes all 17 items. The cause is still undecided (the symptom is that GCC 5.1.1's k1om backend uses that instruction with register addressing under some code shape and crashes on the spot).

One methodological point worth leaving for whoever comes next: **a self-check rule needs a control sample of its own.** At the time there was a "known good" sample right there (the H.7 reduction sink, built with `-O2`, 17 of 17 passing), and simply measuring it too would have shown 3 immediately, instead of mistaking correlation for causation and letting a wrong rule survive all the way into the guide.
