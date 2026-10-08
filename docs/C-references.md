# Appendix C — References and Re-checks

> Every statement in this report should be something the reader can verify again for themselves. This appendix gathers the sources scattered through the chapters and gives commands that can be copied straight out. **Re-checking depends on no external network resource**: for the upstream kernel paths cited in the report, the file bodies of the corresponding versions have all been fetched to disk and placed in two directories, `_work/.kcache/` and `_work/raw/` (see C.2), so a citation such as `v6.6/arch/x86/mm/pat/set_memory.c:763` can be located word for word in the working directory.

---

## C.1 Primary Sources (All on Disk)

| Path | Size | What it is | Mainly used in |
|---|---|---|---|
| `_work/mpss-modules-3.8.6/` | 104 `.c`/`.h` files / 65,811 lines (124 files in the whole tree) | **Intel's original MPSS 3.8.6 kernel module source tree** (hereafter "Tree A") | the whole report, especially Chapters 3, 5 and 6 |
| `mpss-main/mpss-main/mpss3/mpss-modules/` | same shape as Tree A + 1 patch | the community tree (hereafter "Tree B"), already brought to RHEL 8's 4.18 | the "ready-made precedent" of Chapter 6 |
| `mpss-main/mpss-main/mpss3/patches/` | 7 patches | see C.3 | Chapter 6 |
| `_work/pdf/mpss_users_guide.txt` | 8,241 lines | body text extracted from the MPSS User's Guide, including the `lspci` transcript and the BAR sizes | Chapters 2 and 7 |
| `_work/pdf/knc_isa_manual.txt` | 21,487 lines | body text extracted from the K1OM instruction set manual | Chapter 5, 5.3 |
| `_work/findings/x86-coupling.md` | 690 lines | the raw evidence record for Chapter 5 | Chapter 5 |
| `_work/findings/platform.md` | 520 lines | the raw evidence record for Chapter 7 | Chapter 7 |
| `_work/findings/users-guide.md` | 2,169 lines | key points extracted from the User's Guide | Chapters 2 and 4 |
| `_work/findings/isa-manual.md` | 217 lines | key points extracted from the ISA manual | Chapter 5, 5.3 |
| `_work/findings/rpm-contents/` | per-package file inventory | an inventory of the contents of 98 RPMs | Chapter 4 |

The only difference between Tree A and Tree B is that one patch. For how to re-check it, see the fifth entry in C.5.

The item-by-item cross-check between the two extracted PDF texts and the code and the report is gathered in Appendix D of this report: D.3 compares the 31 sysfs nodes recorded in the User's Guide against `host/linsysfs.c` one by one, D.4 lists what these two manuals do not say at the hardware level, D.6 traces the drift in the unit of `reg_cache_limit`, and D.7 gives the commands that reproduce the cross-check. Manual citations are written uniformly throughout the report as `_work/pdf/file-name.txt:line-number` and can be located directly with `sed` or `read`.

---

## C.2 Audit Intermediates (Each Can Be Read on Its Own)

These files are the **raw record** of the audit process, not the report itself. Every number in the report can be traced back to one of them. The "lines" column in the C.1 and C.2 tables uses the same measure — the number of lines in the file (a final line without a terminating newline still counts as one) — and you can count them one by one yourself with `(Get-Content <file>).Count`.

| File | Size | Contents |
|---|---:|---|
| `_work/host38-families.txt` | 224 lines | the suspicious call sites in the 38 host objects, **family by family and line by line** (with `file:line`) |
| `_work/host38-callsites.txt` | 122 lines | the same, condensed after grouping by kernel interface family |
| `_work/kernel-api-hits.txt` | 392 lines | kernel interface hits across the whole tree (excluding `ras/` and `trace_capture/`) |
| `_work/dma-address.md` | 268 lines | a line-by-line assessment of DMA address usage across the whole tree |
| `_work/findings/_treecount.txt` | 20 lines | the raw output of the directory-tree line count |
| `_work/findings/_archscan.txt` | 267 lines | the raw output of the mechanical scan for x86 coupling |
| `_work/findings/_linsysfs.txt`, `_sysfs_hits.txt` | 171 lines | the sysfs attribute inventory (the source of Appendix A) |
| `_work/api-existence.md` | 195 lines | an item-by-item check of the old-version/6.6 existence of 14 groups of kernel symbols |
| `_work/api-versions-1/2/3.md` | see the files | **patch-level** version determination for kernel symbols (which release removed it, and its 6.6 replacement) |
| `_work/.kcache/` | 196 files / 5,721,503 B, of which 190 are upstream kernel file bodies pinned to a version tag | the source text behind every `vX.Y/path:line` citation in the report. In the file names the slashes after `vX.Y/` become underscores, so `v6.6/include/linux/sysfs.h` is stored as `v6.6__include_linux_sysfs.h` |
| `_work/raw/` | 117 files / 7,596,800 B, of which 78 carry a version prefix | another full set of upstream file bodies, with file names separated by double underscores, for example `v6.6__arch__loongarch__kernel__setup.c`. The Bootlin Elixir identifier-search page for `slow_virt_to_phys` is kept here as well |

---

## C.3 The 7 Patches of the Community Tree

They live in `mpss-main/mpss-main/mpss3/patches/`. The first 5 are for RHEL 7's 3.10 and the last 2 for RHEL 8's 4.18.

| Patch | Size | Lines | Notes |
|---|---:|---:|---|
| `mpss-modules-3.10.0-862.el7.x86_64.patch` | 1,380 B | 33 | RHEL 7.0 |
| `mpss-modules-3.10.0-957.el7.x86_64.patch` | 1,380 B | 33 | RHEL 7.6, the same size as the one above |
| `mpss-modules-3.10.0-1062.el7.x86_64.patch` | 2,148 B | 50 | RHEL 7.7 |
| `mpss-modules-3.10.0-1127.el7.x86_64.patch` | 5,061 B | 134 | RHEL 7.8 |
| `mpss-modules-3.10.0-1160.el7.x86_64.patch` | 5,622 B | 146 | RHEL 7.9, RHEL 7's last |
| `mpss-modules-4.18.0-193.el8.x86_64.patch` | 77,762 B | 2,372 | **RHEL 8.2: the single biggest jump** |
| `mpss-modules-4.18.0-240.el8.x86_64.patch` | 77,762 B | 2,372 | RHEL 8.4, the same size as the one above |

The two 4.18 patches have the same byte count: one patch under two file names. **They are the entire basis for the "ready-made precedent" of Chapter 6**: they prove that the community carried this code from 3.10 to 4.18, across 4 major releases.

---

## C.4 Upstream Kernel Source Cited

The following paths are all in Linux mainline. The report cites them to show **what the LoongArch platform provides and what has already been removed**.

| Path | Used in | What is cited |
|---|---|---|
| `arch/loongarch/Kconfig` | Chapter 7, 7.1 | the list of unconditional `select`s |
| `arch/loongarch/include/asm/page.h` | Chapter 7, 7.2 | the four page-size configurations and the 6.12 reorganisation |
| `arch/loongarch/include/asm/addrspace.h` | Chapter 7, 7.3 | `PHYS_OFFSET`, the mapping windows, `PCI_IOSIZE` |
| `arch/loongarch/include/asm/cache.h` | Chapter 5, 5.5 | `L1_CACHE_SHIFT` |
| `arch/loongarch/include/asm/barrier.h` | Chapter 7, 7.8 | `dbar 0x700` |
| `arch/loongarch/kernel/dma.c` | Chapter 7, 7.6 | the `min()` narrowing in `acpi_arch_dma_setup()` |
| `drivers/pci/controller/pci-loongson.c` | Chapter 7, 7.4 | the LS7A ports and `non_compliant_bars` |
| `drivers/irqchip/Makefile` | Chapter 7, 7.5 | `irq-loongson-pch-msi.o` |
| `drivers/iommu/Kconfig` | Chapter 7, 7.6 | no LoongArch entry; the dependency list of `IOMMU_DMA` |
| `drivers/vfio/Kconfig` | Chapter 10, 10.4 | the original wording of `VFIO_NOIOMMU`, "does not support VM passthrough" |
| `drivers/misc/mic/` (historical path) | Chapter 7, 7.10 | introduced in v3.13, last complete in v5.9, **removed in v5.10** |
| `v6.4/include/linux/device/class.h` | Chapter 6 | from 6.4 onwards `class_create()` takes only one argument |

The upstream commit that removed the MIC driver is **`80ade22c06ca115b81dd168e99479c8e09843513`**, titled "misc: mic: remove the MIC drivers", by Sudeep Dutt, dated 2020-10-28; it deletes 65 files and 21,361 lines. It is the only basis for the table in Chapter 7, 7.10.

---

## C.5 Re-check Commands, at a Glance

### First: the scope is 38 files and 32,746 lines

```powershell
$r = "_work\mpss-modules-3.8.6"
$objs = Select-String -Path "$r\Kbuild" -Pattern '^mic-objs \+= (\S+)$' |
        ForEach-Object { $_.Matches[0].Groups[1].Value }
$objs.Count                                        # should be 38
($objs | ForEach-Object { (Get-Content "$r\$($_ -replace '\.o$','.c'")").Count } | Measure-Object -Sum).Sum
                                                    # should be 32746
```

### Second: the card side and the dead code are out of scope

```powershell
Select-String -Path "$r\Kbuild" -Pattern '^obj-' | ForEach-Object { $_.Line }
# you should see the dma/ micscif/ ... of Kbuild:56-57 hanging under CONFIG_X86_MICPCI
# and trace_capture/ never referenced by any line
```

### Third: the whole tree contains only three lines of x86-64 inline assembly

```powershell
Select-String -Path "$r\micscif\micscif_ports.c" -Pattern '__asm__|asm\(' 
# should hit only lines :150 :177 :196, and all three are inside the #if at :129
```

### Fourth: the whole tree contains no `dma_alloc_coherent`

```powershell
Get-ChildItem $r -Recurse -Include *.c,*.h |
  Select-String -Pattern 'dma_alloc_coherent|dma_map_single|dma_sync_single'
# there should be exactly one: pci_dma_sync_single_for_cpu at host\linpsmi.c:80
```

### Fifth: Tree B is Tree A

```powershell
$b = "mpss-main\mpss-main\mpss3\mpss-modules"
Get-ChildItem $r -Recurse -File | ForEach-Object {
  $q = Join-Path $b $_.FullName.Substring($r.Length + 1)
  ...
}
# compare file by file; the differences should be exhaustible and already covered by the 4.18 patches
```

(The above is the idea behind the **file-by-file comparison**. The complete comparison result is already on disk at `_work/diff-pristine-vs-latest.diff`, 91,868 bytes, and can be read directly.)

### Sixth: whether the layout convention in `report/` has been broken

```powershell
Get-ChildItem report -Filter *.md | ForEach-Object {
  $l = Get-Content $_.FullName
  "{0}: {1} lines starting with a single ideographic space" -f $_.Name,
    ($l | Select-String -Pattern '^\u3000(?!\u3000)').Count
}
# all of them should be 0
```

### Seventh: the true scale of the page-size coupling is 265 lines in 29 files

```powershell
$files = (Get-Content _work\host38-files.txt | ForEach-Object { "$r\$_.c" }) +
         (Get-Content _work\micsphdrs.txt   | ForEach-Object { "$r\$_" })
$hit = Select-String -Path $files -Pattern 'PAGE_SIZE|PAGE_SHIFT'
$hit.Count                                                   # should be 265 lines
($hit | Select-Object -ExpandProperty Filename -Unique).Count   # should be 29 files
```

These two numbers are the source of the note on measures in Chapter 5, §5.5. Dimension 4 of §5.2 says "40 files, 368 hits" — that is a broader measure (including card-side files and branches that are not compiled); it does not contradict the 265 here, and the two statements have been aligned in §5.5.

### Eighth: the accounts for the 104 files close exactly

```powershell
# the same script as the fourth entry in B.5 of Appendix B; only the measure is fixed here:
#   38 host objects            32,746 lines
#   24 card-side files         20,232 lines
#    5 dead-code files          2,797 lines (trace_capture/)
#   36 shared headers           9,775 lines (include/, compiled for both sides)
#    1 host/vhost/vhost.h         261 lines
# files  38 + 24 + 5 + 36 + 1 = 104 (of the tree's 124 files, besides these 104 .c/.h there are 20 build and metadata files)
# lines  32,746 + 20,232 + 2,797 + 9,775 + 261 = 65,811
```

The books close: the file counts and the line counts of the five buckets each add up to exactly the tree's 104 `.c`/`.h` files and 65,811 lines (the other 20 build and metadata files in the tree, 934 lines, were never inside the build or the port to begin with). Wherever the report says "how many lines on the card side", it uses the measure of **the 29 files never compiled on the host, 23,029 lines** (24 card-side files with 20,232 lines plus 5 dead-code files with 2,797 lines); the 36 headers are counted separately, since they are compiled for both sides.

### Ninth: the eight-dimension statistics of Chapter 5, §5.2

```powershell
$env:PYTHONIOENCODING='utf-8'
python _work\check05.py
```

It prints `dimN files … lines … card … dead …` dimension by dimension; the three lines that follow give the eight-dimension totals of 73 files and 1103 lines, and then the 29 files and 265 lines of the page-size measure in §5.5. Any cell that disagrees with the table in Chapter 5, §5.2 prints `FAIL`, and the last line is `fails 0`. Every cell of that table corresponds to one count taken here.

### Tenth: every source citation in the report resolves to the original text on disk

```powershell
$env:PYTHONIOENCODING='utf-8'
python _work\check_cites2.py
```

It extracts every citation of the form `path:line` from `report/` and resolves them in place in three classes: host paths under `_work/mpss-modules-3.8.6/`, upstream kernel paths under `_work/.kcache/` and `_work/raw/`, and user-space paths under the various 3.8.6 source package directories in `_work/`. Any citation with no target, or whose line number exceeds the line count of its file, prints `LOCAL-EOF` / `KERN-EOF` / `USER-EOF` / `UNRESOLVED`. The current result is 329 host paths, 41 upstream and 20 user-space citations, all of them hit, with `fails 0` on the last line.

---

## C.6 Index of Items Explicitly Marked "Not Verified" in the Report

"Not verified" in this report means **the report does not intend to guess**. These items are concentrated in the appendix at the end of Chapter 7; the ones with the greatest impact are:

| Item | Why it cannot be verified | Where |
|---|---|---|
| the real BAR0/BAR4 sizes | requires real hardware to read the PCI configuration space | Chapter 7 appendix, Chapter 11 |
| whether the firmware provides an 8 GiB prefetchable MMIO window above 4 GiB | same as above | Chapter 7, 7.4; Chapter 11, step 1 |
| the `_DMA` bit width the firmware writes | requires reading the ACPI DSDT or printing `*dev->dma_mask` on the machine | Chapter 7, 7.6; Chapter 11, step 3 |
| whether the platform declares the device I/O coherent | requires measurement on the machine | Chapter 7, 7.6; Chapter 11, step 4 |
| which cache attribute `ioremap_wc()` actually lands on, on LoongArch | requires reading that version's LoongArch `pgtable.h`, or reading the page tables on the machine | Chapter 7, 7.7 |
| whether the card's SCIF backs off gracefully when it cannot find a host peer | requires measurement on the card's Linux | Chapter 10, 10.7; Chapter 11, step 2 |

The remaining "not verified" items are in the complete list in the appendix to Chapter 7. This index sets the uncertain apart from the settled.
## C.7 Evidence Files for the On-hardware Compilation Run

The section Chapter 8, §8.3 (d) describes the measured run in October 2026 that compiled this source into `mic.ko` on a LoongArch machine (AOSC OS 13.3.1, kernel `7.1.13-aosc-main-16k`, 16 KB pages). Its evidence is not in the report but in the following files.

| File | What it is |
|---|---|
| `_work/remote-logs/build1.log` … `build9.log` | the raw logs of nine build rounds. `build4.log` is the round with the unmodified full build (564 errors, 37 units failed), `build9.log` is the successful round (0 errors, `mic.ko`) |
| `_work/remote/*.sh` | the remote probes and build scripts: `probe.sh` (environment), `api-probe.sh`/`api-probe2.sh`/`probe_ffs.sh`/`probe_werror.sh` (state of the kernel APIs), `build_tree.sh` (generic build), `verify_ko.sh` (artefact acceptance) |
| `_work/port_patches/batch1.py` and others | the porting patch scripts; every rule requires an exact hit count and writes nothing to disk if the count does not match |
| `_work/port-7.1.13/` | the working copy after porting (the clean tree `_work/mpss-modules-3.8.6/` is unchanged, and all line numbers in the report still refer to it) |
| `_work/port_patches/diff_stat.py` | the algorithm behind "lines actually changed": a per-file unified diff, reporting +318 / -309 lines, 185 hunks, 28 files |
| `_work/remote-build-status.md` | the summary record of this measured round (environment, five builds, change list, drift comparison) |

The measured records for Gate 1 and the three steps that follow it (device recognised, card brought up, data plane) are another batch of files, all under `_work/` and under the project root on the server:

| File | What it is |
|---|---|
| `g1_load.log` | the gate check and its result for loading `mic.ko` (including the taint of the unsigned module, the DMA mask change and the `/proc/iomem` occupancy) |
| `g1_boot.log` | the whole process of flashing the k1om kernel + initramfs: `state` going ready→booting→online, 26 seconds, card-side cmdline |
| `g3_ping.log` | the first data-plane trial (100% packet loss) and how the cause was tracked down |
| `g3_remaster.log` | the whole process of adding `auto mic0` to the card-side image, re-flashing and getting ping through |
| `card_console.log` | card console output captured from the host's `/dev/ttyMIC0` (the login banner of the card's Poky 3.8.6) |
| `_work/remote/g*.sh` | the scripts used at each step above (all with timestamped logs, all re-runnable) |

The third batch (SSH and throughput):

| File | What it is |
|---|---|
| `g4_ssh.log` | the whole process of injecting `authorized_keys` per micctrl's key model, re-flashing the card, waiting for sshd, testing `ssh mic0`, and the 512 MB bidirectional `md5sum` comparison and throughput |
| `g5_through.log`, `g5_final.log`, `g5_final2.log`, `g5_confirm.log` | several rounds of plaintext and encrypted throughput measurement (including a few reworks of the measurement method itself: `nc -l` missing `-N`, the card-side BusyBox `nc` not supporting listen, and the stdin of a background job being assigned `/dev/null`) |
| the `g3_remaster*.log` files other than `card_console.log` | the complete record of the three changes to the card-side image (network interface, SSH, host key) |

The fourth batch (the tool-side port), files and directories:

| Location | What it is |
|---|---|
| `mpss-userland/tar/` | the eight source packages taken from the delivery (libscif, mpss-metadata, gen-symver-map, mpss-daemon, mpss-micmgmt, miccheck, mpss-coi, mpss-myo) |
| `mpss-userland/src/` | the source trees unpacked and changed as described in Appendix E |
| `mpss-userland/stage/` | our own install prefix (`usr/lib64`, `usr/include`, `usr/include/mic`), so the components can compile against each other |
| `mpss-userland/logs/` | the build logs and root acceptance logs for every component |
| `patch*.py`, `gen_defsym.py` | the patch script for every change in Appendix E (idempotent, re-runnable) |
| `_work/remote/g6_*.sh`, `g7_*.sh`, `g8_*.sh` | the driver scripts for each step (with timestamped logs) |

