# Chapter 11 — On-Hardware Verification Checklist

> The previous ten chapters have been arguing on paper. This chapter turns that into a sequence of actions you can carry out on a real machine: **what to test first, which print to look at, what counts as a pass, and whom to suspect first when it fails.** Items near the front are "fail here and you cannot proceed"; items near the back are "fail here and it is merely inconvenient".

---

## 11.1 The Principle Behind the Verification Order

There is only one principle, and it is the same one used in Chapters 5 and 7: **test first what would kill the whole project, then what would silently compute the wrong answer, and last what is merely inconvenient.** On that basis all verification items fall into three classes:

| Class | Criterion | Consequence of failure | Sections |
|---|---|---|---|
| **Disqualifiers** | Is there enough platform resource? | The whole project cannot go ahead | 11.2, 11.3, 11.4 |
| **Silent items** | Will it compute the wrong answer without reporting an error? | It runs, but the data is wrong | 11.5, 11.6 |
| **Functional items** | Does the card work? | A difference in how much of it works | 11.7 |

Chapter 7, 7.12 already gave a **design** for a "six-step minimal verification". This chapter is its **execution version**: it fills in the concrete observation commands, the concrete print strings and the concrete criteria, and reorders them by "how expensive is failure". The two do not conflict; this one is more detailed.

---

## 11.2 Disqualifier 1: Does the Firmware Hand Out an 8 GiB High Prefetchable Window?

**This is the number-one criterion in the whole report.**

The first step is to **ask the device itself**:

```bash
lspci -vvv -s <card BDF>
```

There are two criteria, and both are required:

| ID | Which line to read | Pass criterion |
|---|---|---|
| A1 | `Region 0:` | The size must be **8 GiB** (in the form `[size=8G]` or `[size=200000000]`), and the address must be **above 4 GiB** |
| A2 | The attribute string on `Region 0:` | Must contain both `64-bit` and `prefetchable`; `Region 4:` must be `64-bit, non-prefetchable` and 128 KiB |

A real-machine transcript in the MPSS user guide has exactly this shape and can be used for comparison:

```
Region 0: Memory at 3c7e00000000 (64-bit, prefetchable) [size=200000000]
Region 4: Memory at ec000000 (64-bit, non-prefetchable) [size=128K]
```

The **contrast** between these two lines: BAR4 asks for only 128 KiB, and landing below 4 GiB is perfectly fine; BAR0 asks for 8 GiB, so an x86 host bridge necessarily places it above 4 GiB. **The question "does the firmware have the ability to move a large BAR up high" is really the question of where BAR0 lands.** If the size BAR0 reports is not 8 GiB, stop right there — Chapter 7, 7.4 has already established that upstream has nothing equivalent to x86's "Above 4G Decoding" to turn on.

The second step is to **ask the kernel whether it actually handed the space out**:

```bash
sudo dmesg | grep -i -E 'pci|BAR'
cat /proc/iomem | grep -i -A2 mic
```

The pass criterion is: a **contiguous 8 GiB** `mic` entry appears in `/proc/iomem`. If `request_mem_region` did not get the region, the driver prints:

```
mic 0: failed to reserve aperture space
```

[`host/linux.c:303`]. Seeing that line means 11.2 failed; there is no point going further.

---

## 11.3 Disqualifier 2: Has the Firmware Narrowed the Coherent DMA Mask?

Chapter 7, 7.6 already established that LoongArch's `acpi_arch_dma_setup()` reads the firmware's `_DMA` and then **takes a minimum once each** against `*dev->dma_mask` and `dev->coherent_dma_mask`. And the driver's streaming mask **wants 64 bits and has no fallback**:

```c
	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		printk("mic %d: ERROR DMA not available\n", brdnum);
		goto probe_freebd;
	}
```

[`host/linux.c:274`–`278`]

### One action in the measurement that must come first

**The probe module must be loaded before `mic.ko`.** The reason is that `pci_set_dma_mask()` **rewrites** `dev->dma_mask`. Once `mic.ko` is loaded first, what you measure is the value after that rewrite (`0xFFFFFFFFFFFFFFFF`) rather than the initial value the firmware provided — and that will always measure as a pass.

The probe module needs only twenty-odd lines and does three things:

1. find the device with `pci_get_device()`;
2. `pci_enable_device()`, then **read only, never write**: print `*pdev->dev.dma_mask`, `pdev->dev.coherent_dma_mask` and `pdev->dev.bus_dma_limit`;
3. print `dma_get_required_mask(&pdev->dev)`.

A supplementary method is to read the firmware's ACPI tables directly and see whether it wrote a `_DMA` for this device.

The criteria tally as follows:

| Observation | Conclusion |
|---|---|
| All three values are 64-bit | 11.3 passes; go on to 11.4 |
| `bus_dma_limit` < 64 bits | **P0-B hit.** There are two possible outcomes here (Chapter 7, 7.6 explains them): probe fails outright with `mic 0: ERROR DMA not available`, or probe succeeds but DMA is silently confined within `bus_dma_limit`. The former is loud, the latter is not — so this one has to be pinned down, and "the module loaded successfully" is not an acceptable pass criterion |
| The coherent mask is 32-bit | It prints `mic 0: ERROR pci_set_consistent_dma_mask(64) %d` and then **falls back to 32 bits and retries once** (`host/linux.c:279`–`287`). It may still pass, but it means the card's memory can only land below 4 GiB |
| The streaming mask is 32-bit | It prints `mic 0: ERROR DMA not available` and probing terminates |

---

## 11.4 Disqualifier 3: Page Size

There is only one observation point: `CONFIG_PAGE_SIZE_*` in the kernel you deploy.

```bash
zcat /proc/config.gz | grep -E 'PAGE_SIZE|HAVE_PAGE_SIZE'
```

Pass criterion: configured for **4 KB pages**. If the kernel can only do 16 KB, the impact is **bounded and enumerable**, as Chapter 5, 5.5 has already judged point by point:

| ID | Location | Consequence with 16 KB pages | Severity |
|---|---|---|---|
| **F3** | `include/mic/micpsmi.h:56`–`57` | `MIC_PSMI_PAGE_SIZE` goes from 512 KB to 2 MB, and the PSMI page-table granularity changes with it | **High.** If the card side expects to read such a table as 512 KB, it comes out wrong |
| **F4** | `host/uos_download.c:344`–`349` | GTT entries use host page numbers | **Not triggered on this platform**: `GTT_WRITE` has exactly one call site in the whole tree, in the `FAMILY_ABR` branch, which KNC does not take. This is one of the judgments corrected in Chapter 5 |
| **H2** | `include/mic/micscif.h:124`, `include/mic/mic_dma_md.h:87` | Forcibly `#define L1_CACHE_SHIFT 6` | Low. The LoongArch L1 cache line happens to be 64 bytes too, so this is not a problem |

So the only real debt under 16 KB pages is **F3 alone**, and it can be fixed the way Chapter 5, 5.5 describes, by replacing `PAGE_SIZE` with a fixed 512 KB granularity decoupled from the host page. **Page size is a risk, not a disqualifier.**

Beyond F3/F4/H2 there is one more item to add to the list — a **definition drift** (not a coupling, and the point of measuring it differs from the three above): the SCIF registration cache limit. The user guide defines the value of `/proc/scif/reg_cache_limit` as "a decimal number counted in 4 KB pages" (`_work/pdf/mpss_users_guide.txt:5043`), while the code compares it as a page count (the 0x20000 at `include/mic/micscif.h:110`, together with `cur_bytes >> PAGE_SHIFT` at `micscif/micscif_rma.c:1471`, `:1474`). So the limit is 512 MiB under 4 KB pages and 2 GiB under 16 KB pages — the code does not compute anything wrong; the reason to measure it is that **after a page-size change, the same number no longer means physically what the manual says it means**, and anyone tuning parameters by the manual would be off by a factor of four. On hardware, read `/proc/scif/reg_cache_limit` once and record it in the verification log together with the page size of the kernel you deployed; the analysis is in [Appendix D](D-manual-crosscheck.md) §D.6.

---

## 11.5 Silent Item 1: Does the Platform Treat This Device as I/O Coherent?

This is the **only correctness risk in the whole report** and it has to be measured on its own. The reasons were laid out in Chapter 7, 7.6: the driver has **zero** `dma_alloc_coherent` calls, **zero** `dma_sync_single_*`, and the only synchronization operation anywhere is a single `wmb()` at `dma/mic_dma_lib.c:417`. Its entire dependence on cache coherency rests on "the platform will declare this PCIe device I/O coherent".

Start with the cheap checks:

```bash
ls /sys/class/iommu            # should be empty
ls /sys/bus/pci/devices/<BDF>/  # should show no iommu_group
dmesg | grep -i iommu           # should produce no relevant output
```

But those three only prove "there is no IOMMU"; they **cannot** prove "the device is I/O coherent". The latter can only be measured like this:

Download an image or data block with **known contents** into the card, then have the card read that content back to the host in the same layout and compare it byte for byte. This is not a performance measurement but a single round-trip data comparison; once it fails, there is no point tuning any of the code that comes before it.

---

## 11.6 Silent Item 2: Which Attribute `ioremap_wc()` Actually Lands On

One thing has to be done before this step: **confirm that `writecombine=on` has taken effect**. `CONFIG_ARCH_WRITECOMBINE` in the LoongArch 6.6 kernel is off by default, and while it is off `ioremap_wc()` silently degrades to SUC (`v6.6/arch/loongarch/Kconfig:479`–`:493`, `v6.6/arch/loongarch/kernel/setup.c:171`–`:182`; see [Chapter 7](07-loongarch-platform.md) §7.7 for details). Without that parameter, step 2 below actually measures SUC and the conclusion comes out inverted. The observation method is to compare:

1. boot with `writecombine=on`, map the same stretch of device memory with `ioremap()`, and measure sequential write throughput;
2. map the same stretch with `ioremap_wc()` and, **after confirming the boot parameter took effect**, measure the same thing.

If after adding `writecombine=on` the two **still show no measurable difference**, then WUC really is unusable on this bridge (consistent with the judgment at `v6.6/arch/loongarch/Kconfig:488`–`:491`), and the download throughput of the 8 GiB card memory window has to be re-estimated on the basis of strongly-ordered uncached access. **This is not an error; it is a budget correction.**


The **baseline for this step has already been measured**, which saves one round of on-hardware work: in plaintext single-stream, card-to-host is 31.0 and 49.5 MB/s and host-to-card is 14.6 and 33.8 MB/s, and four parallel streams reach 172 MB/s in at least one round (the table in Chapter 8, §8.6). What is missing here is the A/B: add `writecombine=on` to the boot parameters, re-run the same set of commands, and compare the two sets of numbers. Note that an idle card drops into a PC6 deep sleep, and the wake-up cost for the first packet can run to hundreds of milliseconds, so measure each set at least twice before comparing — otherwise you will mistake a power-state transition for an attribute difference.

---

## 11.7 Functional Items: On-Hardware Work in Three Stages

Only once the three items above pass does function come into play.

### Stage 1: Scope A (groups A + B + C, 8,525 lines)

Actions: load `mic.ko`; use MPSS user space to complete initialization and boot.

The pass criteria are a set of concrete prints, all of which have a source in the code:

| What you see | From | Meaning |
|---|---|---|
| `mic0: Transition from state ready to booting` | `include/mic_common.h:700` | Booting begins |
| `/sys/class/mic/mic0/state` reads back `online` | `host/linsysfs.c:240`, `:243` | The card has come up normally |
| `/sys/class/mic/mic0/post_code`, `boot_count` | `host/linsysfs.c:432`, `:442` | Can be used to watch the card's self-test progress |
| `mic0: Transition from state ready to reset failed` | `include/mic_common.h:700`, `:273` | Boot failed |

The `state` attribute has exactly these ten values: `ready`, `booting`, `no response`, `boot failed`, `online`, `shutdown`, `lost`, `resetting`, `reset failed`, `invalid`.

### Stage 2: Scope B (add `linvcons`, `linvnet`, `vnet/*`)

Actions: first measure the question raised in 10.7 — **does the card's SCIF back off gracefully when it cannot find the host peer?** The way to do it is to build once without the SCIF-related objects and see whether the card's `mic0` network comes up as expected.

Pass criterion: a `mic0` network interface appears on the host, and the card side can obtain an address and pass traffic.

### Stage 3: Scope C (full)

Actions: add `micscif/`, `dma/`, `host/vhost/` and `host/vmcore.c` all back in.

Pass criteria: SCIF can establish a connection and complete one RMA transfer; the virtio block device can be mounted; and `vmcore` can export a usable dump after a card crash.

---

## 11.8 Criteria Summary

One table gathering every criterion above. **This table is the project's acceptance standard.**

| # | What is tested | How | Pass | Fail |
|---:|---|---|---|---|
| 1 | BAR0 size and position | `lspci -vvv` | 8 GiB, 64-bit, prefetchable, > 4 GiB | Stop |
| 2 | Whether BAR0 is actually allocated | `/proc/iomem`, dmesg | A contiguous 8 GiB `mic` region appears | See 11.9 |
| 3 | BAR4 | `lspci -vvv` | 128 KiB, 64-bit, non-prefetchable | Stop |
| 4 | Initial DMA mask values | A probe module loaded before `mic.ko` | All three values are 64-bit | See 11.9 |
| 5 | Page size | `/proc/config.gz` | 4 KB | F3 needs rewriting; not a disqualifier |
| 6 | No IOMMU | `/sys/class/iommu`, sysfs | Empty, no group | Must confirm the device is still declared coherent |
| 7 | Cache coherency | Byte-for-byte comparison of a data round trip | Exactly identical | Stop |
| 8 | Whether write-combining is in effect | **Boot with `writecombine=on` added first**, then compare write throughput between the two mappings | A difference exists | Re-estimate performance; does not block |
| 9 | Scope A | `micctrl` | `state` = `online` | See 11.9 |
| 10 | Whether card SCIF backs off gracefully | Build and run without `micscif` | The card's `mic0` still comes up | Scope B is void; full scope is required |
| 11 | Scope C | SCIF connection + RMA | Success | Work through the items one by one |

This table covers only the parts that can be decided **on hardware**. Of the 11 couplings in Chapter 5 (F1 through F7, H1 through H4), two further classes do not need a separate entry here:

| Who catches it | Which class | Result |
|---|---|---|
| The compiler (Gate 2) | F1's `boot_cpu_data.x86_model`, F6's `bsfq`/`btrq`, F7's `slow_virt_to_phys()` simply do not exist in the LoongArch kernel | A failed compile means it is caught; this belongs to the compile loop in Chapter 8, §8.3, and needs no separate criterion |
| The driver's own printk | H1's 64-bit DMA mask (`host/linux.c:274`–`278`), and `request_mem_region` for both BARs | It reports an error; go back to the table in 11.9 |
| Only this table can catch it | F3's PSMI page granularity (silently grows under 16 KB pages), H3/H4's write-combining (`wc_enabled` false silently takes the SUC path), F5's "no IOMMU" premise | **The silent class — exactly why 11.4 and 11.6 exist** |

In other words: not one item in this table is something "the compiler will find for me". The few the compiler can find all surface at Gate 2, and everything this table has to catch is the kind that reports nothing.

---

## 11.9 Whom to Suspect First When It Fails

Once something fails, the concrete print in `dmesg` has already pointed at the fault. Look it up in this table; there is no need to read the code from the top.

| Appears in dmesg | Module line | Real cause | Go back to |
|---|---|---|---|
| `mic 0: failed to reserve mmio space` | `host/linux.c:294` | BAR4 was not allocated by the firmware, or another driver holds it | 11.2 |
| `mic 0: failed to reserve aperture space` | `host/linux.c:301`–`303` | BAR0 **was not allocated** (`pci_resource_start`/`len` are 0, so `request_mem_region` can only try to claim address 0), or that region is already held by another driver. The driver requests the whole block using the actual `pci_resource_len` that was reported, so "the window is smaller than 8 GiB" by itself will not trigger this line | 11.2 |
| `mic 0: ERROR DMA not available` | `host/linux.c:276` | The streaming DMA mask was squeezed to 32 bits by the firmware | 11.3 |
| `mic 0: ERROR pci_set_consistent_dma_mask(64)` | `host/linux.c:281` | The coherent mask was squeezed to 32 bits (it then falls back to 32 bits and retries once) | 11.3 |
| `mic 0: failed to map aperture space` | `host/uos_download.c:1158`, `:1547` | The resource was allocated, but `ioremap` of 8 GiB failed | 11.2 |
| `mic0: Transition from state ready to reset failed` | `include/mic_common.h:700` | A downstream consequence of any of the rows above | Locate it with the first five rows |
| No `Transition from state` print at all | `include/mic_common.h:700` | The module never loaded successfully in the first place | Start again from 11.2 |

The last criterion is the most useful: **`mic_setstate()` prints a line on every single state transition**. So if the module has loaded successfully even once, `dmesg` must contain one. If there is not a single line, the problem lies before loading.
