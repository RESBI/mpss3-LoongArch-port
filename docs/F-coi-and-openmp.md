# Appendix F — Using the Card on LoongArch: Native OpenMP and Hand-Written COI

The preceding twelve chapters answered "can the driver and the tools be ported". This section answers the question that follows: **now that the card runs on a LoongArch machine, how do you compute with it, especially with OpenMP.** The content is based on measurements on this machine, and anything not measured is marked as such. Some of this appendix's conclusions are "this road does not work", and those come with criteria too — they save a newcomer more time than the "how to" parts do.

---

## F.1 — Measured: Host-Side COI Already Works on LoongArch

With the ported `libcoi_host.so` installed, a separate probe (`coi_offload_probe.c`) was written, compiled on LoongArch and run as root, calling the COI engine interfaces one by one. Measured output (transcribed verbatim):

```text
=== 1. Enumerate engines ===
  COIEngineGetCount(COI_ISA_MIC) -> COI_SUCCESS(0), engines = 1
  engine 0: COIEngineGetHandle -> COI_SUCCESS(0), handle=valid
      (COIEngineGetInfo failed - struct size mismatch?)
```

These two lines are the foundation of this appendix: **the COI host library ported to LoongArch enumerates this card as a usable engine on real hardware and successfully obtains a handle.** That is to say, the entry point of the chain "a host program directs the card with COI" is open.

The only thing that did not succeed was `COIEngineGetInfo` — it validates its argument by size, and the probe first passed the size of `COI_ENGINE_INFO` from the local header, which was rejected. The second version of the probe tried 4096/2048/…/64 bytes in turn and reported the size that was accepted, to determine whether this was a struct version difference between header and library or purely a length check. The result is in [Appendix H](H-offload-field-notes.md) H.9.6: passing 5200 bytes yields `COI_ERROR(1)` and the other sizes yield `COI_SIZE_MISMATCH(12)`, which shows that the ported header and the library disagree about this struct; it is **still unexplained**. **This item does not affect engine enumeration, handle acquisition or process creation** — the later end-to-end chain (H.13) does not use it at all.

Next, creating a process on the card was attempted:

```text
=== 2. create a process on the card (offload smoke test) ===
  launching: /bin/sh -c uname -a; id; echo COI-OFFLOAD-OK > /tmp/coi_offload_probe.out
  COIProcessCreateFromFile -> (error)(22), process=null
```

The number `22` has to be read from COI's own enumeration table. In the `COIRESULT` numbering starting at `src/include/common/COIResult_common.h:52`, item 22 is:

```text
COI_BINARY_AND_HARDWARE_MISMATCH   ///< A specified binary will not run on the specified hardware.
```

That is, **"this binary cannot run as a COI process on this hardware."** The sentence points very precisely at what is still missing; see F.4.

## F.2 — Card-Side Runtime: What Is and Is Not in the Image (Measured)

Listing the card-side image from the release and checking it item by item, everything related to computation and offload is in the table below. The criteria are the file paths inside the image and the actual ELF headers (`readelf -h` reports `Machine: Intel K1OM`, that is `EM_K1OM` 181).

| Item | Present | Notes |
|---|:--:|---|
| `coi_daemon` | yes | Card-side COI daemon, 235,760 bytes, k1om ELF, depends on `libscif.so.0` |
| `libcoi_device.so.0` | yes | Card-side COI device library, 485,576 bytes, k1om ELF, depends on `libscif.so.0` |
| `libmyo-service.so.0` | yes | MYO's card-side service, paired with the host-side ported `libmyo-client.so` |
| `libscif.so.0` | yes | Card-side SCIF library |
| glibc | yes | `libc-2.21.so`, dynamic interpreter `ld-linux-k1om.so.2` — the target ABI for card-side binaries |
| `libgomp` | **no** | There is no OpenMP runtime anywhere in the image, nor a single one in MPSS's RPM manifest; you have to build it yourself |
| any COI application | **no** | A full image scan finds only `coi_daemon` and libraries — no examples, no sink program |
| compiler | **no** | You cannot compile on the card; binaries must be sent up |

One further measurement corrected an earlier conclusion: **`coi_daemon` now starts automatically at boot.** The MicDir-generated image contains `/etc/init.d/coi` and the `S95coi` links under `rc?.d`, and once the card reaches `online`, `pidof coi_daemon` returns a value (measured 5591). The image we originally modified by hand did not have this startup script — that is the real reason this report's early COI probe reported "0 engines", and it has nothing to do with the quality of the port.

## F.3 — Routes That Work and Routes That Do Not

### F.3.1 — First, the One That Does Not Work: `#pragma omp target` from GCC Offloading to KNC

The conclusion is hard: **upstream GCC has never had KNC code generation; its MIC offload target is KNL.** There are two independent grounds:

1. The Intel engineer responsible for that GCC feature wrote explicitly on the GCC mailing list in 2014 that the current KNC hardware "will not be supported by GCC", and that the mainline at the time did not support KNC code generation.
2. The version history matches: MIC offload was introduced in GCC 5, marked deprecated in GCC 12, and **removed entirely in GCC 13** (deleting `liboffloadmic/`, 114 files in all, along with `intelmic-mkoffload` and the rest). What it produced all along was x86-64 plus AVX-512 KNL target code, not `EM_K1OM` card-side code.

On LoongArch this route has two further walls stacked on it: `liboffloadmic/configure.tgt` simply declares non-x86 hosts unsupported; and offload requires the host compiler and the target compiler to be **from the same source tree at the same version** (they exchange LTO intermediate code), while the only tree with k1om code generation is GCC 5.1.1, and **LoongArch did not enter GCC mainline until GCC 12** — one tree cannot satisfy both conditions.

One piece of circumstantial evidence can be added: MPSS's own `mpss-offload` and `mpss-offload-dev` packages are **empty shells** (two to three thousand bytes each, zero files), and the offload runtime Intel offered for ICC actually shipped with Composer XE rather than MPSS. Nor can any public report be **found** of "running OpenMP on KNC with GCC" — native or offloaded — which corroborates the two grounds above.

The one positive finding worth recording: on the host side, `liboffloadmic` calls COI via `dlopen("libcoi_host.so.0")` plus `dlvsym(..., "COI_1.0")` — **exactly the COI 1.0 symbol versioning we preserved when porting**. Which is to say that if someone ever does move the offload runtime to LoongArch, our library lines up precisely at the ABI layer; what is stuck is not that layer but the compiler.

### F.3.2 — The Two Viable Routes

| Route | Mechanism | Already in place | Still missing |
|---|---|---|---|
| **ANative OpenMP** (recommended starting point) | Use a k1om compiler to build an `-fopenmp` program into a card-side binary and send it up together with a self-built k1om `libgomp`, running it directly on the card | The card-side runtime is complete (glibc 2.21, libscif, libcoi_device, libmyo-service); the card runs sshd, so `scp` is enough and there is no need to depend on `micnativeloadex` | ① a compiler that can produce k1om code (see F.3.3) ② a **k1om `libgomp`**, which must be built yourself — MPSS's compiler configuration literally says `--disable-libgomp`, and it is in neither the image nor the RPM manifest ③ k1om binutils (an `as` that takes `--march=k1om` and an `ld` that takes `-m elf_k1om`) |
| **CHand-written COI/MYO** | The host program calls the COI or MYO API directly to direct the card, moving data through buffers or SCIF | **Host-side COI has been ported and verified on real hardware** (F.1); the MYO host library has also been ported and works | Whatever runs on the card side must be a "COI program" (F.4), which again comes back to the route A toolchain prerequisite |

### F.3.3 — The Most Practical Step in Route A: Do Not Make the Toolchain Accommodate LoongArch

There is an easy expectation to get wrong here: **do not make "build a k1om cross compiler on LoongArch" your first step.** That k1om tree is GCC 5.1.1, which can neither run on loongarch64 (LoongArch support only began with GCC 12) nor be combined with GCC 12's offload machinery. The genuinely low-cost approach is **to get the existing x86_64 k1om toolchain running on LoongArch**:

- Use **qemu-user** (or an x86-64 user-space emulator such as box64/FEX) to run MPSS's own `k1om-mpss-linux-gcc` and its `as`/`ld` directly. They are ordinary x86-64 Linux user-space programs with no privilege requirements and no need for kernel support;
- Rebuild `libgomp` and the target libraries from the same tree — drop `--disable-libgomp`, keep `--target=k1om-mpss-linux` and MPSS's k1om sysroot, and produce a card-side `libgomp.so` and `omp.h`;
- Binaries compiled afterwards are executed directly by the card, and the LoongArch side is only "running the compiler under an emulator", with zero runtime overhead.

If the goal is "no dependency on an emulator", another path is to forward-port Intel's k1om backend to GCC 12 or later and then build it with LoongArch as the host. That is a compiler-porting-scale project, comparable in effort to the card-side kernel port assessed in G.6, and is not recommended as a starting point.

## F.4 — Why a Card-Side Program Must Be a "COI Program"

What error code 22 means by "binary and hardware mismatch" is not that the ELF machine type is wrong — the card's `/bin/sh` is itself a k1om ELF. COI is checking something else: **whether the target program carries COI's process bootstrap and device runtime**, that is, whether it links the card-side `libcoi_device` (COI's headers call this set of card-side interfaces sink and source). When COI creates a process it has to complete device-side initialization so that the process can establish buffer and pipeline channels with the host, and an ordinary program has none of that.

This agrees with the image scan: the image has only `coi_daemon` and libraries, and **no application that links `libcoi_device`**. So "running something on the card with COI" is blocked on the absence of a **card-side sink program** built with a k1om compiler plus `-lcoi_device`, not on protocol, permissions, or a porting defect.

There is also a convenient deployment path: `scp` the compiled k1om binary and the self-built `libgomp.so` up to the card and run them directly over the card's ssh, with no host-side COI needed at any point, and no `micnativeloadex` either.

In passing: MPSS's `micnativeloadex` is a host-side tool whose source is in the `mpss-coi` tree we already ported (`src/tools/micnativeloadex/`), and internally it also creates processes through COI. If it is to be used to deliver "native" programs, the F.4 hurdle has to be cleared first — or just bypass it with `scp` plus `ssh`, which for native OpenMP carries all the information needed.

This appendix is about routes and criteria; **the results of actually doing the work (the k1om toolchain, the self-built libgomp, the first measured OpenMP run on the card, and the hand-written COI offload working end to end) are recorded in [Appendix H](H-offload-field-notes.md)**. Of that, the parts already completed are: the k1om cross compiler is usable on LoongArch, native OpenMP runs on the card (61 threads, 36× speedup), and the end-to-end chain from a card-side worker to the host-side COI runtime works (H.13: host creates a process → fetches a function by name → OpenMP computation on the card → result retrieved, with the checksum bit-for-bit identical to the host reference); what remains unfinished is **compiler-generated offload** (route B), which is stuck on the compiler and not on the runtime (F.3.1). For turning the above capabilities into repeatable acceptance steps, see [Appendix J](J-acceptance-tests.md).

## F.5 — The Next Steps Listed at the Time (All Now Completed)

This section keeps the original list and annotates where each item ended up — it doubles as a roadmap "from reading to doing":

| # | Planned step | Outcome |
|---|---|---|
| 1 | Run the engine-chain probe again and record the size that `COIEngineGetInfo` accepts | Done: the interface's struct size still does not match (see F.1 and H.9.6), **still unexplained**, but it does not affect any subsequent step, so it was not pursued further |
| 2 | Make the k1om toolchain usable on LoongArch | **Completed** (H.2: using box64 to run MPSS's own x86_64 k1om compiler; the artifact's `readelf -h` reports `Intel K1OM`) |
| 3 | Build a k1om sysroot | **Completed** (H.1: the k1om RPMs of the MPSS SDK, yielding 1366 headers and 237 libraries) |
| 4 | Build a k1om `libgomp` | **Completed** (H.4: `libgomp.so.1.0.0`, 720,422 bytes, `Machine: Intel K1OM`) |
| 5 | First real computation (an `-fopenmp` program sent to the card to run) | **Completed** (H.5: a 200-million-item reduction, 36.3× speedup on 61 threads; later using all 240 hardware threads in a host-initiated offload) |
| 6 | Then consider hand-written offload | **Completed** (H.7 erected the skeleton, H.13 made it work end to end: three defects, the measured N-body run on the card, and the two limitations that remain are all in H.13; for repeatable criteria see [Appendix J](J-acceptance-tests.md)) |

In other words: the two viable routes listed in this appendix (A, native OpenMP on the card, and C, hand-written COI) **have both been completed and left reproducible records**; the only one that did not work is route B (offload compiler-generated offload), and it is stuck on the compiler side (F.3.1), not on the port.

## F.6 — Relationship to the Rest of the Report

The first twelve chapters of this report and Appendix E demonstrate that "the host-side tools work completely on LoongArch", including creating users on the card over SCIF, injecting keys, and reading and writing card state. What this appendix adds is **another way to use the card: as a compute device**. The interface between the two is COI — it is both the near neighbor of the management plane and the landing interface of the offload runtime. The host side has been verified down to the engine-and-handle layer; what remains missing is the **compiler and runtime**, no longer the port.

For the same reason, half of Appendix G's conclusion appears here in advance: **the card itself (kernel + user space) needs no updating and can still be fully utilized by a LoongArch host**; what genuinely has to be built is the toolchain that produces k1om binaries, and that affects only "what runs on the card", not "how the host directs the card".

## F.9 Checklist before writing offload / COI code (details in Appendices K and L)

This section lists only the **rules that must be followed**; the rationale and measurements live in [Appendix K](K-offload-memory-rules.md) (memory movement) and [Appendix L](L-api-determinism-rules.md) (per-API determinism), so the same content is not maintained twice.

| # | Rule | One-line reason | Details |
|---|---|---|---|
| 1 | **Keep registered windows at 1 MiB or less**, at most 512 chunks | The card's chunk table transfers only one page (512 entries); 1 MiB windows are green at all three sizes measured | [K.1/K.2](K-offload-memory-rules.md) |
| 2 | **Slice large arrays by window** instead of registering them whole | Decouples window size from total volume; 4 GiB through 1 MiB windows is measured | [K.2](K-offload-memory-rules.md) |
| 3 | A single `scif_writeto` stays **at or below 1 MiB** (verified up to 1 MiB) | The ladder from 26496 B to 1 MiB showed matching checksums throughout | [L.2.4](L-api-determinism-rules.md) |
| 4 | Every "written, now read" dependency needs **explicit synchronisation** (`SCIF_RMA_SYNC` or a fence) | The header says it outright: without it the transfer "is non-deterministic" | [L.1](L-api-determinism-rules.md) |
| 5 | Register addresses aligned to the **host page**, lengths to 4 KiB; mind `RLIMIT_MEMLOCK` | Driver parameter checks; pinning is bounded by the ulimit | [L.2.3](L-api-determinism-rules.md) |
| 6 | Confirm **in-flight RMA has completed** before unregistering a window | Otherwise it races DMA in flight (an early version wedged here) | [L.2.3](L-api-determinism-rules.md) |
| 7 | Prefer **COI's own API** over hand-rolled SCIF | COI registers per size class and does the handshake and fences itself | [L.3.1](L-api-determinism-rules.md) |
| 8 | Any new pattern must pass **T4 + T8**, with no `DESC-*` and no `kernel BUG` in dmesg | The criteria already exist | [J](J-acceptance-tests.md) / `tests/extra` |
