# Chapter 9 — Effort Estimate and Risk Register

> This chapter does two things only: it gives a set of **person-day ranges** that can be taken straight to a price comparison, and it gives a register sorted by **risk** (not by effort). The former answers the "keep an x86 front-end machine" comparison in Chapter 10 §10.6; the latter answers the table Chapter 1 §1.6 recommends for reporting.

---

## 9.1 The Five Ground Rules of This Chapter

The five ground rules come first, otherwise the numbers below will certainly be misread.

First, **this chapter gives ranges, not a single number**, because the two ends of a range are different in kind: the lower end is "work through the table in Chapter 6 §6.8 and make the changes", the upper end is "prove the data path correct on real hardware". The difference between those two matters far more to the schedule than the difference in line count, which is why Chapter 6 §6.8 splits the 86 lines into 64 mechanical replacements and 22 semantic rewrites and then leaves the estimate to this chapter, with an explicit instruction not to merge them into one number.

Second, the person-days are calculated **bottom-up**: the editing volume is fixed first from the 64-plus-22 split of Chapter 6 §6.8, and then each of the three construction stages in Chapter 8 is costed by its own work content. This supersedes the first version in Chapter 1 §1.5 — that version estimated top-down from the number of interfaces, Chapter 1 §1.5 now carries the numbers from this section, and the shrinking process is written up in 9.3.

Third, none of the numbers **includes calendar time spent waiting**: waiting for the firmware vendor's answer, waiting on procurement, waiting on scheduling. The last line of Chapter 1 §1.7 already declares this boundary, this chapter follows it, and it singles out the one cell of calendar time in Chapter 1 §1.5 (see the third point of 9.3).

Fourth, this chapter has not carried out a single port on real hardware, so these numbers are **inferred values**, not empirical ones. Wherever a line can be traced to source or to an upstream file, I write out the basis; wherever a line is pure inference, I mark it as an estimate.

Fifth, the principle for ordering risks follows Chapter 5 §5.11 and Chapter 7 §7.11: **silent ones come before ones that error, and silent ones that occur at load time come first of all**. Within the same band, "can it be proved by static analysis" comes before "can it be made to work".

---

## 9.2 The Zero-Person-Day Parts

A few items cost zero person-days, but they are entirely different in nature: some need no change, some are not allowed to change. This table is set apart to prevent the most common mis-estimate of all — mistaking a "scope of 32,746 lines" for "32,746 lines to read".

| Item | Size | Nature | Basis |
|---|---:|---|---|
| The 10 card-side modules plus dead code (the 29 files that are never compiled on the host) | 29 files, 23,029 lines (of which 24 card-side files are 20,232 lines) | never compiled on the host, not one line changed | [Chapter 3](03-mpss-kernel-module.md), [Appendix B](B-file-inventory.md) |
| Card firmware and microcode | burned into the card's SPI flash | cannot be changed, and is not on the host | Chapter 1 §1.5 |
| All register offsets, doorbell vectors, SMPT entry bit layout | the seven contracts of Appendix A | **must stay exactly as they are**; change them and nothing works | [Appendix A](A-interface-contracts.md) |
| PCI device ID table | one table | generic, architecture-independent | Chapter 5 §5.10 class C table |
| `trace_capture/` | 5 files, 2,797 lines | dead code, not in the build at all | Chapter 5 §5.10 class C table |

The rows in this table that actually carry a decision are the first and the third. The first says the denominator of the scope is not 32,746 but the host-side portion; the third says that however small the scope gets, the seven contracts of Appendix A still cannot be touched — they are the part that is **zero cost but not optional**, and missing any one of them means the result cannot bring the card up.

---

## 9.3 Where This Disagrees With the First Reading in Chapter 1 §1.5, and Corrects It

The first version of Chapter 1 §1.5 gave two cells of numbers: generational rewrite of the kernel API, "about 500–900 lines of net change", and single-point rewrite, "about 300–600 lines". After Chapter 6 completed the family-by-family alignment, both cells were revised downward, and Chapter 1 §1.5 now carries the right-hand column of the table below. I set out why they shrank, because this ratio is the source of every person-day in this chapter.

Start with the difference in method between the two estimates. The first version of Chapter 1 §1.7 declared itself top-down: "the effort estimate is derived from **code volume plus number of interfaces**"; that sentence has since been replaced with a line-by-line cross-check as the baseline. Chapter 6's method is also a bottom-up cross-check: first find the 62 `KERNEL_VERSION(` hits across the whole tree, confirm that the version guards in this code reach only 4.2.0, then go family by family through the upstream headers to find in which release each symbol disappears, and finally count lines call site by call site, arriving at 86 lines (Chapter 6 §6.8).

The discrepancy comes from two places. One is **phantom counting**: grabbing hits by keyword across the whole tree also sweeps in branches that do not take part in the host build, comment text, and functions that share a name but mean something else. Chapter 6 §6.9 and the class B table of Chapter 8 §8.3 have already eliminated these one by one; the two most typical are `set_mb` (two of the three hits are comments, and the third cannot be reached at all on 6.x) and `read_from_oldmem` (it collides with a kernel symbol, but it is in fact a static function MPSS wrote itself, `host/vmcore.c:164`).

The other place matters more: **"looks like an old API" is not the same as "is an old API"**. Subsection (2) of Chapter 1 §1.4 judged `sysfs_get_dirent()` and `sysfs_notify_dirent()` to be "private symbols the RHEL kernel keeps solely for compatibility, absent from both upstream and the LoongArch kernel", and on that basis put them on the "must be replaced" list. That judgement is wrong. This round verified the upstream file verbatim: `v6.6/include/linux/sysfs.h:643` has `sysfs_get_dirent()`, `:655` has `sysfs_put()`, and `:638` has `sysfs_notify_dirent()`; all three are `static inline`, and all three sit **outside** the `#endif /* CONFIG_SYSFS */` at `:618`, that is, they are provided unconditionally. The same file also has `sysfs_get()` at `:649`. These lines survived all the way to `v6.12` (`v6.12/include/linux/sysfs.h:638`, `:643`, `:655`). So the two call sites, `host/linux.c:339` and `:404`, **need no change at all**, and this cell should move from "must fix" to the class B table of Chapter 8 §8.3. The judgement in the class B table of Chapter 8 §8.3 is correct; Chapter 1 §1.4 has also been updated to this round's verification, and this family now appears in the "zero changes" column.

Two other sentences in Chapter 1 §1.4 have to be corrected in the same way. The first is `PAGE_SIZE`: it was filed under "a pure rename, done as soon as it is changed", whereas in fact it is not a renaming problem at all but a semantic problem of **having to be decoupled from the host page size**. This has to be stated precisely, or one will assume this family is a large debt: searching the host path (38 objects plus 36 headers) for `PAGE_SIZE` / `PAGE_SHIFT` yields **265 lines spread across 29 files**, but the overwhelming majority are conversions that are internally consistent on the host — round-trip conversions such as `nr_pages << PAGE_SHIFT`, or buffer lengths such as `snprintf(buf, PAGE_SIZE, …)` — and when the page size changes they all change together without going wrong. Only two places genuinely have to be decoupled: F3 in Chapter 5 §5.10 (`include/mic/micpsmi.h:56`–`:57`, 2 lines, using a granularity constant computed from the host page as a fixed granularity) and F4 (`host/uos_download.c:344`–`:349`, writing host page numbers into the card's GTT table, which is not triggered on KNC). Chapter 5 §5.5 lists these two together with the cache-line-coverage one (H2) as "page size assumptions". The second sentence concerns the page size itself: subsection (4) of Chapter 1 §1.4 originally read "page size: 4 KB on x86, 4 KB by default on LoongArch, consistent", whereas P2-A in Chapter 7 §7.11 and the criteria in Chapter 11 §11.4 are both written against a **16 KB default**. The latter is right, and both subsection (4) of Chapter 1 §1.4 and the page-size row of §1.5 have been rewritten accordingly.

After the corrections, the two cells read as follows:

| Original judgement in the first version of Chapter 1 §1.5 | Figure after this round's verification | Where the difference lies |
|---|---|---|
| Generational API rewrite: about 500–900 lines | **100–250 lines**. Of these, 86 lines have been verified line by line (Chapter 6 §6.8), and the remainder is the part of `host/vhost/` and `host/linvnet.c` / `vnet/` that was taken only to family level | phantom counting plus wrong classification |
| Single-point rewrite: about 300–600 lines | **this cell no longer holds**. The real problem in `host/vmcore.c` is a symbol name collision, and the community patches handle it by renaming consistently across the board: 9 lines (`host/vmcore.c:164`, `:273`, `:367`, `:449`, `:629`, `:663`, `:697`, `:723`, `:755`) | a rename was estimated as a rewrite |
| Platform resources brought up: days to weeks | **this cell is calendar time, not person-days**. It is the waiting time for the firmware vendor to agree to release the 8 GiB high window, and this chapter does not fold it into any person-day range | different units, cannot be added |

One methodological self-correction is worth adding here. In my first round I too judged `sysfs_get_dirent` to be "must be replaced", on the grounds that "RHEL has it, upstream does not" sounded entirely reasonable. It is wrong, and wrong in a very typical way: **the judgement rested not on the upstream file but on somebody's description of the upstream file.** This round I converted all judgements of that kind to verbatim evidence; the seventh point of Chapter 6 §6.10 records the remaining drift sites that can only be found by line-by-line alignment.

---

## 9.4 Schedule: Person-Days by Stage

The totals first, then the basis.

| Scope | Groups retained | Lines | **Effort (person-days)** | Converted (1 person) |
|---|---|---:|---:|---|
| **A — survives** | A + B + C | 8,525 | **12–24** | about 2.5–5 weeks |
| **B — usable** | A + `linvcons` + `linvnet` + `vnet/*` | 11,751 | **15–30** | about 3–6 weeks |
| **C — full** | all 38 objects | 32,746 | **23–45** | about 5–9 weeks |

The three rows add up; they are not three parallel options: B equals A plus one row, and C equals B plus one row. Below is how the five rows are added.

| No. | Work | Person-days | Basis | Nature |
|---|---|---:|---|---|
| 1 | Gate 1: measure the three veto items on real hardware | 1–2 | the first three of the six steps in Chapter 7 §7.12, pure observation, zero code changes | estimate |
| 2 | Gate two: closed-loop compile of the 38 objects | 3–5 | the 64 mechanical replacements plus 22 semantic rewrites of Chapter 6 §6.8, plus three timer callback signatures (`host/linvcons.c:150`, `host/uos_download.c:1500`, `micscif/micscif_rma_dma.c:842`) | estimate, basis verified |
| 3 | Stage 1: the device is recognised | 2–4 | the four criteria of Chapter 8 §8.4: module loads, both BARs reserved, 1 MSI-X, `mic0` appears | estimate |
| 4 | Stage 2: the card boots | 3–6 | Chapter 8 §8.5: the state machine, the F1 handling, the image verification path must all stay as they are | estimate |
| 5 | User space | 1–3 | the user-space row of Chapter 1 §1.5, carried over as is | estimate |
| 6 | Documentation and regression | 2–4 | no source basis, pure inference | estimate |
| 7 | Stage 3: the data path, `vcons` and `vnet` (Scope B) | 3–6 | the Stage 2 criteria of Chapter 11 §11.7: a `mic0` network interface appears on the host, and the card side can obtain an address | estimate |
| 8 | Stage 3: the data path, SCIF, vhost and `vmcore` (Scope C) | 8–15 | the Stage 3 criteria of Chapter 11 §11.7: a SCIF connection, one RMA, a block device mounted, a dump exported | estimate |

The addition: Scope A equals items 1 to 4, plus 5, plus 6, that is 1 + 3 + 2 + 3 + 1 + 2 to 2 + 5 + 4 + 6 + 3 + 4, which is **12–24 person-days**. Scope B equals A plus item 7, that is **15–30 person-days**. Scope C equals B plus item 8, that is **23–45 person-days**.

Not one row in this table has "writing code" as its main work. The largest cell, item 8 (8–15 person-days), corresponds to the two blocks that the fifth point of Chapter 6 §6.9 deliberately leaves open, plus understanding the semantics of SCIF and vhost; its criterion is not a compiler but a **byte-by-byte round-trip comparison** (Chapter 11 §11.5). Items 3 and 4 together come to 5–10 person-days and correspond to the specific series of printk statements in Chapter 8 §8.4 and §8.5 — that is, the effort in those two stretches goes mostly into understanding the state machine and the image path, not into editing lines.

One more item of debt is not in the table but has to be written down: **long-term maintenance**. The upstream tree was already deleted in v5.10, so there is no free adaptation (P2-C in Chapter 7 §7.11). Every time the kernel gains a major release, the list in Chapter 6 has to be re-run; at the magnitude of this round's 86 lines, I estimate **1–3 person-days** each time. That number is pure inference, but if the decision horizon is ten years, it matters more than any other number in this section.

Finally, one hard boundary: **every number in this section is an inferred value.** This report has not compiled a single file on a LoongArch machine, and there is no KNC card in hand (Chapter 1 §1.7). Anyone using this table as a quotation should first discount it by the boundary declared in Chapter 1 §1.7.

---

## 9.5 Risk Register

Sorted by the principle in the fifth point of 9.1: silent ones come before ones that error, and silent ones that occur at load time come first of all; within the same band, "can it be proved by static analysis" comes before "can it be made to work". The numbering in the table follows the original register number in each chapter; this chapter does not invent a new set.

| Rank | Risk | Silent or error | Where it can be judged | Basis |
|---:|---|---|---|---|
| 1 | Whether the data round trip between card and host is byte-for-byte correct (whether the platform declares this device I/O coherent) | **silent**, and the only correctness risk in this report | real hardware only; see the ledger below | Chapter 11 §11.5 silent item one, Chapter 8 §8.6 |
| 2 | Whether both DMA masks are 64-bit (whether firmware `_DMA` narrows it) | possibly silent (it may also error at probe) | real hardware only, **do this first** | P0-B in Chapter 7 §7.11, Chapter 11 §11.3 |
| 3 | Whether BAR0 can obtain a 64-bit prefetchable window of ≥ 8 GiB located above 4 GiB | errors, but can veto the whole project | real hardware only | P0-A in Chapter 7 §7.11, Chapter 11 §11.2 |
| 4 | The semantics of the host CPU model lookup table (F1) cannot be carried over as is | **silent**; what is lost is the suggested value handed to the card-side SCIF | statically confirmed, handling can only be by parameter | F1 in Chapter 5 §5.10, Chapter 8 §8.5 |
| 5 | Write combining degrades to strongly ordered uncached | silent, only throughput is lost | real hardware only | P1-B in Chapter 7 §7.11, Chapter 11 §11.6 |
| 6 | "No IOMMU, direct mapping" taken as the default and never written down as a precondition (F5) | silent, and it only blows up if an IOMMU is attached in future | can be written down as a deployment constraint | F5 in Chapter 5 §5.10, item 4 of Chapter 6 §6.8 |
| 7 | Descriptor ring addresses come from mapping results and are used directly as the physical address the card sees (the host branch goes through `mic_map_single()`, see `dma/mic_dma_lib.c:216` / `:391`; the card-side branch uses `virt_to_phys()`, see `:213` / `:388`) | silent | statically confirmed, correctness underwritten by the measurement in rank 1 | Chapter 8 §8.6, `micscif/micscif_smpt.c:192`–`:209` |
| 8 | The 2 lines of F3 when the page size is deployed as 16 KB | silent | can be judged locally, the change is only 2 lines | F3 in Chapter 5 §5.10, Chapter 11 §11.4 |
| 9 | Write ordering of doorbells and descriptors under a weakly ordered memory model | **intermittent**, the hardest to reproduce | real hardware only | P1-C in Chapter 7 §7.11 |
| 10 | The batch that breaks at compile time (about 64 mechanical replacements plus 3 callbacks) | errors, loudest and cheapest | can be judged locally | Chapter 6 §6.8 |
| 11 | No maintainer upstream for anything of the same kind, so every kernel upgrade is carried alone | no error, cost counted per year | can only re-run the Chapter 6 list per release | P2-C in Chapter 7 §7.11 |

This table should be read across as well. Ranks 1, 2 and 3 **can all be judged only on real hardware**, and they are precisely the three that decide "whether it can be done" and "whether it will be done right". Rank 10 accounts for the largest editing volume in this report, yet it is the only band that needs neither thought nor testing. This is what Chapter 8 §8.9 means by "sort by risk, not by effort": **the band you change the most is the least important, and the band you cannot change is the most important.**

Ranks 1 and 7 are two sides of the same thing. The landing point of the axiom in rank 1 is the descriptor ring address: the `#ifdef _MIC_SCIF_` card-side branch calls `virt_to_phys()` directly (`dma/mic_dma_lib.c:213`, `:388`), the `#else` host branch calls `mic_map_single()` (`:216`, `:391`), and `mic_map_single()` internally calls `pci_map_single()` first and then does an SMPT registration (`micscif/micscif_smpt.c:192`–`:209`); afterwards `:218` and `:393` use `pci_dma_mapping_error()` to validate the return value of `mic_map_single()`. On a directly mapped platform without an IOMMU these values happen to be exactly the physical addresses, so on x86 it is correct. Across all 38 compilation units in the tree there is not one modern DMA synchronisation call; the only synchronisation action is a single `wmb()` at `dma/mic_dma_lib.c:417`, and there is one further `pci_dma_sync_single_for_cpu()` at `host/linpsmi.c:80`. In other words, this code treats "a DMA address equals a physical address" and "CPU and device are coherent by nature" as two axioms, and on LoongArch those two axioms are guaranteed by the firmware and by the platform respectively — the former by rank 2, the latter by rank 1.

Three sentences for reporting:

- The debt on the code side is **enumerable and clearable**: 86 lines have been verified line by line, of which 64 are pure mechanical replacement.
- Whether the project stands depends on three things that can be measured only on real hardware, two of which involve the firmware.
- The only thing that cannot be proved by reading code is data consistency, and its criterion is a byte-by-byte comparison, not a compile.

---

## 9.6 Honest Boundaries

The first point needs separate mention, because Chapter 8 §8.5 has called on this chapter by name to give a conclusion. The second way of handling F1 — leaving `p2p_proxy_thresh=` and `numa_node=` entirely to the card-side defaults — is **an option, not a verified result**. The reason Chapter 8 §8.5 gives is that `host/uos_download.c:662`–`675` appends these two parameters as optional, and appends neither of them when the condition is not met. That proves only that the **host side** does not crash when it passes neither parameter; it **does not** prove that the card-side SCIF has sensible defaults when neither parameter is present. I did not find what that default is, either in the card-side code or in the material on disk, so this has to be measured on real hardware: cut that decision block and see whether the card-side SCIF can still establish a connection. This point is at the same time the content of the first piece of work in Scope B and Scope C, written up in Chapter 11 §11.7.

The second point: the round-trip byte comparison in Chapter 11 §11.5 is the only correctness criterion in this report, that is, rank 1 in the table in 9.5. It cannot be proved from what is on disk, because it depends on whether the firmware declares the device I/O coherent, not on the code.

The third point: whether route six in Chapter 10 §10.7 (cut SCIF, scope 11,751 lines) holds depends on a question that has no answer on disk: when the card's `micscif.ko` cannot find its host peer, does it back off gracefully or hang in the handshake? The answer would directly rewrite item 7 of 9.4, so **it has to be measured first**, by the method written up in Stage 2 of Chapter 11 §11.7.

The fourth point: every person-day in this chapter is an inferred value. This report has not compiled a single file on a LoongArch machine, and there is no KNC card in hand (Chapter 1 §1.7). I marked every row of 9.4 as "estimate", and only the editing volume in item 2 has a source basis.

The fifth point: the two corrections in 9.3 are judgements actually changed this round, not typographical repairs: the `sysfs_get_dirent` family moved out of "must fix", and the page size changed from "consistent" to "inconsistent but solvable". Chapter 1 §1.4, §1.5 and §1.7, and Chapter 8 §8.3, have all been updated accordingly, and the report no longer contains any statement to the contrary.

The sixth point: I have not estimated any time for "waiting on the firmware vendor", nor any time for procurement, scheduling, or "finding someone willing to change the firmware". That is what the "days to weeks" cell in Chapter 1 §1.5 refers to; it is calendar time and cannot be added to the person-days of this chapter.

---

## 9.7 A Direct Comparison With "Keep an x86 Front-End Machine"

Route five in Chapter 10 §10.6 requires the "0 person-days" cell to be laid on the table, and Chapter 10 §10.10 says Chapter 9 has to supply schedule figures for that comparison. This section is that comparison.

| Criterion | Route one: port it (this chapter 9.4) | Route five: plug a x86 machine back in |
|---|---|---|
| One-off manpower | Scope A **12–24 person-days**, Scope B 15–30, Scope C 23–45 | **0 person-days** of porting work |
| Additional hardware | none needed (the LoongArch machine is already there, the card is already there) | an x86 host is needed, and it must remain usable long term |
| Long-term maintenance | re-run the Chapter 6 list on every kernel major release, estimated 1–3 person-days each time | follows the distribution, adaptation cost sits on the distribution side |
| What the card can do | Scope A drops SCIF, the card's network interface and its virtual console; Scope C keeps everything | keeps everything |
| Precondition for it to hold | firmware hands over the 8 GiB high prefetchable window (P0-A in Chapter 7 §7.11); no IOMMU must stay no IOMMU | none |
| What happens if it does not hold | Gate 1 fails; the problem is not in the code but in the hardware and firmware | not applicable |

One reading note: **"0 person-days" does not equal "0 cost".** The price of route five is that the machine has to exist, has to stay alive, can never be retired, and its kernel cannot be upgraded at will. Its advantage is that this price is **budgetable**, whereas part of the upper end of the port (45 person-days) depends on whether the firmware vendor is willing to cooperate, and another part is needed only for Scope C.

Checked against the three purposes in Chapter 10 §10.6:

- If the purpose is "to make use of this card": choose route five. The port is technically feasible, but its value depends on the purpose, not on the feasibility.
- If the purpose is "to verify that the LoongArch platform can host this class of card": choose route one, and **the 12–24 person-days of Scope A are the end point**; there is no need to go as far as C.
- If the purpose is "to move away from x86 in the long run" and this card is an asset that must be kept: then those 12–24 person-days are only the first stage, and long-term maintenance has to be costed separately, per the last item of 9.4.

---

## 9.8 Chapter Summary

Four sentences.

First, the code-side debt is about four times smaller than Chapter 1's first reading: the generational API rewrite is **100–250 lines**, of which 86 have been verified line by line and 64 are pure mechanical replacement (Chapter 6 §6.8); the two cells in Chapter 1 §1.5 have been revised downward to this round's numbers, the `sysfs_get_dirent` family has moved out of "must fix", and the page size has changed from "consistent" to "inconsistent but solvable". These three corrections are among the conclusions of this chapter.

Second, the schedule stage by stage is **12–24 person-days** (Scope A), **15–30 person-days** (Scope B) and **23–45 person-days** (Scope C), excluding the calendar time spent waiting on the firmware vendor and on procurement, and excluding long-term maintenance.

Third, the ordering of risks differs from the ordering of effort: the top-ranked item (whether the data round trip between card and host is byte-for-byte correct) requires not one line of code to change and can only be established by measurement on real hardware, while the band with the largest editing volume (the 64 lines that break at compile time) ranks last.

Fourth, compared with "keep an x86 front-end machine", the first stage of a port is 12–24 person-days, while the first stage of route five is 0 person-days plus a machine that has to stay alive long term. If the purpose is only to make use of this card, route five is cheaper; if the purpose is to verify that the LoongArch platform can host this class of card, the 12–24 person-days of Scope A are the end point. This comparison is decision input, not a conclusion of this chapter — the conclusions of this chapter are the first three only.
