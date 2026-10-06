# Chapter 12 — Overall Conclusions and Preconditions

> This is the only chapter in the report that can be understood without reading any other chapter. Chapter 1, §1.6, lists it as the endpoint of the "just want the conclusion" path. It introduces no new facts, and every number can be traced back to a source in one of the earlier chapters.

---

## 12.1 The Conclusion in One Sentence

**This Xeon Phi X100 (KNC) MPSS host driver can be ported to LoongArch New World 6.x kernels.** Not one line of card-side code needs changing, only two places are genuinely tied to the host instruction set, and the kernel API debt has been counted line by line (86 lines, 64 of them pure mechanical replacement). What could really veto the whole thing is not code but **whether the firmware is willing to hand out a single contiguous 64-bit prefetchable window of 8 GiB located above 4 GiB** — and that can only be verified on real hardware.

Split into three parts, that sentence is the report's three main threads: the host does only three things (Chapters 2 and 3), there are only two architectural couplings (Chapter 5), and the one possible veto is that window (Chapter 7). The sentence received measured support once, in October 2026: the same tree was moved to a LoongArch machine (kernel 7.1.13, 16 KB pages), modified to the §8.3 specification, and **`mic.ko` was built**. The measured change volume is a good deal larger than the estimate in Chapter 6, §6.10 (28 files, +318 / −309 lines); where the difference lies, and the one file pulled out of the build (`host/vmcore.c`), are both recorded in Chapter 8, §8.3 D. And it went beyond compiling: on that same machine this 7120P was lit up (26 seconds to reach online after the boot command was written), ICMP round trips between the host and the card showed 0% packet loss (including 60 KB large packets), and all three criteria of Gate 1 were passed as well. The data plane also yielded throughput measurements: thirty to fifty MB/s for a single plaintext stream, at least 172 MB/s in one round with four parallel streams, while SSH encryption drops to the teens of MB/s because KNC has no AES instructions. And it goes beyond the kernel side: the host tooling side builds end to end — `libscif`, `libmpssconfig`, `mpssd`, `micctrl`, `libmicmgmt`, `mpssinfo`, `mpssflash`, `miccheck`, COI and MYO all landed on LoongArch, with the call paths of `micctrl --status`, `mpssinfo`, `miccheck` and MYO all exercised successfully on the spot ([Appendix E](E-porting-patches.md)). The tooling side also measured out one privilege boundary and one timing constraint: reading sysfs works for an ordinary user, while functions that go through SCIF require opening the root-only `/dev/mic/scif` first (of the same batch of APIs, 7 passed as an ordinary user and all 18 passed when run as root); and the card-side mpssd's MONITOR_START handshake requires the host mpssd to be listening when the card side starts, otherwise the management port never appears — both are recorded in §E.7 of [Appendix E](E-porting-patches.md). The shortest path for the management plane has also been pinned down: the card-side server is already running on the card, and the host side lacks only `libscif` and a small client (Chapter 4, §4.8).

One more step beyond that has also been achieved: **this card can now be used as a compute device by a LoongArch host**. Process creation through host-side COI is working — the cause turned out to be three concrete defects, not an "application-layer difficulty" ([Appendix H](H-offload-field-notes.md), H.13); what runs on the card is a self-built k1om `libgomp`, and an OpenMP reduction over 2×10⁸ items uses all 240 hardware threads with a relative error of −3.35e-16; for the $O(N^2)$ N-body problem, the checksums at all three problem sizes are **bit-for-bit identical** to the host reference implementation. The acceptance scripts (T0–T8) that turn "it runs" into "it can be re-checked", along with the results as measured on hardware, are recorded in [Appendix J](J-acceptance-tests.md).

---

## 12.2 Three Facts That Make the Conclusion Hold

| Fact | Content | Basis |
|---|---|---|
| The host does only three things | Writes the kernel and initramfs into card memory (the 8 GiB window at BAR0), wakes the card through the SBOX doorbell register at BAR4 (`0xAA08`), and takes one MSI-X interrupt. The card carries its own K1OM operating system, and the host takes no part in running it | Chapters 2 and 3 |
| The host-side module has already been built and run on real hardware | On LoongArch 7.1.13 (16 KB pages), `make -k -j8 MIC_CARD_ARCH=k1om KERNWARNFLAGS=-Wno-error` produced `mic.ko` (a LoongArch ELF, 23,254,080 bytes, readable with `modinfo`); as measured, 28 files, +318 / −309 lines, with `host/vmcore.c` pulled out of the build because its own source is incomplete; probe then succeeded on the same machine (BAR0 16 GiB located above 4 GiB, both DMA masks 64-bit), the card reached `online` within 26 seconds, and host-to-card ICMP round trips showed 0% packet loss | Chapter 8, §8.3 D; Chapter 6, §6.11 |
| Not one line of card-side code changes | Beyond the 38 host objects, another 29 files and 23,029 lines are never compiled on the host (of which 5 files and 2,797 lines are dead code); the host build covers only the other part of the 32,746 lines | Chapter 3; [Appendix B](B-file-inventory.md) |
| Only two places are genuinely tied to the host ISA | `host/uos_download.c:660`–`675` uses `boot_cpu_data.x86` and `x86_model` to determine whether the host is one of the two Intel family 6, model 45/62 platform generations (F1, a semantic deadlock), and `micscif/micscif_ports.c:150`, `:177`, `:196` are three lines of x86-64 inline assembly (F6, the whole block wrapped in the `#if` at `:129`, so compilation breaks immediately). The other 1,103 grep hits were all judged to be card-side code, dead code, or architecture-independent generic constructs | Chapter 5, §5.10 and §5.11 |

The third fact: **in a 32,746-line x86 driver, only two places are truly hard-wired to x86.** The reason is that it left the hardest part on the card.

---

## 12.3 Three Preconditions

Three things must be measured before any code is written. They are not code problems, and changing code will not solve them.

| Precondition | Criterion | What happens if it fails | Basis |
|---|---|---|---|
| BAR0 gets a contiguous 64-bit prefetchable window of ≥ 8 GiB located above 4 GiB | A window of the form `Memory at 3c7e00000000 (64-bit, prefetchable) [size=200000000]` appears in the device resource lines | **This can veto the entire project.** If BAR0 is not allocated at all → `host/linux.c:301`–`:303` prints `failed to reserve aperture space` and exits; if it is merely smaller than 8 GiB → probe gets through, but the memory available on the card shrinks with the window (`host/uos_download.c:592`–`:594`) | Chapter 7, §7.11, P0-A; Chapter 11, §11.2 |
| Both DMA masks are 64-bit | First print `*dev->dma_mask` and `dev->coherent_dma_mask` with a minimal probe module. **The probe must be loaded before `mic.ko`**, because the driver rewrites both values | Probe may fail outright (`host/linux.c:274`–`278`), or the mapping stage may be silently constrained by `bus_dma_limit` | Chapter 7, §7.11, P0-B; Chapter 11, §11.3 |
| The actual page size of the target kernel | `CONFIG_PAGE_SIZE_*`. 4 KB is the least trouble; 16 KB is also acceptable | At 16 KB the real debt is only the 2 lines of F3 (`include/mic/micpsmi.h:56`–`:57`), which can be decoupled from the host page size using a fixed **512 KB** granularity | Chapter 7, §7.11, P2-A; Chapter 11, §11.4; Chapter 5, §5.10, F3 |

Two further items can only be measured, not confirmed by reading code, and they come after the three above: **whether the platform declares this device I/O-coherent** (Chapter 11, §11.5 — the only correctness risk in the whole report), and **whether write-combining actually takes effect** (Chapter 11, §11.6 — it affects throughput only, not correctness).

---

## 12.4 The Cost: Three Numbers

| Number | What it is | What it is not |
|---:|---|---|
| **32,746** | The line count of all 38 host-side objects | **Not the amount to be read.** Scope A covers only the 8,525 lines of groups A, B and C (the grouping in Chapter 3, the sum in Chapter 10, §10.1) |
| **86** | The number of kernel-API change lines verified line by line: 64 pure mechanical replacements and 22 that must be handled semantically. Adding the margin from `host/vhost/` and `host/linvnet.c`/`vnet/`, which were checked only down to the family level, the estimated total is 100–250 lines | **Not a schedule.** Chapter 6, §6.8 explicitly declines to combine this cell into a single number |
| **12–24 / 15–30 / 23–45 person-days** | The schedules for Scope A, Scope B and Scope C (Chapter 9, §9.4), excluding calendar time waiting on the firmware vendor and long-term maintenance | **Not a quote.** It is an extrapolated figure, and the report did not compile a single file on real hardware (Chapter 1, §1.7) |

---

## 12.5 Seven Honest Boundaries

First, this report is the product of **purely static code analysis plus documentary research**: not a single file was compiled on a LoongArch machine, and no physical KNC card was at hand (Chapter 1, §1.7). Every source-level conclusion can be re-checked item by item by file name and line number, and every conclusion relating to firmware or hardware gives criteria only, never assertions.

Second, **there is no upstream precedent for 6.x.** The community did carry the same code to RHEL 8's 4.18, and the patches are on disk (22 files, 2,372 lines), but that round stopped short of the 5.8, 5.10, 5.12 and 5.18 barriers, and the ≥4.14 branch it produced itself no longer compiles on ≥5.15 (the `tsk` parameter of `get_user_pages_remote()`, Chapter 6, §6.7). Any estimate for 6.x based on "the community already ported it to 4.18" will miss part of the bill.

Third, how F1 is handled is **optional, not verified**. Deleting the host-CPU-model test and leaving everything to card-side defaults only proves that the host side will not crash when it does not pass `numa_node=` and `p2p_proxy_thresh=`; it does not prove that card-side SCIF has sensible defaults (Chapter 8, §8.5; Chapter 9, §9.6).

Fourth, whether Route 6 in Chapter 10, §10.7 holds (cut SCIF, keep only boot and networking, a scope of 11,751 lines) depends on a question with no answer on disk: when the card's `micscif.ko` cannot find a host peer, does it **back off gracefully** or **hang in the handshake**? If the answer is the latter, that route no longer saves work.

Fifth, `host/vhost/` and `host/linvnet.c`/`vnet/` were checked only down to the family level this round, not line by line. Chapter 6, §6.9, item 5 already states this, and notes that its inventory still carries the same kind of margin — of the five new families added this round, not one was found by searching for a family name (Chapter 6, §6.10, item 7).

Sixth, the upstream `drivers/misc/mic/` tree was deleted in v5.10 (`80ade22c06ca`, removing 65 files and 21,361 lines), so **there is no free adaptation**. It can serve as a reference book, not as a starting point. The entire cost of long-term maintenance is borne by us.

Seventh, **card-side offload currently carries two measured limitations**: `COIBufferCreate` is unusable on this port's chain (it returns `COI_OUT_OF_MEMORY(13)`), so large data can only be generated by the card side from parameters or transferred in batches; and the **optimization level for card-side sinks must be measured source by source** (in this suite the N-body sink runs only at `-O0`, while the reduction sink works normally at `-O2`; the cause is still undetermined). Both are recorded in [Appendix H](H-offload-field-notes.md), H.13 and [Appendix J](J-acceptance-tests.md); and the explanation once written into the guide — "the mask-compression store instruction is unavailable" — has since been disproved by this report itself in H.14.

---

## 12.6 Recommendation: Choose by Objective

| Objective | What to choose | Cost |
|---|---|---|
| Put this card to use | **Plug it back into an x86 machine** (Chapter 10, §10.6, Route 5) | 0 person-days of porting work, plus a machine that must stay available long-term and whose kernel cannot be upgraded freely |
| Verify that the LoongArch platform can host this class of card | Port it, **stopping at Scope A** | 12–24 person-days |
| Remove x86 for the long term, with this card as an asset that must be kept | Port it, Scope B or C | 15–30 or 23–45 person-days, plus long-term maintenance |
| Find out "will it work" at the lowest cost | Do Gate 1 only | 1–2 person-days, zero code changes |

Three things that are not recommended, all for reasons in Chapter 8, §8.8: do not rewrite this driver, do not count on the upstream MIC driver that has already been deleted, and do not start Gate 2 before Gate 1.

---

## 12.7 If You Decide to Do It: The First Week

The order cannot be reversed, because Gate 1's outcome can veto the whole project, while the sense of progress at Gate 2 is illusory.

| Step | What to do | Person-days | If it fails |
|---:|---|---:|---|
| 1 | Gate 1, steps 1 and 2: confirm the slot is within the enumeration range, and measure BAR0's actual window | 1 | If it is not in the enumeration range or no window can be obtained, stop; the problem is in firmware and hardware |
| 2 | Gate 1, step 3: measure both DMA masks with a minimal probe module (**loaded before `mic.ko`**) | 0.5 | If the masks are narrowed, stop; the driver has no 32-bit fallback |
| 3 | Gate 1, step 6: confirm the page size of the deployed kernel | 0.5 | 16 KB is acceptable; the debt is the 2 lines of F3 |
| 4 | Gate 2: change until all 38 objects compile and link (the 64 mechanical replacements plus 22 semantic rewrites from Chapter 6, §6.8) | 3–5 | Compiler errors are Table A in Chapter 8, §8.3; fix them one by one |

Steps 1 to 3 together come to under 2 person-days; they are all observation and require not one line of code to change. Only after step 4 does Stage 1 from Chapter 8, §8.4 come into play.

---

## 12.8 The Three Numbers Most Easily Misread

The first is **32,746**. It is not the amount to be read or changed, but the total line count of the 38 host-side objects; the 29 card-side files and 23,029 lines are never compiled on the host, and what Scope A actually covers is 8,525 lines.

The second is **86 lines**. That is the line-by-line verified volume of kernel-API changes, not a schedule, and not the entire margin. What really determines the schedule is understanding the semantics of each of the three stages of work, getting the data plane on real hardware to the point where it does not fail silently, and the part that cannot be bought — whether the hardware cooperates (Chapter 8, §8.9).

The third is **0 person-days**. Route 5 in Chapter 10, §10.6 really is 0 person-days of porting work, but its cost is that the machine must exist, must stay running, and can never be decommissioned. **"0 person-days" does not mean "0 cost".**

Read these three numbers correctly, and the rest of this report is just a checklist.
