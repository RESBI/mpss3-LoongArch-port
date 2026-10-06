# CHANGELOG — Documentation and offline site (docs/)

This file records **functional updates** to this sub-project: the technical report, the offload programming manual, the offline site and how it is built. Each entry is one functional update. Entries run **oldest first — the newest update is at the bottom**.

---

## 2026-10-02 — Report body and the offline site

- **Added**The body of the report: 12 chapters plus appendices A–D — task background and overall verdict; hardware form factor and boot contract; dissection of the kernel module; host user space; a point-by-point audit of architecture coupling; kernel API drift; the LoongArch platform; migration roadmap; effort and risk; route comparison; on-hardware verification checklist; conclusion. Appendices cover the interface contract list, the file inventory, references and re-check commands, and a cross-check against Intel's manuals.
- **Added**The offline site: `tools/md2site` pre-renders mermaid diagrams and MathJax formulas into inline SVG at build time, so the output needs no network and no CDN. Cross-references are rewritten automatically, and broken links and "arrows-only" diagrams have a dedicated verification script.
- **Fixed**Site build adapted to this document set: a reference redirection map was added (files mentioned in the text that are not part of the document set are explicitly mapped to plain text), and the build command (source directory, output directory, title, ref-map) was frozen into a script so a rebuild no longer requires reverse-engineering the artefacts.

## 2026-10-05 — Appendices E, F and G

- **Added**Appendix E, "Porting patch inventory": every change made to the tool side, with its acceptance criterion; includes one measured privilege boundary and one measured timing constraint.
- **Added**Appendix F, "Using this card on LoongArch": the card-side runtime inventory, the three viable routes, and the one that does not work (compiler-generated offload).
- **Added**Appendix G, "Feasibility of a newer kernel for the card": what k1om really is, what upstream did to the MIC family, the hard fact that there is no SSE2, and the effort and risk of a forward port.

## 2026-10-06 — Report write-back: measured facts folded into the text

- **Added**Appendix J, "Acceptance test suite": the T0–T8 stage table, the measured results on real hardware (72 passed / 0 failed / 1 skipped), and the mapping to the test scripts.
- **Changed**Appendix H gained two sections: H.13 records the three defects that blocked COI and the measured numbers (including the two limitations that remain), and H.14 records a **self-correction** — the "masked compressed store instruction" criterion that had been written into the guide was disproved by measurement (a working binary contains the same instructions, so the count is not the criterion).
- **Fixed**Section I.11.4 changed from "open item" to "open at the time — since resolved", and the original attribution (that it belonged to the COI application layer) is explicitly marked as wrong.
- **Changed**Appendix F's "next steps" list is marked as fully completed, each item mapped to the relevant section of appendix H; chapter 12 gained the "the card can now be used as a compute device" result plus the two measured limitations.

## 2026-10-06 — Offload programming manual rewritten

- **Added**The *KNC offload Programming Manual* (`OFFLOAD_GUIDE.md`), rewritten around the **COI API itself** and replacing the earlier fragments organised as project chronicle plus per-batch narration (three files consolidated into one).
- **Structure**Seven parts plus six appendices: model and first steps / processes and libraries / compute and threads / data channels / synchronisation / errors and tuning / per-function API reference. It opens with a "find it by question" index, so common needs — bulk data transfer, creating threads, allocating space on the card, managing card-side pointers from the host — are directly addressable.
- **Coverage**Part seven explains the API function by function, grouped by object (prototype, argument values, return codes, key points). A script check confirms **all 63 public functions** declared in the headers are covered. A type-and-constant quick reference is included (buffer flags, access flags, map types, copy types, state and move flags, size limits).
- **Changed**Decoupled from any environment: build commands use placeholders such as `<MPSS prefix>` and `<k1om sysroot>`, and the text contains no machine-specific path, user name or kernel version. Platform-specific field notes live in a clearly separated "Appendix E — field notes (platform differences, not API semantics)", which states that the headers take precedence whenever the two disagree.
- **Verified**Render check: 4 mermaid diagrams and the formula render successfully, with no broken links; table check: 45 table blocks with 0 column-count inconsistencies.

## 2026-10-06 — Documentation layout settled

- **Changed**Documentation is collected under `docs/` in the release tree (23 report files plus one programming manual), alongside `tests/`. Internal references use relative file names, and source-tree paths mentioned in prose resolve relative to the **project root**.
- **Added**The CHANGELOG scheme (one for the whole project plus one per sub-project); this documentation sub-project is this file.

---

## 2026-10-06 — Bilingual delivery and document renaming

- **Changed** Documents now ship in pairs: the Chinese edition's filename ends in `_CN.md`, and the English edition is the same name with `_CN` dropped. Every Chinese document that previously carried no suffix (the twelve chapters, appendices A–K, and the report guide) was renamed, and filenames containing Chinese were given ASCII base names — for example `A-接口契约清单.md` became `A-interface-contracts_CN.md` and `A-interface-contracts.md`.
- **Added** English editions for the whole document set: twelve chapters, appendices A–K, the report guide and the *KNC offload Programming Manual* — 26 files, section for section with their Chinese counterparts. Cross-references inside a document point at the edition in the same language.
- **Added** One label and terminology scheme for the English edition: 甲/乙/丙/丁 as table labels become `table (a)/(b)/(c)/(d)` and as subsection labels in §8.3 become `(a)–(d)`; 段一/二/三 become `Stage 1/2/3`; 射程甲/乙/丙 become `Scope A/B/C`; 闸门一/二 become `Gate 1/2`. Table numbers belonging to Intel's own manuals (such as `Table B.9/B.10`) are left untouched.
- **Fixed** Two defects in the Chinese edition along the way: the §12.5 heading said "six honest boundaries" while the body listed seven (now seven), and one unclosed backtick in §4.4.
- **Verified** Line counts and structure checked file by file (heading levels, table rows, fences, mermaid blocks, formula blocks); the whole tree re-checked for link targets that exist, absence of CRLF, and no residual Chinese or full-width punctuation in the English editions.

## 2026-10-06 — Appendix I gains I.12, appendix J records the T8 measurements

- **Added** [Appendix I](I-page-size-alignment.md) gained **I.12**: the second batch of page-size defects (the host's own window counts scaled twice, leading to `mic_smpt.ref_count < 0`, the `micscif_get_dma_addr` BUG, the endless unregistration retry and the `D` state; plus the two branches of `micscif_get_dma_addr` disagreeing about the page size while peer counts were divided by four into zero), with the evidence, the reason T4 never tripped it, the fix, the verification, and a methodological note that half a fix is more dangerous than none.
- **Added** [Appendix J](J-acceptance-tests.md) records T8: a new row in the stage and result tables (12 criteria), a three-size bandwidth table (wire-side 359.0 / 338.6 / **325.9** MB/s, end-to-end 103.8 / 101.2 / **98.8** MB/s), the "measured pitfalls" list grown from three to five (adding "a reused window's staging area must not overlap window 0's destination" and "a large window's page counts and chunk spans must be interpreted per side"), and one more lesson on criterion design (a check must localise, not just report a mismatch).
- **Changed** The functional-stage total moves from 72 to **84 passed, 0 failed, 1 skipped**.

## 2026-10-06 — Settled: the chunk table transfers only one page (512 entries); Appendix K added; I.12.6's twelve-bit claim disproved

- **Settled (measured)** A 32 MiB card window arrives with 527 chunks of which entries 512 to 526 have a **raw value of `0x0`** (read before the page counts are stripped, via `RAW-SCAN`/`RAW-HEAD`/`RAW-TAIL`) — the card **never wrote those entries** — and 512 is exactly `NR_PHYS_ADDR_IN_PAGE` on the card, whose pages are 4 KiB. The card runs an unpatched upstream module, and the host cannot reconstruct what was never sent.
- **Disproved** I.12.6's earlier argument that the twelve-bit wire page count (4095 pages) is the binding ceiling was **wrong**: the chunking rule (`micscif_detect_large_page`) makes a normal page its own chunk and lets a huge page run to the next 2 MiB boundary, so a chunk holds at most 512 pages and the twelve-bit field is never reached. The section has been rewritten with the process kept; the guard stays as defence in depth but is no longer presented as the cause.
- **Added** [Appendix K](K-offload-memory-rules.md) (and its Chinese twin): user-side constraints for offload memory movement — **at most 512 chunks per window** (the invariant), windows of 1 MiB or less (three sizes green as measured), slicing by window, 26496-byte RMAs, pinning subject to `RLIMIT_MEMLOCK`, plus the dmesg checks.
- **Acceptance** The same bad input (a 32 MiB window) now shows `DESC-BAD ... refusing the copy` with `EINVAL` in user space, **0 `kernel BUG` lines and no `D` state**; a T8 64 MiB run immediately afterwards is still 12/12 with matching checksums.

## See also

- Documentation guide and typographic conventions: `README.md`
- Site build: `tools/md2site/` (source lives outside the project root; used to rebuild the site)
