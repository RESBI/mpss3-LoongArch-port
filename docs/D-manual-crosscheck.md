# Appendix D — Manual Cross-check: What Intel's Two Manuals Can Vouch For

> This appendix pins every "the manual says" judgement in the report to a line number, and line numbers always refer to the extracts `_work/pdf/mpss_users_guide.txt` and `_work/pdf/knc_isa_manual.txt`. The extracts keep the manuals' page markers (of the form `===== [mpss_users_guide p.40] =====`), so a quotation can be located both by extract line and by manual page. Conversely, **what cannot be found in the manuals is also set out here** — that is exactly what produced the "not verified" list at the end of Chapter 7.

---

## D.1 What the Two Extracts Are

| Extract | Lines | Which document | Who uses it in the report |
|---|---:|---|---|
| `_work/pdf/mpss_users_guide.txt` | 8,241 | Intel MPSS 3.8 User's Guide, April 2017 | Chapter 1 §1.3, Chapter 2 (BAR and the `lspci` transcript), Chapter 3 §3.5 (module parameters), Appendix A.5 |
| `_work/pdf/knc_isa_manual.txt` | 21,487 | Intel Xeon Phi coprocessor instruction set manual | Chapter 5 §5.3 (the instruction set hurdle), Appendix D.5 here |

Neither is a hardware manual: the User's Guide is an **installation and troubleshooting manual**, and the ISA manual is the **instruction set manual for the CPU on the card**. That boundary determines how they are used — the manuals serve only to vouch for statements about "what the card is like"; anything touching host kernel behaviour, PCI enumeration, address assignment or interrupt assignment goes back to the source and the upstream kernel, because the manuals simply do not contain it (item-by-item statistics in D.4).

---

## D.2 Seven Things the Manuals Can Vouch For in the Report

| Manual line | What the manual says | Which statement it vouches for | Where in the report |
|---|---|---|---|
| `:1098`–`:1100` | `BIOS and OS support for large (8GB+) Memory Mapped I/O Base Address Registers (MMIO BAR's) above the 4GB address limit must be enabled.` | the 8 GiB high window is a **firmware switch**, not something the kernel can obtain on its own | Chapter 1, 1.5; P0-A of Chapter 7, 7.11; Chapter 11, step 1 |
| `:1359`–`:1362` | `Region 0: Memory at 3c7e00000000 (64-bit, prefetchable) [size=200000000]` and `Region 4: Memory at ec000000 (64-bit, non-prefetchable) [size=128K]` | the card memory window is 8 GiB and far above 4 GiB; the register window is 128 KiB | the BAR table of Chapter 2, 2.2 and Appendix A.1 |
| `:1370` | `The output shows that both BAR0 (region 0) and BAR1 (region 4) have valid assigned values.` | **terminology warning**: the manual's "BAR1" means region 4 in configuration space, which is not the same thing as the numbering of `pci_resource` | Chapter 1 terminology, the macro table of Appendix A.1 |
| `:1497`–`:1506` | the output of `micinfo -group Board`: `PCIe Width: x16`, `PCIe Speed: 5 GT/s`, `Max payload size: 256 bytes`, `Max read req size: 512 bytes` | the card is an x16 Gen2 device with a 256-byte maximum payload — the 8 GiB window has to be moved through it | Chapter 2, 2.2 |
| `:4943`–`:5053` | eight subsections explaining `watchdog`, `watchdog_auto_reboot`, `crash_dump`, `p2p`, `p2p_proxy`, `ulimit`, `reg_cache` and `huge_page` one by one | of the eight parameters on the `mic.conf:31` line, **every one has a matching subsection** in the manual, and the manual is authoritative for their semantics | the parameter table of Chapter 3, 3.5 |
| `:3027`–`:3030` | `The augmented command line can be read at /sys/class/mic/micN/kernel_cmdline.` plus the entries it lists — `card`, `vnet`, `scif_id`, `scif_addr`, `mem`, `ramoops_size` — showing that they are filled in automatically by `mic.ko` | contract five of Appendix A.5: that string of parameters is written for the card's kernel, not for the host | Appendix A.5, Chapter 3, 3.5 |
| `:8041`–`:8070` | `cat /sys/class/mic/micN/post_code` and the POST code table: `11 Signal host to download coprocessor OS`, `12 Wait for coprocessor OS download`, `13 Signal received from host to boot coprocessor OS` | the division of responsibility is a **fact at the hardware level**: the card gets itself as far as "wait for the host to download an image", and the host only downloads and knocks | Chapter 2, 2.5; the flow chart in Appendix A.5 |

The three POST codes `11`/`12`/`13` are the handover points Intel itself documented. The report's claim throughout that "the host only downloads the image, knocks on the doorbell and takes interrupts" rests on this plus the source in Chapter 3.

---

## D.3 The sysfs Item-by-item Comparison: All 31 Nodes the Manual Lists, Not One Missing in the Source

The "host driver sysfs entries" section of the User's Guide (from `_work/pdf/mpss_users_guide.txt:7205`) lists 31 `/sys/class/mic/micN/*` nodes. Checking every one of them back against `host/linsysfs.c` gives a result of **not one missing**:

| Node | Manual | Source |
|---|---|---|
| `active_cores` | `:7214` | `host/linsysfs.c:119` |
| `boot_count` | `:7266` | `host/linsysfs.c:442` |
| `cmdline` | `:2827` | `host/linsysfs.c:487` |
| `crash_count` | `:7267` | `host/linsysfs.c:452` |
| `extended_family` | `:7322` | `host/linsysfs.c:115` |
| `extended_model` | `:7323` | `host/linsysfs.c:114` |
| `fail_safe_offset` | `:7305` | `host/linsysfs.c:120` |
| `family` | `:7206` | `host/linsysfs.c:228` |
| `flash_update` | `:7304` | `host/linsysfs.c:161` |
| `flashversion` | `:7303` | `host/linsysfs.c:105` |
| `fuse_config_rev` | `:7324` | `host/linsysfs.c:117` |
| `image` | `:7228` | `host/linsysfs.c:396` |
| `kernel_cmdline` | `:3027` | `host/linsysfs.c:502` |
| `log_buf_addr` | `:7276` | `host/linsysfs.c:641` |
| `log_buf_len` | `:7277` | `host/linsysfs.c:671` |
| `meminfo` | `:7325` | `host/linsysfs.c:174` |
| `memoryfrequency` | `:7331` | `host/linsysfs.c:103` |
| `memoryvoltage` | `:7332` | `host/linsysfs.c:102` |
| `memsize` | `:7215` | `host/linsysfs.c:104` |
| `mode` | `:7227` | `host/linsysfs.c:374` |
| `model` | `:7333` | `host/linsysfs.c:110` |
| `pc3_enabled` | `:7312` | `host/linsysfs.c:538` |
| `pc6_enabled` | `:7313` | `host/linsysfs.c:576` |
| `platform` | `:7273` | `host/linsysfs.c:113` |
| `post_code` | `:7274` | `host/linsysfs.c:432` |
| `scif_status` | `:7275` | `host/linsysfs.c:388` |
| `sku` | `:7207` | `host/linsysfs.c:182` |
| `state` | `:2830` | `host/linsysfs.c:361` |
| `stepping` | `:7208` | `host/linsysfs.c:238` |
| `stepping_data` | `:7335` | `host/linsysfs.c:109` |
| `virtblk_file` | `:5155` | `host/linsysfs.c:711` |

The reverse direction is not symmetric: `host/linsysfs.c` has 11 further attributes that this section of the manual does not list — `corevoltage` (`:99`), `corefrequency` (`:100`), `substepping_data` (`:108`), `family_data` (`:111`), `processor` (`:112`), `version` (`:190`), `peer2peer` (`:197`), `initramfs` (`:404`), `pc6_timeout` (`:611`), `serialnumber` (`:697`), `interface_version` (`:704`). One of them, `peer2peer`, is in fact documented, just under a different class path (`/sys/class/mic/ctrl/peer2peer` at `_work/pdf/mpss_users_guide.txt:7196`).

This comparison table serves to fix the **boundary of changes for the port**: these 31 names are what the host user space (`micctrl`, `mpssd`) and the operations scripts look at, and renaming one is changing an ABI. Appendix A.8 freezes them; this appendix gives the source of every name.

---

## D.4 The Manuals' Gaps: The "Not Verified" Items of Chapter 7 Were Not Skipped, They Cannot Be Found

The report lists a number of "not verified" items in Chapter 7, and the easiest one to be challenged on is this kind: **why not look it up in the manual?** The answer is in the statistics below, from a full-text keyword search of the User's Guide:

| Keyword | Manual hits | Meaning |
|---|---:|---|
| `doorbell` | 0 | how the host knocks on the doorbell is not mentioned once in the manual |
| `aperture` | 0 | how the 8 GiB window is mapped into host address space is not covered |
| `MSI-X` | 0 | how many interrupt vectors are used, and whether they can be obtained, is not covered |
| `IOMMU`, `VT-d` | 0 | DMA coherence and address width are not covered |
| `cpuid`, `wbinvd` | 0 | host privileged instructions are not covered (what it covers is the card's instruction set) |
| `page size`, `4 KB` | 0 | the host page size: the manual writes it once, as `4K` at `:5043` (see D.6) |
| `PCIe` | 24 | but all of them are speed, width and BIOS switches, with no enumeration, BAR assignment or bridge windows |

Two conclusions follow. First, **the manual is a user manual, not a hardware manual**, so any judgement in the report about PCI enumeration, address assignment or interrupt assignment can only come from the source and the upstream kernel. Second, the reason the report writes "whether the firmware gives an 8 GiB high window" as a possible veto rather than a conclusion in Chapter 7 is that all the manual lets you check is "BIOS must have this switch enabled" (`:1098`); **it does not let you check "whether this particular board gives it"**.

---

## D.5 Where the ISA Manual Fits

The extract is 21,487 lines; across the whole text `K1OM` scores **0** hits and `Knights Corner` **0** hits — it calls itself the Intel Xeon Phi coprocessor instruction set (table of contents at `_work/pdf/knc_isa_manual.txt:46`–`:55`), and what it describes is the 512-bit vector extension of **the CPU on the card** (from `:568`: 32 512-bit vector registers).

Its direct contribution to this work is therefore zero: the 24 card-side files and 20,232 lines are not changed at all (Appendix B.3), and the only x86 assembly on the host side is the three lines `micscif/micscif_ports.c:150`, `:177` and `:196`, with the instructions `bsfq`/`btrq`/`btsq` — precisely host instructions the ISA manual does not cover.

The condition at `micscif/micscif_ports.c:129` is `#if 1 && (defined(__GNUC__) || defined(ICC))`, which **is not an architecture test**, so on LoongArch these three lines of inline assembly are still taken. That is the exact meaning of "breaks as soon as it is compiled" in F6 of Chapter 5 — this belongs to the easiest class to fix (replace them with the kernel's `ffs()`/`__clear_bit()`/`__set_bit()`), but seeing an `#if` must never be taken to mean the code is not compiled.

What the ISA manual does genuinely match is card-side code, for example the APIC and CPUID conventions of the APs on the card (`_work/pdf/knc_isa_manual.txt:2206`, `:19903`, `:19920`). That belongs to the analysis of "how the card boots", not to this host-side port.

---

## D.6 The One Sentence in the Manual That Mentions Page Size, and the Node It Leads To

The User's Guide, `_work/pdf/mpss_users_guide.txt:5040`–`:5043`, verbatim:

```text
[host]# echo <limit> > /proc/scif/reg_cache_limit
where <limit> is the decimal number of 4K pages.
```

This is the only sentence in the whole manual that has anything to do with the host page size, and the node it describes is implemented in the source like this: the directory entry is `micscif/micscif_debug.c:873`, the node is registered at `:880` (`proc_create`) and `:903` (the old `create_proc_entry` interface), the default value comes from `SCIF_RMA_TEMP_CACHE_LIMIT` (`0x20000`) at `include/mic/micscif.h:110`, and it is loaded into the host's `ms_info.mi_rma_tc_limit` at `host/linscif_host.c:128` and `micscif/micscif_main.c:381` respectively, while **the comparison uses the host page shift**:

```c
	if ((cur_bytes >> PAGE_SHIFT) > ms_info.mi_rma_tc_limit)
```

[`micscif/micscif_rma.c:1471`]; the same test appears again at `:1474`.

Lay the numbers out: `0x20000` is 131,072 pages, which equals **512 MiB** with 4 KB pages and **2 GiB** with 16 KB pages. In other words, once the host page size changes, this node — **already written into the manual and already exposed under `/proc`** — has both its unit and its actual budget multiplied by four: the manual tells the user the unit is 4K pages, while the code counts host pages.

The handling matches the decision in Chapter 7, §7.2: build the host kernel with 4 KB pages (`CONFIG_PAGE_SIZE_4KB`), and keep treating this item, together with F3 and F4, as "latent"; if the deployed kernel takes the 16 KB default, then besides F3 the unit of this node has to be redefined — it is not a crash but **interface semantic drift**, and it belongs to the class that must be named in the on-hardware verification (Chapter 11, §11.4).

---

## D.7 Re-check Commands

```powershell
$g = "_work\pdf\mpss_users_guide.txt"
Select-String -Path $g -Pattern 'doorbell|aperture|MSI-X|IOMMU|VT-d|cpuid|wbinvd'   # all should be empty
Select-String -Path $g -Pattern 'Region 0|Region 4|size='                        # hits the lspci transcript at 1359-1362
Select-String -Path $g -Pattern '4K pages'                                       # should hit only the single line 5043
Select-String -Path "_work\pdf\knc_isa_manual.txt" -Pattern 'K1OM|Knights Corner'   # all should be empty
Select-String -Path "_work\mpss-modules-3.8.6\host\linsysfs.c" -Pattern 'DEVICE_ATTR' -CaseSensitive # counts 43 lines (without -CaseSensitive it hits 83 lines)
```

The last one has to be read like this: in `host/linsysfs.c`, `DEVICE_ATTR` hits 43 lines, one of which, `:84`, is the `DEVICE_ATTR_SBOX` macro this file defines itself (it takes a `_name` parameter); the other 42 lines are 42 attribute definitions — 25 ordinary attributes (`bd_attributes[]` with 23 entries plus `host_attributes[]` with 2) and 17 SBOX attributes. Of those 42, the manual lists 31 and does not list 11; 31 + 11 = 42. 43 = 1 + 42 balances, and the name table in Appendix A.7 is split the same way.
