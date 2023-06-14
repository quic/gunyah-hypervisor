// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypconstants.h>
#include <hypregisters.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <cpulocal.h>
#include <hyp_aspace.h>
#include <log.h>
#include <panic.h>
#include <partition.h>
#include <pgtable.h>
#include <platform_cpu.h>
#include <platform_ipi.h>
#include <platform_irq.h>
#include <preempt.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>
#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE
#include <vcpu.h>
#endif

#include <events/platform.h>

#include <asm/barrier.h>

#include "event_handlers.h"
#include "gicv3.h"
#include "gicv3_config.h"

#if defined(VERBOSE) && VERBOSE
#define GICV3_DEBUG 1
#else
#define GICV3_DEBUG 0
#endif

static_assert(!GICV3_HAS_ITS || GICV3_HAS_LPI,
	      "An ITS cannot be present without LPI support");
#if defined(GICV3_ENABLE_VPE)
static_assert(!GICV3_ENABLE_VPE || GICV3_HAS_VLPI,
	      "VPE support cannot be enabled unless VLPIs are implemented");
#endif
static_assert(!GICV3_HAS_VLPI || GICV3_HAS_ITS,
	      "VLPIs (GICv4) cannot be implemented without an ITS");
static_assert(!GICV3_HAS_VLPI_V4_1 || GICV3_HAS_VLPI,
	      "VPEs (GICv4.1) cannot be implemented without VLPIs (GICv4.0)");
static_assert(!GICV3_HAS_LPI || !GICV3_HAS_ITS || GICV3_HAS_VLPI_V4_1,
	      "LPIs only supported if ITS is absent or GICv4.1 is implemented");

#define GICD_ENABLE_GET_N(x) ((x) >> 5)
#define GIC_ENABLE_BIT(x)    (uint32_t)(util_bit((x)&31UL))

static gicd_t *gicd;
static gicr_t *mapped_gicrs[PLATFORM_GICR_COUNT];
#if GICV3_HAS_ITS
static gits_t *mapped_gitss[PLATFORM_GITS_COUNT];
#endif

// It is sometimes desirable to be able to inspect the state of GIC in the
// debugger from the secure world point of view. Expose the physical addresses
// of GICD, GICR and GITS so the debuggers can find them.
extern paddr_t platform_gicd_base;
paddr_t	       platform_gicd_base = PLATFORM_GIC_BASE;
extern paddr_t platform_gicrs_bases[PLATFORM_GICR_COUNT];
paddr_t	       platform_gicrs_bases[PLATFORM_GICR_COUNT];
#if GICV3_HAS_ITS
extern paddr_t platform_gits_base;
paddr_t	       platform_gits_base = PLATFORM_GITS_BASE;
#endif

// Several of the IRQ configuration registers can only be updated using writes
// that affect more than one IRQ at a time, notably GICD_ICFGR and GICD_ICLAR.
// This lock is used to make the read-modify-write sequences atomic.
static spinlock_t bitmap_update_lock;

// Guard between SPI set route and CPU poweroff SPI migration
static spinlock_t spi_route_lock;

#if GICV3_HAS_LPI
#if GICV3_HAS_VLPI_V4_1
// Currently we only need one LPI per VCPU with a VGIC attachment, to be used
// as a GICv4.1 default scheduling doorbell. We therefore allocate the minimum
// nonzero number of LPIs.
#define GIC_LPI_NUM 8192U
#else
#error define GIC_LPI_NUM
#endif
static_assert(util_is_p2(GIC_LPI_BASE + GIC_LPI_NUM) && (GIC_LPI_NUM > 0),
	      "Hard-coded max LPI count must be 8192 less than a power of two");

#define GIC_LPI_PROP_ALIGNMENT ((size_t)util_bit(GICR_PROPBASER_PA_SHIFT))
static gic_lpi_prop_t alignas(GIC_LPI_PROP_ALIGNMENT)
	gic_lpi_prop_table[GIC_LPI_NUM];

static GICR_PROPBASER_t gic_lpi_propbase;
#endif // GICV3_HAS_LPI

CPULOCAL_DECLARE_STATIC(gicr_cpu_t, gicr_cpu);

static GICD_CTLR_NS_t
gicd_wait_for_write(void)
{
	// Order the write we're waiting for before the loads in the poll
	atomic_device_fence(memory_order_seq_cst);

	GICD_CTLR_t ctlr = atomic_load_relaxed(&gicd->ctlr);

	while (GICD_CTLR_NS_get_RWP(&ctlr.ns) != 0U) {
		asm_yield();
		ctlr = atomic_load_relaxed(&gicd->ctlr);
	}

	// Order the successful load in the poll before anything afterwards
	atomic_device_fence(memory_order_acquire);

	return ctlr.ns;
}

static void
gicr_wait_for_write(gicr_t *gicr)
{
	// Order the write we're waiting for before the loads in the poll
	atomic_device_fence(memory_order_seq_cst);

	GICR_CTLR_t ctlr = atomic_load_relaxed(&gicr->rd.ctlr);

	while (GICR_CTLR_get_RWP(&ctlr) != 0U) {
		asm_yield();
		ctlr = atomic_load_relaxed(&gicr->rd.ctlr);
	}

	// Order the successful load in the poll before anything afterwards
	atomic_device_fence(memory_order_acquire);
}

#if GICV3_HAS_LPI && (!GICV3_HAS_ITS || GICV3_HAS_VLPI_V4_1)
static void
gicr_wait_for_sync(gicr_t *gicr)
{
	// Order the write we're waiting for before the loads in the poll
	atomic_device_fence(memory_order_seq_cst);

	GICR_SYNCR_t syncr = atomic_load_relaxed(&gicr->rd.syncr);

	while (GICR_SYNCR_get_Busy(&syncr) != 0U) {
		asm_yield();
		syncr = atomic_load_relaxed(&gicr->rd.syncr);
	}

	// Order the successful load in the poll before anything afterwards
	atomic_device_fence(memory_order_acquire);
}
#endif

static count_t gicv3_spi_max_cache;
#if GICV3_EXT_IRQS
static count_t gicv3_spi_ext_max_cache;
static count_t gicv3_ppi_ext_max_cache;
#endif
#if GICV3_HAS_LPI
static count_t gicv3_lpi_max_cache;
#endif

static_assert(PLATFORM_GICR_COUNT >= PLATFORM_MAX_CORES,
	      "There must be enough GICRs for all possible CPUs");

static error_t
gicv3_spi_set_route_internal(irq_t irq, GICD_IROUTER_t route);

static void
gicr_set_percpu(cpu_index_t cpu)
{
	MPIDR_EL1_t mpidr = platform_cpu_index_to_mpidr(cpu);
	uint8_t	    aff0  = MPIDR_EL1_get_Aff0(&mpidr);
	uint8_t	    aff1  = MPIDR_EL1_get_Aff1(&mpidr);
	uint8_t	    aff2  = MPIDR_EL1_get_Aff2(&mpidr);
	uint8_t	    aff3  = MPIDR_EL1_get_Aff3(&mpidr);

	size_t gicr_stride = util_bit(GICR_STRIDE_SHIFT); // 64k for v3, 128k
							  // for v4
	GICR_TYPER_t gicr_typer;
	gicr_t	    *gicr = NULL;
#if GICV3_HAS_ITS
	paddr_t gicr_phys = 0U;
#endif

	// Search for the redistributor that matches this affinity value. We
	// assume that the stride that separates all redistributors is the same.
	for (index_t i = 0U; i < PLATFORM_GICR_COUNT; i++) {
		gicr = mapped_gicrs[i];
		assert(gicr != NULL);

#if GICV3_HAS_ITS
		gicr_phys = platform_gicrs_bases[i];
#endif
		gicr_typer = atomic_load_relaxed(&gicr->rd.typer);

		if ((GICR_TYPER_get_Aff0(&gicr_typer) == aff0) &&
		    (GICR_TYPER_get_Aff1(&gicr_typer) == aff1) &&
		    (GICR_TYPER_get_Aff2(&gicr_typer) == aff2) &&
		    (GICR_TYPER_get_Aff3(&gicr_typer) == aff3)) {
			break;
		} else {
			gicr = (gicr_t *)((paddr_t)gicr + gicr_stride);
		}
		if (GICR_TYPER_get_Last(&gicr_typer)) {
			break;
		}
	}

	if ((gicr == NULL) || (GICR_TYPER_get_Aff0(&gicr_typer) != aff0) ||
	    (GICR_TYPER_get_Aff1(&gicr_typer) != aff1) ||
	    (GICR_TYPER_get_Aff2(&gicr_typer) != aff2) ||
	    (GICR_TYPER_get_Aff3(&gicr_typer) != aff3)) {
		panic("gicv3: Unable to find CPU's redistributor.");
	}

	CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr = gicr;

#if GICV3_HAS_ITS
	// Inform the ITS driver of this CPU's redistributor
	gicv3_its_init_cpu(cpu, gicr, gicr_phys,
			   GICR_TYPER_get_Processor_Num(&gicr_typer));
#endif // GICV3_HAS_ITS
}

count_t
gicv3_irq_max(void)
{
	count_t result = gicv3_spi_max_cache;

#if GICV3_EXT_IRQS
	if (gicv3_spi_ext_max_cache != 0U) {
		result = gicv3_spi_ext_max_cache;
	} else if (gicv3_ppi_ext_max_cache != 0U) {
		result = gicv3_ppi_ext_max_cache;
	} else {
		// No extended IRQs implemented
	}
#endif

#if GICV3_HAS_LPI
	if (gicv3_lpi_max_cache != 0U) {
		result = gicv3_lpi_max_cache;
	}
#endif

	return result;
}

gicv3_irq_type_t
gicv3_get_irq_type(irq_t irq)
{
	gicv3_irq_type_t type;

	if (irq < GIC_SGI_BASE + GIC_SGI_NUM) {
		type = GICV3_IRQ_TYPE_SGI;
	} else if ((irq >= GIC_PPI_BASE) &&
		   (irq < (GIC_PPI_BASE + GIC_PPI_NUM))) {
		type = GICV3_IRQ_TYPE_PPI;
	} else if ((irq >= GIC_SPI_BASE) &&
		   (irq < (GIC_SPI_BASE + GIC_SPI_NUM)) &&
		   (irq <= gicv3_spi_max_cache)) {
		type = GICV3_IRQ_TYPE_SPI;
	} else if ((irq >= GIC_SPECIAL_INTIDS_BASE) &&
		   (irq < (GIC_SPECIAL_INTIDS_BASE + GIC_SPECIAL_INTIDS_NUM))) {
		type = GICV3_IRQ_TYPE_SPECIAL;
#if GICV3_HAS_LPI
	} else if ((irq >= GIC_LPI_BASE) &&
		   (irq < (GIC_LPI_BASE + GIC_LPI_NUM)) &&
		   (irq <= gicv3_lpi_max_cache)) {
		type = GICV3_IRQ_TYPE_LPI;
#endif
#if GICV3_EXT_IRQS
	} else if ((irq >= GIC_PPI_EXT_BASE) &&
		   (irq < (GIC_PPI_EXT_BASE + GIC_PPI_EXT_NUM)) &&
		   (irq <= gicv3_ppi_ext_max_cache)) {
		type = GICV3_IRQ_TYPE_PPI_EXT;
	} else if ((irq >= GIC_SPI_EXT_BASE) &&
		   (irq < (GIC_SPI_EXT_BASE + GIC_SPI_EXT_NUM)) &&
		   (irq <= gicv3_spi_ext_max_cache)) {
		type = GICV3_IRQ_TYPE_SPI_EXT;
#endif
	} else {
		type = GICV3_IRQ_TYPE_RESERVED;
	}

	return type;
}

bool
gicv3_irq_is_percpu(irq_t irq)
{
	bool ret;

	switch (gicv3_get_irq_type(irq)) {
	case GICV3_IRQ_TYPE_SGI:
	case GICV3_IRQ_TYPE_PPI:
#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_PPI_EXT:
#endif
#if GICV3_HAS_LPI
	case GICV3_IRQ_TYPE_LPI:
		// LPIs are treated as percpu because we need to know which GICR
		// to operate on. Note that we don't support platforms with an
		// ITS unless they also have GICv4.1. If we did, we would have
		// to reverse translate the LPI number to an event and device
		// ID, and use them to queue an ITS command.
#endif
		ret = true;
		break;
	case GICV3_IRQ_TYPE_SPI:
#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_SPI_EXT:
#endif
	case GICV3_IRQ_TYPE_SPECIAL:
	case GICV3_IRQ_TYPE_RESERVED:
	default:
		ret = false;
		break;
	}

	return ret;
}

static bool
is_irq_reserved(irq_t irq)
{
	uint8_t ipriority;

	// Assume that all CPUs have the same set of reserved SGIs / PPIs,
	// so it doesn't matter which GICR we check.
	assert(irq <= gicv3_irq_max());
	cpu_index_t cpu	 = cpulocal_check_index(cpulocal_get_index_unsafe());
	gicr_t	   *gicr = CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr;

	switch (gicv3_get_irq_type(irq)) {
	case GICV3_IRQ_TYPE_SGI:
	case GICV3_IRQ_TYPE_PPI:
		ipriority = atomic_load_relaxed(&gicr->sgi.ipriorityr[irq]);
		break;
	case GICV3_IRQ_TYPE_SPI:
		ipriority = atomic_load_relaxed(&gicd->ipriorityr[irq]);
		break;
#if GICV3_HAS_LPI
	case GICV3_IRQ_TYPE_LPI:
		// All LPIs are in group 1 and are not reserved.
		ipriority = GIC_PRIORITY_DEFAULT;
		break;
#endif
#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_PPI_EXT:
		ipriority = atomic_load_relaxed(
			&gicr->sgi.ipriorityr_e[irq - GIC_PPI_EXT_BASE]);
		break;
	case GICV3_IRQ_TYPE_SPI_EXT:
		ipriority = atomic_load_relaxed(
			&gicd->ipriorityr_e[irq - GIC_SPI_EXT_BASE]);
		break;
#endif
	case GICV3_IRQ_TYPE_SPECIAL:
	case GICV3_IRQ_TYPE_RESERVED:
	default:
		// Always reserved.
		ipriority = 0U;
		break;
	}

	// All interrupts with priority zero are reserved.
	return (ipriority == 0U);
}

error_t
gicv3_irq_check(irq_t irq)
{
	error_t ret = OK;

	if (irq > gicv3_irq_max()) {
		ret = ERROR_ARGUMENT_INVALID;
	} else if (is_irq_reserved(irq)) {
		ret = ERROR_DENIED;
	} else {
		ret = OK;
	}

	return ret;
}

static bool
gicv3_spi_is_enabled(irq_t irq)
{
	uint32_t isenabler =
		atomic_load_relaxed(&gicd->isenabler[GICD_ENABLE_GET_N(irq)]);
	bool enabled = (isenabler & GIC_ENABLE_BIT(irq)) != 0U;

#if GICV3_EXT_IRQS
#error Check the extended IRQs
#endif

	return enabled;
}

static bool
gicv3_spi_get_route_cpu_affinity(const GICD_IROUTER_t *route, cpu_index_t *cpu)
{
	bool found = false;

	MPIDR_EL1_t mpidr = MPIDR_EL1_default();
	MPIDR_EL1_set_Aff0(&mpidr, GICD_IROUTER_get_Aff0(route));
	MPIDR_EL1_set_Aff1(&mpidr, GICD_IROUTER_get_Aff1(route));
	MPIDR_EL1_set_Aff2(&mpidr, GICD_IROUTER_get_Aff2(route));
	MPIDR_EL1_set_Aff3(&mpidr, GICD_IROUTER_get_Aff3(route));

	platform_mpidr_mapping_t mapping = platform_cpu_get_mpidr_mapping();

	if (platform_cpu_map_mpidr_valid(&mapping, mpidr)) {
		*cpu  = (cpu_index_t)platform_cpu_map_mpidr_to_index(&mapping,
								     mpidr);
		found = true;
	}

	return found;
}

static void
gicv3_spi_set_route_cpu_affinity(GICD_IROUTER_t *route, cpu_index_t cpu)
{
	assert(cpu < PLATFORM_MAX_CORES);

	MPIDR_EL1_t mpidr = platform_cpu_index_to_mpidr(cpu);
	GICD_IROUTER_set_IRM(route, 0);
	GICD_IROUTER_set_Aff0(route, MPIDR_EL1_get_Aff0(&mpidr));
	GICD_IROUTER_set_Aff1(route, MPIDR_EL1_get_Aff1(&mpidr));
	GICD_IROUTER_set_Aff2(route, MPIDR_EL1_get_Aff2(&mpidr));
	GICD_IROUTER_set_Aff3(route, MPIDR_EL1_get_Aff3(&mpidr));
}

#if GICV3_HAS_LPI || GICV3_HAS_ITS
static void
gicv3_boot_cold_init_lpis(GICD_TYPER_t typer, partition_t *hyp_partition,
			  GICR_TYPER_t gicr_typer)
{
	// Check that LPIs are actually supported, and determine the maximum
	// LPI number.
	bool	have_lpis = GICD_TYPER_get_LPIS(&typer);
	count_t idbits	  = GICD_TYPER_get_IDbits(&typer);
	count_t lpibits	  = GICD_TYPER_get_num_LPIs(&typer);
	if (!have_lpis || (idbits < 13U)) {
		gicv3_lpi_max_cache = 0U;
	} else if (lpibits == 0U) {
		gicv3_lpi_max_cache = (count_t)util_mask(idbits + 1U);
	} else {
		gicv3_lpi_max_cache =
			GIC_LPI_BASE + (count_t)util_mask(lpibits + 1U);
	}

	// Limit configured LPIs to the hard-coded maximum if the hardware
	// supports more than we need.
	gicv3_lpi_max_cache =
		util_min(gicv3_lpi_max_cache, GIC_LPI_BASE + GIC_LPI_NUM - 1U);

	if (gicv3_lpi_max_cache > 0U) {
		// Set all LPI property entries to disabled and default
		// priority.
		gic_lpi_prop_t lpi_prop = gic_lpi_prop_default();
		gic_lpi_prop_set_priority(&lpi_prop, GIC_PRIORITY_DEFAULT);
		for (count_t i = 0U; i < GIC_LPI_NUM; i++) {
			gic_lpi_prop_table[i] = lpi_prop;
		}

#if defined(GICV3_CACHE_INCOHERENT) && GICV3_CACHE_INCOHERENT
		CACHE_CLEAN_OBJECT(gic_lpi_prop_table);
#endif

		// Calculate the GICR_PROPBASE value that points to the config
		// table
		gic_lpi_propbase = GICR_PROPBASER_default();
		// IDbits: the number of bits required to represent all
		// configured LPIs, minus one
		GICR_PROPBASER_set_IDbits(
			&gic_lpi_propbase,
			32U - compiler_clz((uint32_t)gicv3_lpi_max_cache) - 1U);
		// InnerCache == 7: Inner write back, read + write alloc
		GICR_PROPBASER_set_InnerCache(&gic_lpi_propbase, 7U);
		// Shareability == 1: Inner shareable
		GICR_PROPBASER_set_Shareability(&gic_lpi_propbase, 1U);
		// PA: physical address of the LPI property table
		GICR_PROPBASER_set_PA(
			&gic_lpi_propbase,
			partition_virt_to_phys(hyp_partition,
					       (uintptr_t)&gic_lpi_prop_table));
		// OuterCache == 0: Inner and outer attributes are the same
		GICR_PROPBASER_set_OuterCache(&gic_lpi_propbase, 0U);

		// Allocate LPI pending bitmap and calculate GICR_PENDBASE per
		// CPU
		for (cpu_index_t i = 0U; i < PLATFORM_MAX_CORES; i++) {
			// One bit per IRQ number (including the first 8192
			// which are not LPIs); must be 64k-aligned and
			// zero-initialised. We allocate these from the heap
			// rather than BSS in the hope of not wasting the space
			// between them (~0.5MiB total)
			size_t lpi_bitmap_sz = (size_t)gicv3_lpi_max_cache / 8U;
			void_ptr_result_t alloc_r = partition_alloc(
				hyp_partition, lpi_bitmap_sz,
				(size_t)util_bit(GICR_PENDBASER_PA_SHIFT));
			if (alloc_r.e != OK) {
				panic("Unable to allocate physical LPI pending bitmap");
			}
			(void)memset_s(alloc_r.r, lpi_bitmap_sz, 0,
				       lpi_bitmap_sz);
			CPULOCAL_BY_INDEX(gicr_cpu, i).lpi_pending_bitmap =
				(register_t *)alloc_r.r;

			GICR_PENDBASER_t pendbase = GICR_PENDBASER_default();
			// InnerCache == 7: Inner write back, read + write alloc
			GICR_PENDBASER_set_InnerCache(&pendbase, 7U);
			// Shareability == 1: Inner shareable
			GICR_PENDBASER_set_Shareability(&pendbase, 1U);
			// PA: 64k-aligned physical address of the LPI pending
			// bitmap
			paddr_t lpi_bitmap_phys = partition_virt_to_phys(
				hyp_partition, (uintptr_t)alloc_r.r);
			assert(util_is_p2aligned(lpi_bitmap_phys,
						 GICR_PENDBASER_PA_SHIFT));
			GICR_PENDBASER_set_PA(&pendbase, lpi_bitmap_phys);
			// OuterCache == 0: Inner and outer attributes are the
			// same
			GICR_PENDBASER_set_OuterCache(&pendbase, 0U);
			// PTZ: table has been zeroed
			GICR_PENDBASER_set_PTZ(&pendbase, true);
			CPULOCAL_BY_INDEX(gicr_cpu, i).lpi_pendbase = pendbase;

#if defined(GICV3_CACHE_INCOHERENT) && GICV3_CACHE_INCOHERENT
			CACHE_CLEAN_RANGE(lpi_pending_bitmap, lpi_bitmap_sz);
#endif

#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE && GICV3_HAS_VLPI_V4_1
			spinlock_init(
				&CPULOCAL_BY_INDEX(gicr_cpu, i).vsgi_query_lock);
#endif
		}
	}

#if GICV3_HAS_ITS
	// Initialise the ITSs
	gicv3_its_init(&mapped_gitss,
		       (count_t)4U - (count_t)GICR_TYPER_get_CommonLPIAff(
					     &gicr_typer));
#endif // GICV3_HAS_ITS

#if GICV3_HAS_VLPI_V4_1
	// Check the supported vPE range
	GICD_TYPER2_t typer2   = atomic_load_relaxed(&gicd->typer2);
	count_t	      vpe_bits = GICD_TYPER2_get_VIL(&typer2)
					 ? 16U
					 : (GICD_TYPER2_get_VID(&typer2) + 1U);
	assert(GICV3_ITS_VPES < ((count_t)util_bit(vpe_bits)));
#endif // GICV3_HAS_VLPI_V4_1
}
#endif

#if GICV3_HAS_ITS
static void
gicv3_map_its(size_t gicr_size, size_t gits_size, partition_t *hyp_partition,
	      size_t gits_stride)
{
	// Map the ITS registers and calculate their virtual addresses.
	mapped_gitss[0] = (gits_t *)util_p2align_up(
		(uintptr_t)mapped_gicrs[0] + gicr_size, GITS_STRIDE_SHIFT);

	// Note: we are assuming here that the ITSs are physically contiguous,
	// which is a reasonable configuration but is not actually required by
	// the spec. This is not yet a significant issue because we have no
	// platform with multiple ITSs.
	pgtable_hyp_start();
	error_t ret = pgtable_hyp_map(hyp_partition, (uintptr_t)mapped_gitss[0],
				      gits_size, platform_gits_base,
				      PGTABLE_HYP_MEMTYPE_NOSPEC_NOCOMBINE,
				      PGTABLE_ACCESS_RW,
				      VMSA_SHAREABILITY_NON_SHAREABLE);
	if (ret != OK) {
		panic("gicv3: Mapping of ITSs failed.");
	}
	pgtable_hyp_commit();
#if (PLATFORM_GITS_COUNT > 1U)
	for (cpu_index_t i = 1U; i < PLATFORM_GITS_COUNT; i++) {
		mapped_gitss[i] = (gits_t *)((uintptr_t)mapped_gitss[i - 1U] +
					     gits_stride);
	}
#else
	(void)gits_stride;
#endif
}
#endif

static void
gicv3_map_gicd_and_gicrs(size_t gicr_size, partition_t *hyp_partition,
			 size_t gicd_size, virt_range_result_t range)
{
	paddr_t gicr_base   = PLATFORM_GICR_BASE;
	size_t	gicr_stride = (size_t)util_bit(GICR_STRIDE_SHIFT);

	pgtable_hyp_start();

	// Map the distributor
	gicd	    = (gicd_t *)range.r.base;
	error_t ret = pgtable_hyp_map(hyp_partition, (uintptr_t)gicd, gicd_size,
				      platform_gicd_base,
				      PGTABLE_HYP_MEMTYPE_NOSPEC_NOCOMBINE,
				      PGTABLE_ACCESS_RW,
				      VMSA_SHAREABILITY_NON_SHAREABLE);
	if (ret != OK) {
		panic("gicv3: Mapping of distributor failed.");
	}

	// Map the redistributors and calculate their addresses
	platform_gicrs_bases[0] = gicr_base;
	mapped_gicrs[0] =
		(gicr_t *)(range.r.base +
			   util_p2align_up(gicd_size, GICR_STRIDE_SHIFT));
	ret = pgtable_hyp_map(hyp_partition, (uintptr_t)mapped_gicrs[0],
			      gicr_size, platform_gicrs_bases[0],
			      PGTABLE_HYP_MEMTYPE_NOSPEC_NOCOMBINE,
			      PGTABLE_ACCESS_RW,
			      VMSA_SHAREABILITY_NON_SHAREABLE);
	if (ret != OK) {
		panic("gicv3: Mapping of redistributors failed.");
	}

	pgtable_hyp_commit();

	for (index_t i = 1; i < PLATFORM_GICR_COUNT; i++) {
		mapped_gicrs[i] = (gicr_t *)((uintptr_t)mapped_gicrs[i - 1U] +
					     gicr_stride);
		platform_gicrs_bases[i] =
			platform_gicrs_bases[i - 1U] + gicr_stride;

		// Ensure that DPG1NS is set, so the GICD does not try to route
		// 1-of-N interrupts to this GICR before we have set it up (when
		// the CPU first powers on), assuming that TZ has enabled E1NWF.
		// It is implementation defined whether this affects CPUs that
		// are sleeping, but it does work for GIC-600 and GIC-700.
		GICR_CTLR_t gicr_ctlr =
			atomic_load_relaxed(&mapped_gicrs[i]->rd.ctlr);
		GICR_CTLR_set_DPG1NS(&gicr_ctlr, true);
		atomic_store_relaxed(&mapped_gicrs[i]->rd.ctlr, gicr_ctlr);
	}
}

// In boot_cold we map the distributor and all the redistributors, based on
// their base addresses and sizes read from the device tree. We then initialize
// the distributor.
void
gicv3_handle_boot_cold_init(cpu_index_t cpu)
{
	partition_t *hyp_partition = partition_get_private();

	// FIXME: remove when read from device tree
	size_t gicr_size = PLATFORM_GICR_COUNT * util_bit(GICR_STRIDE_SHIFT);
	size_t gicd_size = 0x10000U; // GICD is always 64K

	static_assert(PLATFORM_GICR_SIZE == PLATFORM_GICR_COUNT
						    << GICR_STRIDE_SHIFT,
		      "bad PLATFORM_GICR_SIZE");

#if GICV3_HAS_ITS
	size_t gits_stride = (size_t)util_bit(GITS_STRIDE_SHIFT);
	size_t gits_size   = (size_t)PLATFORM_GITS_COUNT << GITS_STRIDE_SHIFT;
#else
	size_t gits_size = 0U;
#endif

	virt_range_result_t range = hyp_aspace_allocate(
		util_p2align_up(gicd_size, GICR_STRIDE_SHIFT) + gicr_size +
		gits_size);
	if (range.e != OK) {
		panic("gicv3: Address allocation failed.");
	}

	gicv3_map_gicd_and_gicrs(gicr_size, hyp_partition, gicd_size, range);

#if GICV3_HAS_ITS
	gicv3_map_its(gicr_size, gits_size, hyp_partition, gits_stride);
#endif

	// Disable the distributor
	atomic_store_relaxed(&gicd->ctlr,
			     (GICD_CTLR_t){ .ns = GICD_CTLR_NS_default() });
	GICD_CTLR_NS_t ctlr = gicd_wait_for_write();

#if GICV3_HAS_SECURITY_DISABLED
	// If security disabled set all interrupts to group 1
	GICD_CTLR_t ctlr_ds = atomic_load_relaxed(&(gicd->ctlr));
	assert(GICD_CTLR_DS_get_DS(&ctlr_ds.ds));

	for (index_t i = 0; i < util_array_size(gicd->igroupr); i++) {
		atomic_store_relaxed(&gicd->igroupr[i], 0xffffffff);
	}
#if GICV3_EXT_IRQS
	for (index_t i = 0; i < util_array_size(gicd->igroupr_e); i++) {
		atomic_store_relaxed(&gicd->igroupr_e[i], 0xffffffff);
	}
#endif
#endif

	// Calculate the number of supported IRQs
	GICD_TYPER_t typer = atomic_load_relaxed(&gicd->typer);

	count_t lines	    = GICD_TYPER_get_ITLinesNumber(&typer);
	gicv3_spi_max_cache = util_min(GIC_SPI_BASE + GIC_SPI_NUM - 1U,
				       (32U * (lines + 1U)) - 1U);

#if GICV3_EXT_IRQS || GICV3_HAS_ITS
	// Pick an arbitrary GICR to probe extended PPI and common LPI affinity
	// (we assume that these are the same across all GICRs)
	gicr_t	    *gicr	= mapped_gicrs[0];
	GICR_TYPER_t gicr_typer = atomic_load_relaxed(&gicr->rd.typer);
#endif

#if GICV3_EXT_IRQS
	bool espi = GICD_TYPER_get_ESPI(&typer);

	if (espi) {
		count_t espi_range = GICD_TYPER_get_ESPI_range(&typer);
		gicv3_spi_ext_max_cache =
			GIC_SPI_EXT_BASE - 1U + (32U * (espi_range + 1U));
	} else {
		gicv3_spi_ext_max_cache = 0U;
	}

	GICR_TYPER_PPInum_t eppi = GICR_TYPER_get_PPInum(&gicr_typer);

	switch (eppi) {
	case GICR_TYPER_PPINUM_MAX_1087:
		gicv3_ppi_ext_max_cache = 1087U;
		break;
	case GICR_TYPER_PPINUM_MAX_1119:
		gicv3_ppi_ext_max_cache = 1119U;
		break;
	case GICR_TYPER_PPINUM_MAX_31:
	default:
		gicv3_ppi_ext_max_cache = 0U;
		break;
	}
#endif

#if GICV3_HAS_1N
	assert(!GICD_TYPER_get_No1N(&typer));
#endif

	// Enable non-secure state affinity routing
	GICD_CTLR_NS_set_ARE_NS(&ctlr, true);

	atomic_store_relaxed(&gicd->ctlr, (GICD_CTLR_t){ .ns = ctlr });
	ctlr = gicd_wait_for_write();

	// Configure all SPIs to the default priority
	for (irq_t i = GIC_SPI_BASE; i < (GIC_SPI_BASE + GIC_SPI_NUM); i++) {
		atomic_store_relaxed(&gicd->ipriorityr[i],
				     GIC_PRIORITY_DEFAULT);
	}

#if GICV3_EXT_IRQS
	// Configure all extended SPIs to the default priority
	for (irq_t i = 0; i < GIC_SPI_EXT_NUM; i++) {
		atomic_store_relaxed(&gicd->ipriorityr_e[i],
				     GIC_PRIORITY_DEFAULT);
	}
#endif

#if GICV3_HAS_LPI
	gicv3_boot_cold_init_lpis(typer, hyp_partition, gicr_typer);
#endif // GICV3_HAS_LPI

	// Route all SPIs to the boot CPU by default.
	MPIDR_EL1_t    mpidr   = platform_cpu_index_to_mpidr(cpu);
	uint8_t	       aff0    = MPIDR_EL1_get_Aff0(&mpidr);
	uint8_t	       aff1    = MPIDR_EL1_get_Aff1(&mpidr);
	uint8_t	       aff2    = MPIDR_EL1_get_Aff2(&mpidr);
	uint8_t	       aff3    = MPIDR_EL1_get_Aff3(&mpidr);
	GICD_IROUTER_t irouter = GICD_IROUTER_default();
	GICD_IROUTER_set_IRM(&irouter, false);
	GICD_IROUTER_set_Aff0(&irouter, aff0);
	GICD_IROUTER_set_Aff1(&irouter, aff1);
	GICD_IROUTER_set_Aff2(&irouter, aff2);
	GICD_IROUTER_set_Aff3(&irouter, aff3);

	for (irq_t i = 0; i < GIC_SPI_NUM; i++) {
		atomic_store_relaxed(&gicd->irouter[i], irouter);
	}
#if GICV3_EXT_IRQS
	for (irq_t i = 0; i < GIC_SPI_EXT_NUM; i++) {
		atomic_store_relaxed(&gicd->irouter_e[i], irouter);
	}
#endif

	// Enable Affinity Group 1 interrupts
	GICD_CTLR_NS_set_EnableGrp1A(&ctlr, true);
	atomic_store_relaxed(&gicd->ctlr, (GICD_CTLR_t){ .ns = ctlr });

	// Disable forwarding of the all SPIs interrupts
	// First 32 bits (index 0) correspond to SGIs and PPIs, which are now
	// handled in the redistributor. Therefore, we start from index 1.
	for (index_t i = 1; i < util_array_size(gicd->icenabler); i++) {
		atomic_store_relaxed(&gicd->icenabler[i], 0xffffffff);
	}

#if GICV3_EXT_IRQS
	for (index_t i = 0; i < util_array_size(gicd->icenabler_e); i++) {
		atomic_store_relaxed(&gicd->icenabler_e[i], 0xffffffff);
	}
#endif
	(void)gicd_wait_for_write();

	// Set up the cached SGIRs used for IPIs targeting each CPU
	for (cpu_index_t i = 0U; i < PLATFORM_MAX_CORES; i++) {
		mpidr = platform_cpu_index_to_mpidr(i);

		aff0 = MPIDR_EL1_get_Aff0(&mpidr);
		aff1 = MPIDR_EL1_get_Aff1(&mpidr);
		aff2 = MPIDR_EL1_get_Aff2(&mpidr);
		aff3 = MPIDR_EL1_get_Aff3(&mpidr);

		ICC_SGIR_EL1_t icc_sgi1r = ICC_SGIR_EL1_default();
		ICC_SGIR_EL1_set_TargetList(&icc_sgi1r,
					    (uint16_t)util_bit(aff0 % 16U));
		ICC_SGIR_EL1_set_RS(&icc_sgi1r, aff0 / 16U);
		ICC_SGIR_EL1_set_Aff1(&icc_sgi1r, aff1);
		ICC_SGIR_EL1_set_Aff2(&icc_sgi1r, aff2);
		ICC_SGIR_EL1_set_Aff3(&icc_sgi1r, aff3);

		CPULOCAL_BY_INDEX(gicr_cpu, i).icc_sgi1r = icc_sgi1r;
	}

	// Set up gicr for each CPU
	for (cpu_index_t i = 0U; i < PLATFORM_MAX_CORES; i++) {
		if (platform_cpu_exists(i)) {
			gicr_set_percpu(i);

			// Set online state for interrupt migration at poweroff
			// Set boot cpu as online. Secondary CPUs will be set
			// by 'power_cpu_online' handler
			gicr_cpu_t *gc = &CPULOCAL_BY_INDEX(gicr_cpu, i);
			gc->online     = (i == cpu);
		}
	}

#if GICV3_HAS_GICD_ICLAR
	// The physical GIC implements IRQ classes for 1-of-N IRQs. The virtual
	// GIC will expose these to VMs through an implementation-defined
	// register of its own (GICD_SETCLASSR).
	GICD_IIDR_t iidr = atomic_load_relaxed(&gicd->iidr);
	// Implementer must be ARM (JEP106 code: [0x4] 0x3b)
	assert(GICD_IIDR_get_Implementer(&iidr) == 0x43bU);
	// Product ID must be 2 (GIC-600) or 4 (GIC-700)
	assert((GICD_IIDR_get_ProductID(&iidr) == 2U) ||
	       (GICD_IIDR_get_ProductID(&iidr) == 4U));
#endif

	spinlock_init(&bitmap_update_lock);
	spinlock_init(&spi_route_lock);
}

// In the boot_cpu_cold we search for the redistributor that corresponds to the
// current cpu by comparing the affinity values.
void
gicv3_handle_boot_cpu_cold_init(cpu_index_t cpu)
{
	gicr_t *gicr = CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr;

	// Configure all interrupts to the default priority
	for (irq_t i = GIC_SGI_BASE; i < GIC_PPI_BASE; i++) {
		atomic_store_relaxed(&gicr->sgi.ipriorityr[i],
				     GIC_PRIORITY_DEFAULT);
	}
	for (irq_t i = GIC_PPI_BASE; i < GIC_SPI_BASE; i++) {
		atomic_store_relaxed(&gicr->sgi.ipriorityr[i],
				     GIC_PRIORITY_DEFAULT);
	}

#if GICV3_EXT_IRQS
	for (irq_t i = 0; i < GIC_PPI_EXT_NUM; i++) {
		atomic_store_relaxed(&gicr->sgi.ipriorityr_e[i],
				     GIC_PRIORITY_DEFAULT);
	}
#endif

#if GICV3_HAS_SECURITY_DISABLED
	// If security disabled set all interrupts to group 1
	GICD_CTLR_t ctlr_ds = atomic_load_relaxed(&(gicd->ctlr));
	assert(GICD_CTLR_DS_get_DS(&ctlr_ds.ds));

	atomic_store_relaxed(&gicr->sgi.igroupr0, 0xffffffff);
#if GICV3_EXT_IRQS
	atomic_store_relaxed(&gicr->sgi.igroupr_e, 0xffffffff);
#endif

	GICR_WAKER_t waker = GICR_WAKER_default();
	GICR_WAKER_set_ProcessorSleep(&waker, false);
	atomic_store_relaxed(&gicr->rd.waker, waker);

	// Wait for gicr to be on
	GICR_WAKER_t waker_read;
	do {
		waker_read = atomic_load_acquire(&gicr->rd.waker);
	} while (GICR_WAKER_get_ChildrenAsleep(&waker_read) != 0U);
#endif

	// Disable all local IRQs
	atomic_store_relaxed(&gicr->sgi.icenabler0, 0xffffffff);
#if GICV3_EXT_IRQS
	for (index_t i = 0; i < util_array_size(gicr->sgi.icenabler_e); i++) {
		atomic_store_relaxed(&gicr->sgi.icenabler_e, 0xffffffff);
	}
#endif
	gicr_wait_for_write(gicr);

	GICR_CTLR_t ctlr = atomic_load_relaxed(&gicr->rd.ctlr);

	// If LPIs have already been enabled, we can't set the base registers
	// if we have LPI support, and we (possibly) can't disable them if we
	// don't support them.
	assert(!GICR_CTLR_get_Enable_LPIs(&ctlr));

	// Enable 1-of-N targeting to this CPU
	GICR_CTLR_set_DPG1NS(&ctlr, false);
	atomic_store_relaxed(&gicr->rd.ctlr, ctlr);

#if GICV3_HAS_LPI
	GICR_TYPER_t typer = atomic_load_relaxed(&gicr->rd.typer);

	assert(GICR_TYPER_get_PLPIS(&typer));

#if !GICV3_HAS_ITS
	// Direct LPIs must be supported if there is no ITS; otherwise there
	// would be no way to generate LPIs.
	//
	// Note that three of the five registers covered by this feature,
	// INVLPIR, INVALLR and SYNCR, are also mandatory in GICv4.1; however,
	// SETLPIR and CLRLPIR are not, so this bit might not be set by a
	// GICv4.1 implementation. In GICv4.1, a separate ID bit is added to
	// indicate support for those three registers; see below.
	assert(GICR_TYPER_get_DirectLPI(&typer));
#endif // !GICV3_HAS_ITS

#if GICV3_HAS_VLPI_V4_1
	// GICv4.1 requires the GICR to use the vPE-format VPENDBASER, and to
	// support the invalidate registers (a subset of DirectLPI), VSGI
	// delivery, and polling for completion of vPE scheduling.
	assert(GICR_TYPER_get_RVPEID(&typer) && GICR_TYPER_get_VSGI(&typer) &&
	       GICR_TYPER_get_Dirty(&typer) && GICR_CTLR_get_IR(&ctlr));
#elif GICV3_HAS_VLPI
	// GICv4.0 requires the GICR not to use the vPE-format VPENDBASER.
	assert(!GICR_TYPER_get_RVPEID(&typer));
#endif

	if (gicv3_lpi_max_cache > 0U) {
		// Set the base registers
		atomic_store_relaxed(&gicr->rd.propbaser, gic_lpi_propbase);
		GICR_PENDBASER_t *pendbase =
			&CPULOCAL_BY_INDEX(gicr_cpu, cpu).lpi_pendbase;
		atomic_store_relaxed(&gicr->rd.pendbaser, *pendbase);

		// Read back pendbaser to assert that shareability is nonzero
		// (i.e. accesses to the table are cache-coherent) and also to
		// clear PTZ in case we need to rewrite it
		*pendbase = atomic_load_relaxed(&gicr->rd.pendbaser);
		assert(GICR_PENDBASER_get_Shareability(pendbase) != 0U);

		// Enable LPIs (note: this may be permanent until reset)
		GICR_CTLR_set_Enable_LPIs(&ctlr, true);
		atomic_store_release(&gicr->rd.ctlr, ctlr);
	}

#endif // GICV3_HAS_LPI

#if PLATFORM_IPI_LINES > ENUM_IPI_REASON_MAX_VALUE
	// Enable the SGIs used by IPIs
	atomic_store_release(&gicr->sgi.isenabler0,
			     util_mask(ENUM_IPI_REASON_MAX_VALUE + 1U));
#else
	// Enable the shared SGI for all IPIs
	atomic_store_release(&gicr->sgi.isenabler0, 0x1);
#endif
}

// Redistributor control register initialization
void
gicv3_handle_boot_cpu_warm_init(void)
{
	asm_ordering_dummy_t gic_init_order;

#if GICV3_HAS_LPI
	GICR_CTLR_t ctlr =
		atomic_load_relaxed(&CPULOCAL(gicr_cpu).gicr->rd.ctlr);

	// LPIs should have already been enabled
	assert(GICR_CTLR_get_Enable_LPIs(&ctlr) == (gicv3_lpi_max_cache != 0U));
#endif

	// Enable system register access and disable FIQ and IRQ bypass
	ICC_SRE_EL2_t icc_sre = ICC_SRE_EL2_default();
	// Trap EL1 accesses to ICC_SRE_EL1
	ICC_SRE_EL2_set_Enable(&icc_sre, false);
	// Disable IRQ and FIQ bypass
	ICC_SRE_EL2_set_DIB(&icc_sre, true);
	ICC_SRE_EL2_set_DFB(&icc_sre, true);
	// Enable system register accesses
	ICC_SRE_EL2_set_SRE(&icc_sre, true);
	register_ICC_SRE_EL2_write_ordered(icc_sre, &gic_init_order);
	asm_context_sync_ordered(&gic_init_order);

	// Configure PMR to allow all interrupt priorities
	ICC_PMR_EL1_t icc_pmr = ICC_PMR_EL1_default();
	ICC_PMR_EL1_set_Priority(&icc_pmr, 0xff);
	register_ICC_PMR_EL1_write_ordered(icc_pmr, &gic_init_order);

	// Set EOImode to 1, so we can drop priority before delivery to VMs
	ICC_CTLR_EL1_t icc_ctrl = register_ICC_CTLR_EL1_read();
	ICC_CTLR_EL1_set_EOImode(&icc_ctrl, true);
	register_ICC_CTLR_EL1_write_ordered(icc_ctrl, &gic_init_order);

	// Enable group 1 interrupts
	ICC_IGRPEN_EL1_t icc_grpen1 = ICC_IGRPEN_EL1_default();
	ICC_IGRPEN_EL1_set_Enable(&icc_grpen1, true);
	register_ICC_IGRPEN1_EL1_write_ordered(icc_grpen1, &gic_init_order);
	asm_context_sync_ordered(&gic_init_order);

#if GICV3_DEBUG
	gicr_t *gicr = CPULOCAL(gicr_cpu).gicr;
	TRACE_LOCAL(
		DEBUG, INFO,
		"gicv3 cpu warm init, en {:#x} act {:#x} grp {:#x} hpp {:#x}",
		atomic_load_relaxed(&gicr->sgi.isenabler0),
		atomic_load_relaxed(&gicr->sgi.isactiver0),
		atomic_load_relaxed(&gicr->sgi.igroupr0),
		ICC_HPPIR_EL1_raw(register_ICC_HPPIR1_EL1_read()));
#endif
}

error_t
gicv3_handle_power_cpu_suspend(void)
{
	// Disable group 1 interrupts
	ICC_IGRPEN_EL1_t icc_grpen1 = ICC_IGRPEN_EL1_default();
	ICC_IGRPEN_EL1_set_Enable(&icc_grpen1, false);
	register_ICC_IGRPEN1_EL1_write_ordered(icc_grpen1, &asm_ordering);

#if GICV3_DEBUG || GICV3_HAS_SECURITY_DISABLED
	gicr_t *gicr = CPULOCAL(gicr_cpu).gicr;
#endif
#if GICV3_DEBUG
	TRACE_LOCAL(DEBUG, INFO,
		    "gicv3 cpu suspend, en {:#x} act {:#x} grp {:#x} hpp {:#x}",
		    atomic_load_relaxed(&gicr->sgi.isenabler0),
		    atomic_load_relaxed(&gicr->sgi.isactiver0),
		    atomic_load_relaxed(&gicr->sgi.igroupr0),
		    ICC_HPPIR_EL1_raw(register_ICC_HPPIR1_EL1_read()));
#endif

#if GICV3_HAS_SECURITY_DISABLED
	// Ensure that the IGRPEN1_EL1 write has completed
	__asm__ volatile("isb; dsb sy;" ::: "memory");

	// Set ProcessorSleep, so that the redistributor hands over ownership
	// of any pending interrupts before it powers off
	GICR_WAKER_t waker = GICR_WAKER_default();
	GICR_WAKER_set_ProcessorSleep(&waker, true);
	atomic_store_relaxed(&gicr->rd.waker, waker);
#endif

#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE
	// Ensure that the GICR has finished descheduling the last vPE.
	gicv3_vpe_sync_deschedule(cpulocal_get_index(), false);
#endif

#if GICV3_HAS_SECURITY_DISABLED
	// Wait for gicr to be off
	GICR_WAKER_t waker_read;
	do {
		waker_read = atomic_load_acquire(&gicr->rd.waker);
	} while (GICR_WAKER_get_ChildrenAsleep(&waker_read) == 0U);
#endif

	return OK;
}

void
gicv3_handle_power_cpu_resume(void)
{
	asm_ordering_dummy_t gic_enable_order;

	// Enable group 1 interrupts
	ICC_IGRPEN_EL1_t icc_grpen1 = ICC_IGRPEN_EL1_default();
	ICC_IGRPEN_EL1_set_Enable(&icc_grpen1, true);
	register_ICC_IGRPEN1_EL1_write_ordered(icc_grpen1, &gic_enable_order);
	asm_context_sync_ordered(&gic_enable_order);

#if GICV3_DEBUG || GICV3_HAS_SECURITY_DISABLED
	gicr_t *gicr = CPULOCAL(gicr_cpu).gicr;
#endif
#if GICV3_DEBUG
	TRACE_LOCAL(DEBUG, INFO,
		    "gicv3 cpu resume, en {:#x} act {:#x} grp {:#x} hpp {:#x}",
		    atomic_load_relaxed(&gicr->sgi.isenabler0),
		    atomic_load_relaxed(&gicr->sgi.isactiver0),
		    atomic_load_relaxed(&gicr->sgi.igroupr0),
		    ICC_HPPIR_EL1_raw(register_ICC_HPPIR1_EL1_read()));
#endif

#if GICV3_HAS_SECURITY_DISABLED
	// Clear ProcessorSleep, so that it can start handling interrupts.
	GICR_WAKER_t waker = GICR_WAKER_default();
	GICR_WAKER_set_ProcessorSleep(&waker, false);
	atomic_store_relaxed(&gicr->rd.waker, waker);

	// Wait for gicr to be on
	GICR_WAKER_t waker_read;
	do {
		waker_read = atomic_load_acquire(&gicr->rd.waker);
	} while (GICR_WAKER_get_ChildrenAsleep(&waker_read) != 0U);
#endif
}

void
gicv3_irq_enable(irq_t irq)
{
	assert(irq <= gicv3_irq_max());

	gicv3_irq_type_t irq_type = gicv3_get_irq_type(irq);

	switch (irq_type) {
	case GICV3_IRQ_TYPE_SPI:
		// Ensure the route is still valid if we enable the irq for the
		// first time

		// Take the SPI lock so we can fetch the current route
		// If it has a specific target CPU (not 1:N) ensure it is online
		spinlock_acquire(&spi_route_lock);

		if (!gicv3_spi_is_enabled(irq)) {
			GICD_IROUTER_t route =
				atomic_load_relaxed(&gicd->irouter[irq]);
			if (!GICD_IROUTER_get_IRM(&route)) {
				cpu_index_t target;
				gicr_cpu_t *gc = NULL;
				bool valid = gicv3_spi_get_route_cpu_affinity(
					&route, &target);
				if (valid) {
					gc = &CPULOCAL_BY_INDEX(gicr_cpu,
								target);
				}
				// Set affinity to this CPU if needed
				if (!valid || !gc->online) {
					gc = &CPULOCAL(gicr_cpu);
					assert(gc->online);
					gicv3_spi_set_route_cpu_affinity(
						&route, cpulocal_get_index());
					(void)gicv3_spi_set_route_internal(
						irq, route);
				}
			}
		}

		atomic_store_release(&gicd->isenabler[GICD_ENABLE_GET_N(irq)],
				     GIC_ENABLE_BIT(irq));

		spinlock_release(&spi_route_lock);

		break;

#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_SPI_EXT:
		// Extended SPI
#error TODO check route is valid

		atomic_store_release(&gicd->isenabler_e[GICD_ENABLE_GET_N(
					     irq - GIC_SPI_EXT_BASE)],
				     GIC_ENABLE_BIT(irq - GIC_SPI_EXT_BASE));
		break;
#endif
#if GICV3_HAS_LPI
	case GICV3_IRQ_TYPE_LPI:
#endif
	case GICV3_IRQ_TYPE_SGI:
	case GICV3_IRQ_TYPE_PPI:
#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_PPI_EXT:
#endif
	case GICV3_IRQ_TYPE_SPECIAL:
	case GICV3_IRQ_TYPE_RESERVED:
	default:
		panic("Incorrect IRQ type");
	}
}

void
gicv3_irq_enable_percpu(irq_t irq, cpu_index_t cpu)
{
	assert(irq <= gicv3_irq_max());

	gicr_t		*gicr	  = CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr;
	gicv3_irq_type_t irq_type = gicv3_get_irq_type(irq);

	switch (irq_type) {
	case GICV3_IRQ_TYPE_SGI:
	case GICV3_IRQ_TYPE_PPI: {
		atomic_store_release(&gicr->sgi.isenabler0,
				     GIC_ENABLE_BIT(irq));
		break;
	}

#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_PPI_EXT: {
		// Extended PPI
		atomic_store_release(&gicr->sgi.isenabler_e[GICD_ENABLE_GET_N(
					     irq - GIC_PPI_EXT_BASE)],
				     GIC_ENABLE_BIT(irq - GIC_PPI_EXT_BASE));
		break;
	}
#endif
#if GICV3_HAS_LPI
	case GICV3_IRQ_TYPE_LPI:
		gic_lpi_prop_set_enable(&gic_lpi_prop_table[irq - GIC_LPI_BASE],
					true);
		gicv3_lpi_inv_by_id(cpu, irq);
		break;
#endif
	case GICV3_IRQ_TYPE_SPI:
#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_SPI_EXT:
#endif
	case GICV3_IRQ_TYPE_SPECIAL:
	case GICV3_IRQ_TYPE_RESERVED:
	default:
		panic("Incorrect IRQ type");
	}
}

void
gicv3_irq_enable_local(irq_t irq)
{
	assert_cpulocal_safe();
	gicv3_irq_enable_percpu(irq, cpulocal_get_index());
}

void
gicv3_irq_disable(irq_t irq)
{
	assert(irq <= gicv3_irq_max());

	gicv3_irq_type_t irq_type = gicv3_get_irq_type(irq);

	switch (irq_type) {
	case GICV3_IRQ_TYPE_SPI:
		atomic_store_relaxed(&gicd->icenabler[GICD_ENABLE_GET_N(irq)],
				     GIC_ENABLE_BIT(irq));
		(void)gicd_wait_for_write();
		break;

#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_SPI_EXT: {
		// Extended SPI
		atomic_store_relaxed(&gicd->icenabler_e[GICD_ENABLE_GET_N(
					     irq - GIC_SPI_EXT_BASE)],
				     GIC_ENABLE_BIT(irq - GIC_SPI_EXT_BASE));
		(void)gicd_wait_for_write();
		break;
	}
#endif
#if GICV3_HAS_LPI
	case GICV3_IRQ_TYPE_LPI:
#endif
	case GICV3_IRQ_TYPE_SGI:
	case GICV3_IRQ_TYPE_PPI:
#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_PPI_EXT:
#endif
	case GICV3_IRQ_TYPE_SPECIAL:
	case GICV3_IRQ_TYPE_RESERVED:
	default:
		panic("Incorrect IRQ type");
	}
}

void
gicv3_irq_cancel_nowait(irq_t irq)
{
	assert(irq <= gicv3_irq_max());

	gicv3_irq_type_t irq_type = gicv3_get_irq_type(irq);

	switch (irq_type) {
	case GICV3_IRQ_TYPE_SPI:
		atomic_store_relaxed(&gicd->icpendr[GICD_ENABLE_GET_N(irq)],
				     GIC_ENABLE_BIT(irq));
		// The spec does not give us any way to wait for this to
		// complete, hence the nowait() in the name. There is also no
		// guarantee of timely completion.
		break;

#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_SPI_EXT: {
		// Extended SPI
		atomic_store_relaxed(&gicd->icpendr_e[GICD_ENABLE_GET_N(
					     irq - GIC_SPI_EXT_BASE)],
				     GIC_ENABLE_BIT(irq - GIC_SPI_EXT_BASE));
		// As above, there is no way to guarantee completion.
		break;
	}
#endif
#if GICV3_HAS_LPI
	case GICV3_IRQ_TYPE_LPI:
#endif
	case GICV3_IRQ_TYPE_SGI:
	case GICV3_IRQ_TYPE_PPI:
#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_PPI_EXT:
#endif
	case GICV3_IRQ_TYPE_SPECIAL:
	case GICV3_IRQ_TYPE_RESERVED:
	default:
		panic("Incorrect IRQ type");
	}
}

static void
gicv3_irq_disable_percpu_nowait(irq_t irq, cpu_index_t cpu)
{
	assert(irq <= gicv3_irq_max());

	gicr_t		*gicr	  = CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr;
	gicv3_irq_type_t irq_type = gicv3_get_irq_type(irq);

	switch (irq_type) {
	case GICV3_IRQ_TYPE_SGI:
	case GICV3_IRQ_TYPE_PPI: {
		atomic_store_relaxed(&gicr->sgi.icenabler0,
				     GIC_ENABLE_BIT(irq));
		break;
	}
#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_PPI_EXT: {
		// Extended PPI
		atomic_store_relaxed(&gicr->sgi.icenabler_e[GICD_ENABLE_GET_N(
					     irq - GIC_PPI_EXT_BASE)],
				     GIC_ENABLE_BIT(irq - GIC_PPI_EXT_BASE));
		break;
	}
#endif
#if GICV3_HAS_LPI
	case GICV3_IRQ_TYPE_LPI:
		gic_lpi_prop_set_enable(&gic_lpi_prop_table[irq - GIC_LPI_BASE],
					false);
		gicv3_lpi_inv_by_id(cpu, irq);
		break;
#endif
	case GICV3_IRQ_TYPE_SPI:
#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_SPI_EXT:
#endif
	case GICV3_IRQ_TYPE_SPECIAL:
	case GICV3_IRQ_TYPE_RESERVED:
	default:
		panic("Incorrect IRQ type");
	}
}

void
gicv3_irq_disable_percpu(irq_t irq, cpu_index_t cpu)
{
	gicv3_irq_disable_percpu_nowait(irq, cpu);
#if GICV3_HAS_LPI
	if (gicv3_get_irq_type(irq) == GICV3_IRQ_TYPE_LPI) {
		gicr_wait_for_sync(CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr);
	} else {
		gicr_wait_for_write(CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr);
	}
#else
	gicr_wait_for_write(CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr);
#endif
}

void
gicv3_irq_disable_local(irq_t irq)
{
	assert_cpulocal_safe();
	gicv3_irq_disable_percpu(irq, cpulocal_get_index());
}

void
gicv3_irq_disable_local_nowait(irq_t irq)
{
	assert_cpulocal_safe();
	gicv3_irq_disable_percpu_nowait(irq, cpulocal_get_index());
}

irq_trigger_result_t
gicv3_irq_set_trigger_percpu(irq_t irq, irq_trigger_t trigger, cpu_index_t cpu)
{
	irq_trigger_result_t ret;

	gicr_t		*gicr	  = CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr;
	gicv3_irq_type_t irq_type = gicv3_get_irq_type(irq);

	// We do not support this behavior for now
	if (trigger == IRQ_TRIGGER_MESSAGE) {
		ret = irq_trigger_result_error(ERROR_ARGUMENT_INVALID);
		goto end_function;
	}

	spinlock_acquire(&bitmap_update_lock);

	switch (irq_type) {
	case GICV3_IRQ_TYPE_PPI: {
		uint32_t isenabler0 =
			atomic_load_relaxed(&gicr->sgi.isenabler0);
		bool enabled = (isenabler0 & GIC_ENABLE_BIT(irq)) != 0U;

		if (enabled) {
			gicv3_irq_disable_percpu(irq, cpu);
		}

		register_t icfg =
			(register_t)atomic_load_relaxed(&gicr->sgi.icfgr[1]);

		if ((trigger == IRQ_TRIGGER_LEVEL_HIGH) ||
		    (trigger == IRQ_TRIGGER_LEVEL_LOW)) {
			bitmap_clear(&icfg, ((irq % 16U) * 2U) + 1U);
		} else {
			bitmap_set(&icfg, ((irq % 16U) * 2U) + 1U);
		}

		atomic_store_relaxed(&gicr->sgi.icfgr[1], (uint32_t)icfg);

		if (enabled) {
			gicv3_irq_enable_percpu(irq, cpu);
		}

		// Read back the value in case it could not be changed
		icfg = atomic_load_relaxed(&gicr->sgi.icfgr[1]);
		ret  = irq_trigger_result_ok(
			 bitmap_isset(&icfg, ((irq % 16U) * 2U) + 1U)
				 ? IRQ_TRIGGER_EDGE_RISING
				 : IRQ_TRIGGER_LEVEL_HIGH);

		break;
	}

#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_PPI_EXT: {
		// Extended PPI
		uint32_t isenabler_e = atomic_load_relaxed(
			gicr->sgi.isenabler_e[GICD_ENABLE_GET_N(
				irq - GIC_PPI_EXT_BASE)]);
		bool enabled = (isenabler_e &
				GIC_ENABLE_BIT(irq - GIC_PPI_EXT_BASE)) != 0U;

		if (enabled) {
			gicv3_irq_disable_percpu(irq, cpu);
		}

		register_t icfg = (register_t)atomic_load_relaxed(
			&gicr->sgi.icfgr_e[(irq - GIC_PPI_EXT_BASE) / 16]);

		if ((trigger == IRQ_TRIGGER_LEVEL_HIGH) ||
		    (trigger == IRQ_TRIGGER_LEVEL_LOW)) {
			bitmap_clear(&icfg, ((irq % 16U) * 2U) + 1U);
		} else {
			bitmap_set(&icfg, ((irq % 16U) * 2U) + 1U);
		}

		atomic_store_relaxed(
			&gicr->sgi.icfgr_e[(irq - GIC_PPI_EXT_BASE) / 16],
			(uint32_t)icfg);

		if (enabled) {
			gicv3_irq_enable_percpu(irq);
		}

		// Read back the value in case it could not be changed
		icfg = (register_t)atomic_load_relaxed(
			&gicr->sgi.icfgr_e[(irq - GIC_PPI_EXT_BASE) / 16]);
		ret = irq_trigger_result_ok(
			bitmap_isset(&icfg, ((irq % 16U) * 2U) + 1U)
				? IRQ_TRIGGER_EDGE_RISING
				: IRQ_TRIGGER_LEVEL_HIGH);

		break;
	}
#endif

	case GICV3_IRQ_TYPE_SGI:
	case GICV3_IRQ_TYPE_SPI:
#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_SPI_EXT:
#endif
#if GICV3_HAS_LPI
	case GICV3_IRQ_TYPE_LPI:
#endif
	case GICV3_IRQ_TYPE_SPECIAL:
	case GICV3_IRQ_TYPE_RESERVED:
	default:
		// No action required as irq is not handled.
		ret = irq_trigger_result_error(ERROR_UNIMPLEMENTED);
		break;
	}

	spinlock_release(&bitmap_update_lock);

end_function:
	return ret;
}

irq_trigger_result_t
gicv3_irq_set_trigger(irq_t irq, irq_trigger_t trigger)
{
	irq_trigger_result_t ret;

	assert(irq <= gicv3_irq_max());

	gicv3_irq_type_t irq_type = gicv3_get_irq_type(irq);

	spinlock_acquire(&bitmap_update_lock);

	switch (irq_type) {
	case GICV3_IRQ_TYPE_SGI:
		// SGIs only support edge-triggered behavior
		ret = irq_trigger_result_ok(IRQ_TRIGGER_EDGE_RISING);
		break;

	case GICV3_IRQ_TYPE_PPI: {
		gicr_t *gicr = CPULOCAL(gicr_cpu).gicr;

		uint32_t isenabler0 =
			atomic_load_relaxed(&gicr->sgi.isenabler0);
		bool enabled = (isenabler0 & GIC_ENABLE_BIT(irq)) != 0U;

		if (enabled) {
			gicv3_irq_disable(irq);
		}

		register_t icfg =
			(register_t)atomic_load_relaxed(&gicr->sgi.icfgr[1]);

		if ((trigger == IRQ_TRIGGER_LEVEL_HIGH) ||
		    (trigger == IRQ_TRIGGER_LEVEL_LOW)) {
			bitmap_clear(&icfg, ((irq % 16U) * 2U) + 1U);
		} else {
			bitmap_set(&icfg, ((irq % 16U) * 2U) + 1U);
		}

		atomic_store_relaxed(&gicr->sgi.icfgr[1], (uint32_t)icfg);

		if (enabled) {
			gicv3_irq_enable(irq);
		}

		// Read back the value in case it could not be changed
		icfg = atomic_load_relaxed(&gicr->sgi.icfgr[1]);
		ret  = irq_trigger_result_ok(
			 bitmap_isset(&icfg, ((irq % 16U) * 2U) + 1U)
				 ? IRQ_TRIGGER_EDGE_RISING
				 : IRQ_TRIGGER_LEVEL_HIGH);

		break;
	}

	case GICV3_IRQ_TYPE_SPI: {
		// Disable the interrupt if it is already enabled
		uint32_t isenabler = atomic_load_relaxed(
			&gicd->isenabler[GICD_ENABLE_GET_N(irq)]);
		bool enabled = (isenabler & GIC_ENABLE_BIT(irq)) != 0U;

		if (enabled) {
			gicv3_irq_disable(irq);
		}

		register_t icfg =
			(register_t)atomic_load_relaxed(&gicd->icfgr[irq / 16]);

		if ((trigger == IRQ_TRIGGER_LEVEL_HIGH) ||
		    (trigger == IRQ_TRIGGER_LEVEL_LOW)) {
			bitmap_clear(&icfg, ((irq % 16U) * 2U) + 1U);
		} else {
			bitmap_set(&icfg, ((irq % 16U) * 2U) + 1U);
		}

		atomic_store_relaxed(&gicd->icfgr[irq / 16], (uint32_t)icfg);

		if (enabled) {
			gicv3_irq_enable(irq);
		}

		// Read back the value in case it could not be changed
		icfg = atomic_load_relaxed(&gicd->icfgr[irq / 16]);
		ret  = irq_trigger_result_ok(
			 bitmap_isset(&icfg, ((irq % 16U) * 2U) + 1U)
				 ? IRQ_TRIGGER_EDGE_RISING
				 : IRQ_TRIGGER_LEVEL_HIGH);

		break;
	}

#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_PPI_EXT: {
		// Extended PPI
		gicr_t *gicr = CPULOCAL(gicr_cpu).gicr;

		uint32_t isenabler_e = atomic_load_relaxed(
			gicr->sgi.isenabler_e[GICD_ENABLE_GET_N(
				irq - GIC_PPI_EXT_BASE)]);
		bool enabled = (isenabler_e &
				GIC_ENABLE_BIT(irq - GIC_PPI_EXT_BASE)) != 0U;

		if (enabled) {
			gicv3_irq_disable(irq);
		}

		register_t icfg = (register_t)atomic_load_relaxed(
			&gicr->sgi.icfgr_e[(irq - GIC_PPI_EXT_BASE) / 16]);

		if ((trigger == IRQ_TRIGGER_LEVEL_HIGH) ||
		    (trigger == IRQ_TRIGGER_LEVEL_LOW)) {
			bitmap_clear(&icfg, ((irq % 16U) * 2U) + 1U);
		} else {
			bitmap_set(&icfg, ((irq % 16U) * 2U) + 1U);
		}

		atomic_store_relaxed(
			&gicr->sgi.icfgr_e[(irq - GIC_PPI_EXT_BASE) / 16],
			(uint32_t)icfg);

		if (enabled) {
			gicv3_irq_enable(irq);
		}

		// Read back the value in case it could not be changed
		icfg = (register_t)atomic_load_relaxed(
			&gicr->sgi.icfgr_e[(irq - GIC_PPI_EXT_BASE) / 16]);
		ret = irq_trigger_result_ok(
			bitmap_isset(&icfg, ((irq % 16U) * 2U) + 1U)
				? IRQ_TRIGGER_EDGE_RISING
				: IRQ_TRIGGER_LEVEL_HIGH);

		break;
	}

	case GICV3_IRQ_TYPE_SPI_EXT: {
		// Extended SPI

		// Disable the interrupt if it is already enabled
		uint32_t isenabler = atomic_load_relaxed(
			&gicd->isenabler_e[GICD_ENABLE_GET_N(
				irq - GIC_SPI_EXT_BASE)]);
		bool enabled = (isenabler &
				GIC_ENABLE_BIT(irq - GIC_SPI_EXT_BASE)) != 0U;

		if (enabled) {
			gicv3_irq_disable(irq);
		}

		register_t icfg = (register_t)atomic_load_relaxed(
			&gicd->icfgr_e[(irq - GIC_SPI_EXT_BASE) / 16U]);

		if ((trigger == IRQ_TRIGGER_LEVEL_HIGH) ||
		    (trigger == IRQ_TRIGGER_LEVEL_LOW)) {
			bitmap_clear(&icfg, ((irq % 16U) * 2U) + 1U);
		} else {
			bitmap_set(&icfg, ((irq % 16U) * 2U) + 1U);
		}

		atomic_store_relaxed(
			&gicd->icfgr[(irq - GIC_SPI_EXT_BASE) / 16U],
			(uint32_t)icfg);

		if (enabled) {
			gicv3_irq_enable(irq);
		}

		// Read back the value in case it could not be changed
		icfg = (register_t)atomic_load_relaxed(
			&gicd->icfgr_e[GICD_ENABLE_GET_N(irq -
							 GIC_SPI_EXT_BASE)]);
		ret = irq_trigger_result_ok(
			bitmap_isset(&icfg, ((irq % 16U) * 2U) + 1U)
				? IRQ_TRIGGER_EDGE_RISING
				: IRQ_TRIGGER_LEVEL_HIGH);

		break;
	}
#endif
#if GICV3_HAS_LPI
	case GICV3_IRQ_TYPE_LPI:
		// LPIs are always message-signalled
		ret = irq_trigger_result_ok(IRQ_TRIGGER_MESSAGE);
		break;
#endif
	case GICV3_IRQ_TYPE_SPECIAL:
	case GICV3_IRQ_TYPE_RESERVED:
	default:
		// No action required as irq is not handled.
		ret = irq_trigger_result_error(ERROR_UNIMPLEMENTED);
		break;
	}

	spinlock_release(&bitmap_update_lock);

	return ret;
}

static error_t
gicv3_spi_set_route_internal(irq_t irq, GICD_IROUTER_t route)
	REQUIRE_SPINLOCK(spi_route_lock)
{
	error_t ret;

	assert_preempt_disabled();

	switch (gicv3_get_irq_type(irq)) {
	case GICV3_IRQ_TYPE_SPI:
		atomic_store_relaxed(&gicd->irouter[irq - GIC_SPI_BASE], route);
		ret = OK;
		break;
#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_SPI_EXT:
		atomic_store_relaxed(&gicd->irouter_e[irq - GIC_SPI_EXT_BASE],
				     route);
		ret = OK;
		break;
#endif
	case GICV3_IRQ_TYPE_SGI:
	case GICV3_IRQ_TYPE_PPI:
#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_PPI_EXT:
#endif
#if GICV3_HAS_LPI
	case GICV3_IRQ_TYPE_LPI:
#endif
	case GICV3_IRQ_TYPE_SPECIAL:
	case GICV3_IRQ_TYPE_RESERVED:
	default:
		ret = ERROR_ARGUMENT_INVALID;
		break;
	}

	return ret;
}

error_t
gicv3_spi_set_route(irq_t irq, GICD_IROUTER_t route)
{
	error_t ret = ERROR_ARGUMENT_INVALID;

	spinlock_acquire(&spi_route_lock);

	// If the SPI is enabled and routes to a specific CPU we need to check
	// it is online. The route is also checked when the SPI is enabled.
	// If using 1:N routing, the GIC decides which CPU should get it.
	if (!GICD_IROUTER_get_IRM(&route)) {
		cpu_index_t cpu;

		// Determine the target CPU for the route
		if (!gicv3_spi_get_route_cpu_affinity(&route, &cpu)) {
			goto out;
		}

		// If the interrupt is enabled check the target CPU is online
		if (gicv3_spi_is_enabled(irq)) {
			gicr_cpu_t *gc = &CPULOCAL_BY_INDEX(gicr_cpu, cpu);

			// If the target CPU is offline adjust the route
			if (!gc->online) {
				gc = &CPULOCAL(gicr_cpu);
				assert(gc->online);
				gicv3_spi_set_route_cpu_affinity(
					&route, cpulocal_get_index());
			}
		}
	}

	ret = gicv3_spi_set_route_internal(irq, route);

out:
	spinlock_release(&spi_route_lock);
	return ret;
}

#if GICV3_HAS_GICD_ICLAR
error_t
gicv3_spi_set_classes(irq_t irq, bool class0, bool class1)
{
	error_t ret;

	spinlock_acquire(&bitmap_update_lock);

	switch (gicv3_get_irq_type(irq)) {
	case GICV3_IRQ_TYPE_SPI: {
		register_t iclar =
			(register_t)atomic_load_relaxed(&gicd->iclar[irq / 16]);

		if (class0) {
			bitmap_clear(&iclar, (irq % 16U) * 2U);
		} else {
			bitmap_set(&iclar, (irq % 16U) * 2U);
		}

		if (class1) {
			bitmap_clear(&iclar, ((irq % 16U) * 2U) + 1U);
		} else {
			bitmap_set(&iclar, ((irq % 16U) * 2U) + 1U);
		}

		// This must be a store-release to ensure that it takes effect
		// after any preceding write to GICD_IROUTER<irq>, since these
		// bits are RAZ/WI until GICD_IROUTER<irq>.IRM is set.
		atomic_store_release(&gicd->iclar[irq / 16], (uint32_t)iclar);

		ret = OK;
		break;
	}
#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_SPI_EXT: {
		register_t iclar = (register_t)atomic_load_relaxed(
			&gicd->iclar_e[(irq - GIC_SPI_EXT_BASE) / 16]);

		if (class0) {
			bitmap_clear(&iclar, (irq % 16U) * 2U);
		} else {
			bitmap_set(&iclar, (irq % 16U) * 2U);
		}

		if (class1) {
			bitmap_clear(&iclar, ((irq % 16U) * 2U) + 1U);
		} else {
			bitmap_set(&iclar, ((irq % 16U) * 2U) + 1U);
		}

		// This must be a store-release to ensure that it takes effect
		// after any preceding write to GICD_IROUTER<irq>E, since these
		// bits are RAZ/WI until GICD_IROUTER<irq>E.IRM is set.
		atomic_store_release(
			&gicd->iclar_e[(irq - GIC_SPI_EXT_BASE) / 16],
			(uint32_t)iclar);

		ret = OK;
		break;
	}
#endif
	case GICV3_IRQ_TYPE_SGI:
	case GICV3_IRQ_TYPE_PPI:
#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_PPI_EXT:
#endif
#if GICV3_HAS_LPI
	case GICV3_IRQ_TYPE_LPI:
#endif
	case GICV3_IRQ_TYPE_SPECIAL:
	case GICV3_IRQ_TYPE_RESERVED:
	default:
		ret = ERROR_ARGUMENT_INVALID;
		break;
	}

	spinlock_release(&bitmap_update_lock);

	return ret;
}
#endif

irq_result_t
gicv3_irq_acknowledge(void)
{
	irq_result_t ret = { 0 };

	ICC_IAR_EL1_t iar =
		register_ICC_IAR1_EL1_read_volatile_ordered(&asm_ordering);

	uint32_t intid = ICC_IAR_EL1_get_INTID(&iar);

	// 1023 is returned if there is no pending interrupt with sufficient
	// priority for it to be signaled to the PE, or if the highest priority
	// pending interrupt is not appropriate for the current security state
	// or interrupt group that is associated with the System register.
	if (intid == 1023U) {
		ret.e = ERROR_IDLE;
		goto error;
	}

	// Ensure distributor has activated the interrupt before prio drop
	__asm__ volatile("isb; dsb sy" : "+m"(asm_ordering));

	if (gicv3_get_irq_type(intid) == GICV3_IRQ_TYPE_SGI) {
		gicv3_irq_priority_drop(intid);
#if PLATFORM_IPI_LINES > ENUM_IPI_REASON_MAX_VALUE
		assert(intid <= ENUM_IPI_REASON_MAX_VALUE);
		trigger_platform_ipi_event((ipi_reason_t)intid);
#else
		(void)trigger_platform_ipi_event();
#endif
		gicv3_irq_deactivate(intid);
		ret.e = ERROR_RETRY;
	} else {
		ret.e = OK;
		ret.r = intid;
	}

error:
	return ret;
}

void
gicv3_irq_priority_drop(irq_t irq)
{
	assert(irq <= gicv3_irq_max());

	ICC_EOIR_EL1_t eoir = ICC_EOIR_EL1_default();

	ICC_EOIR_EL1_set_INTID(&eoir, irq);

	// No need for a barrier here: nothing we do to handle this IRQ
	// before the priority drop will affect whether we get a different
	// IRQ after the drop.

	register_ICC_EOIR1_EL1_write_ordered(eoir, &asm_ordering);
}

void
gicv3_irq_deactivate(irq_t irq)
{
	assert(irq <= gicv3_irq_max());

#if GICV3_HAS_LPI
	if (irq >= GIC_LPI_BASE) {
		// Deactivation is meaningless for LPIs (apart from the priority
		// drop which is done separately), so skip the barriers and
		// register write
	} else
#endif
	{
		ICC_DIR_EL1_t dir = ICC_DIR_EL1_default();

		ICC_DIR_EL1_set_INTID(&dir, irq);

		// Ensure interrupt handling is complete
		__asm__ volatile("dsb sy; isb" ::: "memory");

		register_ICC_DIR_EL1_write_ordered(dir, &asm_ordering);
	}
}

void
gicv3_irq_deactivate_percpu(irq_t irq, cpu_index_t cpu)
{
	gicr_t *gicr = CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr;

	if (gicr == NULL) {
		LOG(DEBUG, INFO, "gicr is NULL for cpu(%u):\n", cpu);
		goto out;
	}

	switch (gicv3_get_irq_type(irq)) {
	case GICV3_IRQ_TYPE_SGI:
	case GICV3_IRQ_TYPE_PPI: {
		atomic_store_relaxed(&gicr->sgi.icactiver0,
				     GIC_ENABLE_BIT(irq));
		break;
	}
#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_PPI_EXT: {
		// Extended PPI
		atomic_store_relaxed(&gicr->sgi.icactiver_e[GICD_ENABLE_GET_N(
					     irq - GIC_PPI_EXT_BASE)],
				     GIC_ENABLE_BIT(irq - GIC_PPI_EXT_BASE));
		break;
	}
#endif
	case GICV3_IRQ_TYPE_SPI:
#if GICV3_EXT_IRQS
	case GICV3_IRQ_TYPE_SPI_EXT:
#endif
#if GICV3_HAS_LPI
	case GICV3_IRQ_TYPE_LPI:
#endif
	case GICV3_IRQ_TYPE_SPECIAL:
	case GICV3_IRQ_TYPE_RESERVED:
	default:
		panic("Incorrect IRQ type");
	}

out:
	return;
}

#if PLATFORM_IPI_LINES > ENUM_IPI_REASON_MAX_VALUE
void
platform_ipi_others(ipi_reason_t ipi)
{
	ICC_SGIR_EL1_t sgir = ICC_SGIR_EL1_default();
	ICC_SGIR_EL1_set_IRM(&sgir, true);
	ICC_SGIR_EL1_set_INTID(&sgir, (irq_t)ipi);

	__asm__ volatile("dsb sy; isb" ::: "memory");

	register_ICC_SGI1R_EL1_write_ordered(sgir, &asm_ordering);
}

void
platform_ipi_one(ipi_reason_t ipi, cpu_index_t cpu)
{
	assert((ipi < GIC_SGI_NUM) && cpulocal_index_valid(cpu));

	ICC_SGIR_EL1_t sgir = CPULOCAL_BY_INDEX(gicr_cpu, cpu).icc_sgi1r;
	ICC_SGIR_EL1_set_INTID(&sgir, (irq_t)ipi);

	__asm__ volatile("dsb sy; isb" ::: "memory");

	register_ICC_SGI1R_EL1_write_ordered(sgir, &asm_ordering);
}

void
platform_ipi_clear(ipi_reason_t ipi)
{
	(void)ipi;
}

void
platform_ipi_mask(ipi_reason_t ipi)
{
	gicv3_irq_disable((irq_t)ipi);
}

void
platform_ipi_unmask(ipi_reason_t ipi)
{
	gicv3_irq_enable((irq_t)ipi);
}
#else
void
platform_ipi_others(void)
{
	ICC_SGIR_EL1_t sgir = ICC_SGIR_EL1_default();
	ICC_SGIR_EL1_set_IRM(&sgir, true);
	ICC_SGIR_EL1_set_INTID(&sgir, 0U);

	__asm__ volatile("dsb sy; isb" ::: "memory");

	register_ICC_SGI1R_EL1_write_ordered(sgir, &asm_ordering);
}

void
platform_ipi_one(cpu_index_t cpu)
{
	assert(cpulocal_index_valid(cpu));

	ICC_SGIR_EL1_t sgir = CPULOCAL_BY_INDEX(gicr_cpu, cpu).icc_sgi1r;
	ICC_SGIR_EL1_set_INTID(&sgir, 0U);

	__asm__ volatile("dsb sy; isb" ::: "memory");

	register_ICC_SGI1R_EL1_write_ordered(sgir, &asm_ordering);
}
#endif

#if defined(INTERFACE_VCPU) && INTERFACE_VCPU && GICV3_HAS_1N

error_t
gicv3_handle_vcpu_poweron(thread_t *vcpu)
{
	if (vcpu_option_flags_get_hlos_vm(&vcpu->vcpu_options)) {
		cpu_index_t cpu = scheduler_get_affinity(vcpu);

		// Enable 1-of-N targeting to the VCPU's physical CPU.
		//
		// No locking, because we assume that the hlos_vm flag is only
		// set on one VCPU per physical CPU. Also we are assuming here
		// that DPGs are implemented.
		gicr_t	   *gicr      = CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr;
		GICR_CTLR_t gicr_ctlr = atomic_load_relaxed(&gicr->rd.ctlr);
		GICR_CTLR_set_DPG1NS(&gicr_ctlr, false);
		atomic_store_relaxed(&gicr->rd.ctlr, gicr_ctlr);
	}

	return OK;
}

error_t
gicv3_handle_vcpu_poweroff(thread_t *vcpu)
{
	if (vcpu_option_flags_get_hlos_vm(&vcpu->vcpu_options)) {
		cpu_index_t cpu = scheduler_get_affinity(vcpu);

		// Disable 1-of-N targeting to the VCPU's physical CPU.
		//
		// No locking, because we assume that the hlos_vm flag is only
		// set on one VCPU per physical CPU. Also we are assuming here
		// that DPGs are implemented.
		gicr_t	   *gicr      = CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr;
		GICR_CTLR_t gicr_ctlr = atomic_load_relaxed(&gicr->rd.ctlr);
		GICR_CTLR_set_DPG1NS(&gicr_ctlr, true);
		atomic_store_relaxed(&gicr->rd.ctlr, gicr_ctlr);
	}

	return OK;
}

#endif // INTERFACE_VCPU && GICV3_HAS_1N

#if GICV3_HAS_LPI
const irq_t platform_irq_msi_base = GIC_LPI_BASE;

irq_t
platform_irq_msi_max(void)
{
	return gicv3_lpi_max_cache;
}

#if !GICV3_HAS_ITS || GICV3_HAS_VLPI_V4_1
void
gicv3_lpi_inv_by_id(cpu_index_t cpu, irq_t lpi)
{
	gicr_t *gicr = CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr;

	GICR_INVLPIR_t invlpir = GICR_INVLPIR_default();
	GICR_INVLPIR_set_pINTID(&invlpir, lpi);
	atomic_store_release(&gicr->rd.invlpir, invlpir);
}

// This is an ITS function for GICv3 with ITS or GICv4.0, and a GICR function
// for GICv3 without ITS or GICv4.1. See gicv3_its.c for the ITS version.
void
gicv3_lpi_inv_all(cpu_index_t cpu)
{
	gicr_t *gicr = CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr;

	GICR_INVALLR_t invallr = GICR_INVALLR_default();
	atomic_store_release(&gicr->rd.invallr, invallr);
}

// This is an ITS function for GICv3 with ITS or GICv4.0, and a GICR function
// for GICv3 without ITS or GICv4.1. See gicv3_its.c for the ITS version.
bool
gicv3_lpi_inv_pending(cpu_index_t cpu)
{
	gicr_t	    *gicr  = CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr;
	GICR_SYNCR_t syncr = atomic_load_acquire(&gicr->rd.syncr);
	return GICR_SYNCR_get_Busy(&syncr);
}
#endif // !GICV3_HAS_ITS || GICV3_HAS_VLPI_V4_1

#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE

#if GICV3_HAS_VLPI_V4_1
void
gicv3_vlpi_inv_by_id(thread_t *vcpu, virq_t vlpi)
{
	scheduler_lock(vcpu);
	if ((vcpu->gicv3_its_doorbell != NULL) &&
	    (vcpu->scheduler_affinity < PLATFORM_MAX_CORES)) {
		gicr_t *gicr =
			CPULOCAL_BY_INDEX(gicr_cpu, vcpu->scheduler_affinity)
				.gicr;

		GICR_INVLPIR_t invlpir = GICR_INVLPIR_default();
		GICR_INVLPIR_set_V(&invlpir, true);
		GICR_INVLPIR_set_vPEID(&invlpir, vcpu->gicv3_its_vpe_id);
		GICR_INVLPIR_set_pINTID(&invlpir, vlpi);
		atomic_store_release(&gicr->rd.invlpir, invlpir);
	}
	scheduler_unlock(vcpu);
}

void
gicv3_vlpi_inv_all(thread_t *vcpu)
{
	scheduler_lock(vcpu);
	if ((vcpu->gicv3_its_doorbell != NULL) &&
	    (vcpu->scheduler_affinity < PLATFORM_MAX_CORES)) {
		gicr_t *gicr =
			CPULOCAL_BY_INDEX(gicr_cpu, vcpu->scheduler_affinity)
				.gicr;

		GICR_INVALLR_t invallr = GICR_INVALLR_default();
		GICR_INVALLR_set_V(&invallr, true);
		GICR_INVALLR_set_vPEID(&invallr, vcpu->gicv3_its_vpe_id);
		atomic_store_release(&gicr->rd.invallr, invallr);
	}
	scheduler_unlock(vcpu);
}

bool
gicv3_vlpi_inv_pending(thread_t *vcpu)
{
	bool busy = false;

	scheduler_lock(vcpu);
	if ((vcpu->gicv3_its_doorbell != NULL) &&
	    (vcpu->scheduler_affinity < PLATFORM_MAX_CORES)) {
		gicr_t *gicr =
			CPULOCAL_BY_INDEX(gicr_cpu, vcpu->scheduler_affinity)
				.gicr;
		GICR_SYNCR_t syncr = atomic_load_acquire(&gicr->rd.syncr);
		busy		   = GICR_SYNCR_get_Busy(&syncr);
	}
	scheduler_unlock(vcpu);

	return busy;
}

void
gicv3_vpe_schedule(bool enable_group0, bool enable_group1)
{
	// Current thread must be a mapped VCPU
	thread_t *current = thread_get_self();
	assert(current != NULL);
	assert_preempt_disabled();

	if (cpulocal_index_valid(current->gicv3_its_mapped_cpu)) {
		assert(cpulocal_get_index() == current->gicv3_its_mapped_cpu);
		gicr_t *gicr = CPULOCAL(gicr_cpu).gicr;
		assert(gicr != NULL);

		// Wait until the GICR has finished descheduling any previous
		// vPE. Note that we only wait in deschedule if we're enabling
		// the doorbell.
		gicv3_vpe_sync_deschedule(cpulocal_get_index(), false);

		// Write the new valid VPENDBASER
		GICR_VPENDBASER_t vpendbaser = GICR_VPENDBASER_default();
		GICR_VPENDBASER_set_vPEID(&vpendbaser,
					  current->gicv3_its_vpe_id);
		GICR_VPENDBASER_set_vGrp1En(&vpendbaser, enable_group1);
		GICR_VPENDBASER_set_vGrp0En(&vpendbaser, enable_group0);
		// Note: PendingLast is RES1 when setting Valid
		GICR_VPENDBASER_set_PendingLast(&vpendbaser, true);
		GICR_VPENDBASER_set_Valid(&vpendbaser, true);
		atomic_store_relaxed(&gicr->vlpi.vpendbaser, vpendbaser);

		TRACE(DEBUG, INFO,
		      "gicv3_vpe_schedule: {:#x} -> vpendbase {:#x}",
		      (uintptr_t)current, GICR_VPENDBASER_raw(vpendbaser));
	}
}

bool
gicv3_vpe_deschedule(bool enable_doorbell)
{
	bool wakeup = false;

	// Current thread must be a mapped VCPU
	thread_t *current = thread_get_self();
	assert(current != NULL);
	assert_preempt_disabled();

	if (cpulocal_index_valid(current->gicv3_its_mapped_cpu)) {
		assert(cpulocal_get_index() == current->gicv3_its_mapped_cpu);
		gicr_t *gicr = CPULOCAL(gicr_cpu).gicr;
		assert(gicr != NULL);

		// Wait until the pending table has been parsed after any
		// previous vPE scheduling operation, which could have been very
		// recent. The GICR appears to reject deschedule operations if
		// this is not done.
		GICR_VPENDBASER_t vpendbaser;
		do {
			vpendbaser =
				atomic_load_relaxed(&gicr->vlpi.vpendbaser);
			assert(GICR_VPENDBASER_get_Valid(&vpendbaser));
		} while (GICR_VPENDBASER_get_Dirty(&vpendbaser));

		// Write an invalid VPENDBASER.
		GICR_VPENDBASER_set_Valid(&vpendbaser, false);
		GICR_VPENDBASER_set_Doorbell(&vpendbaser, enable_doorbell);
		// If we are not enabling the doorbell, write PendingLast as 1
		// to tell the GICR that we don't care, so the GICR can avoid
		// wasting time calculating it.
		GICR_VPENDBASER_set_PendingLast(&vpendbaser, !enable_doorbell);
		atomic_store_relaxed(&gicr->vlpi.vpendbaser, vpendbaser);

		TRACE(DEBUG, INFO,
		      "gicv3_vpe_deschedule: {:#x} -> vpendbase {:#x}",
		      (uintptr_t)current, GICR_VPENDBASER_raw(vpendbaser));

		if (enable_doorbell) {
			// Read back VPENDBASER to get PendingLast which
			// indicates that a VLPI or VSGI is already pending
			// (which will suppress the doorbell LPI, so we must
			// wake the thread now).
			//
			// This could be deferred to reduce the time spent in
			// the loop, but it must be done before the new thread's
			// ICH_MDCR_EL2 is loaded, so PendingLast calculation
			// uses this thread's group enable bits.
			do {
				vpendbaser = atomic_load_relaxed(
					&gicr->vlpi.vpendbaser);
				assert(!GICR_VPENDBASER_get_Valid(&vpendbaser));
			} while (GICR_VPENDBASER_get_Dirty(&vpendbaser));
			wakeup = GICR_VPENDBASER_get_PendingLast(&vpendbaser);

			if (wakeup) {
				current->gicv3_its_need_wakeup_check = true;
			}
		}
	}

	return wakeup;
}

bool
gicv3_vpe_check_wakeup(bool retry_trap)
{
	thread_t *current    = thread_get_self();
	bool	  might_wake = false;
	assert(current->kind == THREAD_KIND_VCPU);

	cpulocal_begin();

	if (cpulocal_index_valid(current->gicv3_its_mapped_cpu) &&
	    current->gicv3_its_need_wakeup_check) {
		// The VCPU is scheduled in the GICR. Check whether the
		// scheduling operation has finished; if so, we won't need to
		// exit next time we get here.
		assert(cpulocal_get_index() == current->gicv3_its_mapped_cpu);
		gicr_t *gicr = CPULOCAL(gicr_cpu).gicr;
		assert(gicr != NULL);
		GICR_VPENDBASER_t vpendbaser =
			atomic_load_relaxed(&gicr->vlpi.vpendbaser);
		TRACE(DEBUG, INFO,
		      "gicv3_vpe_check_wakeup: may wakeup, vpendbase {:#x}, retry {:d}",
		      GICR_VPENDBASER_raw(vpendbaser), (register_t)retry_trap);
		assert(GICR_VPENDBASER_get_Valid(&vpendbaser));
		if (!GICR_VPENDBASER_get_Dirty(&vpendbaser)) {
			__asm__ volatile("dsb ish" ::: "memory");
			current->gicv3_its_need_wakeup_check = false;
		}

		if (retry_trap || GICR_VPENDBASER_get_Dirty(&vpendbaser)) {
			might_wake = true;
		}
	} else {
		TRACE(DEBUG, INFO, "gicv3_vpe_check_wakeup: no wakeup");
	}

	cpulocal_end();

	return might_wake;
}

void
gicv3_vpe_sync_deschedule(cpu_index_t cpu, bool maybe_scheduled)
{
	assert_cpulocal_safe();
	gicr_t *gicr = CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr;
	assert(gicr != NULL);

	GICR_VPENDBASER_t vpendbaser;
	do {
		vpendbaser = atomic_load_relaxed(&gicr->vlpi.vpendbaser);
		TRACE(DEBUG, INFO, "gicv3_vpe_sync_deschedule: vpendbase {:#x}",
		      GICR_VPENDBASER_raw(vpendbaser));
		assert(maybe_scheduled ||
		       !GICR_VPENDBASER_get_Valid(&vpendbaser));
	} while (!GICR_VPENDBASER_get_Valid(&vpendbaser) &&
		 GICR_VPENDBASER_get_Dirty(&vpendbaser));
}

bool
gicv3_vpe_handle_irq_received_doorbell(hwirq_t *hwirq)
{
	thread_t *vcpu = atomic_load_consume(&hwirq->gicv3_its_vcpu);

	scheduler_lock(vcpu);
	vcpu->gicv3_its_need_wakeup_check = true;
	vcpu_wakeup(vcpu);
	scheduler_unlock(vcpu);

	return true;
}

uint32_result_t
gicv3_vpe_vsgi_query(thread_t *vcpu)
{
	uint32_result_t ret;
	cpu_index_t	cpu = vcpu->gicv3_its_mapped_cpu;

	if (cpulocal_index_valid(cpu)) {
		gicr_t	   *gicr = CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr;
		spinlock_t *lock =
			&CPULOCAL_BY_INDEX(gicr_cpu, cpu).vsgi_query_lock;

		spinlock_acquire(lock);
		GICR_VSGIPENDR_t pend;
#if !defined(NDEBUG)
		pend = atomic_load_acquire(&gicr->vlpi.vsgipendr);
		assert(!GICR_VSGIPENDR_get_Busy(&pend));
#endif
		GICR_VSGIR_t vsgir = GICR_VSGIR_default();
		GICR_VSGIR_set_vPEID(&vsgir, vcpu->gicv3_its_vpe_id);
		atomic_store_release(&gicr->vlpi.vsgir, vsgir);
		do {
			pend = atomic_load_relaxed(&gicr->vlpi.vsgipendr);
		} while (GICR_VSGIPENDR_get_Busy(&pend));
		spinlock_release(lock);

		ret = uint32_result_ok(GICR_VSGIPENDR_get_Pending(&pend));
	} else {
		ret = uint32_result_error(ERROR_IDLE);
	}

	return ret;
}

#elif GICV3_HAS_VLPI // && !GICV3_HAS_VLPI_V4_1
#error VPE scheduling is not implemented for GICv4.0
#endif // GICV3_HAS_VLPI && !GICV3_HAS_VLPI_V4_1

#endif // defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE

static void
gicv3_lpi_disable_all(void) REQUIRE_PREEMPT_DISABLED
{
#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE
	// Deschedule the current vPE, if any, and discard wakeups.
	(void)gicv3_vpe_deschedule(false);
#endif

#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE
	for (cpu_index_t cpu = 0U; cpu < PLATFORM_MAX_CORES; cpu++) {
		if (CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr == NULL) {
			// No GICR for this CPU index, skip it
			continue;
		}

		// Ensure that the GICR has finished descheduling the last vPE
		gicv3_vpe_sync_deschedule(cpu, false);
	}
#endif

	for (index_t i = 0U; i < PLATFORM_GICR_COUNT; i++) {
		gicr_t *gicr = mapped_gicrs[i];

		// Disable LPIs and clear the vPE table base
		GICR_CTLR_t gicr_ctlr = atomic_load_relaxed(&gicr->rd.ctlr);
		GICR_CTLR_set_Enable_LPIs(&gicr_ctlr, false);
		atomic_store_relaxed(&gicr->rd.ctlr, gicr_ctlr);
#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE
		atomic_store_relaxed(&gicr->vlpi.vpropbaser,
				     GICR_VPROPBASER_default());
#endif

		// Wait for the GICR to finish disabling LPIs before clearing
		// the PROPBASE and PENDBASE registers
		gicr_wait_for_write(gicr);

		atomic_store_relaxed(&gicr->rd.propbaser,
				     GICR_PROPBASER_default());
		atomic_store_relaxed(&gicr->rd.pendbaser,
				     GICR_PENDBASER_default());
	}
}
#endif // GICV3_HAS_LPI

void
gicv3_handle_boot_hypervisor_handover(void)
{
	// Disable group 1 interrupts on the local CPU
	ICC_IGRPEN_EL1_t icc_grpen1 = ICC_IGRPEN_EL1_default();
	ICC_IGRPEN_EL1_set_Enable(&icc_grpen1, false);
	register_ICC_IGRPEN1_EL1_write_ordered(icc_grpen1, &asm_ordering);

	// Disable affinity-routed group 1 interrupts at the distributor
	GICD_CTLR_t ctlr = atomic_load_relaxed(&gicd->ctlr);
	GICD_CTLR_NS_set_EnableGrp1A(&ctlr.ns, false);
	atomic_store_relaxed(&gicd->ctlr, ctlr);
	(void)gicd_wait_for_write();

	// Deactivate and clear edge pending for all SPIs
	for (index_t i = 0U; i < 32U; i++) {
		atomic_store_relaxed(&gicd->icactiver[i], 0xffffffffU);
		atomic_store_relaxed(&gicd->icpendr[i], 0xffffffffU);
	}

	// Deactivate and clear edge pending for PPIs on all GICRs
	for (index_t i = 0U; i < PLATFORM_GICR_COUNT; i++) {
		gicr_t *gicr = mapped_gicrs[i];
		atomic_store_relaxed(&gicr->sgi.icactiver0, 0xffffffffU);
		atomic_store_relaxed(&gicr->sgi.icpendr0, 0xffffffffU);
	}
#if GICV3_EXT_IRQS
#error Hand-over clean up of the extended IRQs
#endif

#if GICV3_HAS_LPI
	gicv3_lpi_disable_all();
#endif

#if GICV3_HAS_ITS
	gicv3_its_disable_all(&mapped_gitss);
#endif
}

void
gicv3_handle_power_cpu_online(void)
{
	gicr_cpu_t *gc = &CPULOCAL(gicr_cpu);

	// Mark this CPU as online and availbable for SPI route migration
	assert_preempt_disabled();
	spinlock_acquire_nopreempt(&spi_route_lock);
	gc->online = true;
	spinlock_release_nopreempt(&spi_route_lock);
}

static bool
gicv3_try_move_spi_to_cpu(cpu_index_t target) REQUIRE_PREEMPT_DISABLED
{
	// Hold the spi_route_lock before scanning the SPI route table so it
	// does not change while we are working with it. Holding the lock also
	// prevents the target CPU from going offline while setting routes.

	bool	    moved = false;
	gicr_cpu_t *gc	  = &CPULOCAL_BY_INDEX(gicr_cpu, target);

	assert_preempt_disabled();

	// Take the SPI lock so we can search the route table safely
	spinlock_acquire_nopreempt(&spi_route_lock);

	if (gc->online) {
		// We have an online target CPU we can use. Search SPI routes
		// that need migration
		GICD_IROUTER_t route	 = GICD_IROUTER_default();
		GICD_IROUTER_t route_cmp = GICD_IROUTER_default();

		// Create a route with our affinity to compare against
		// Ignore 1:N routes
		gicv3_spi_set_route_cpu_affinity(&route_cmp,
						 cpulocal_get_index());

		// To advance 32 IRQs at a time, our base should start on a
		// boundary
		static_assert(GIC_SPI_BASE % 32U == 0,
			      "GIC_SPI_BASE not 32bit aligned");

		for (irq_t irq_base = GIC_SPI_BASE;
		     irq_base <= gicv3_spi_max_cache; irq_base += 32U) {
			uint32_t isenabler = atomic_load_relaxed(
				&gicd->isenabler[GICD_ENABLE_GET_N(irq_base)]);
			while (isenabler != 0U) {
				index_t i = compiler_ctz(isenabler);
				isenabler &= ~((index_t)util_bit(i));

				irq_t irq = irq_base + i;

				// Special or reserved IRQs should never be
				// enabled
				assert(gicv3_get_irq_type(irq) ==
				       GICV3_IRQ_TYPE_SPI);

				route = atomic_load_relaxed(
					&gicd->irouter[irq]);
				if (GICD_IROUTER_is_equal(route_cmp, route)) {
					gicv3_spi_set_route_cpu_affinity(
						&route, target);
					error_t ret =
						gicv3_spi_set_route_internal(
							irq, route);
					if (ret != OK) {
						panic("Failed to move SPI");
					}
				}
			}
		}

#if GICV3_EXT_IRQS
#error Migration of the extended IRQs
#endif

		moved = true;
	}

	spinlock_release_nopreempt(&spi_route_lock);

	return moved;
}

void
gicv3_handle_power_cpu_offline(void)
{
	// Migrate any SPIs routed to this CPU to another online CPU.
	// If the IRQ is mapped to the wrong CPU it will get fixed the next
	// time the IRQ occurs.

	// Try to move any SPI IRQs to the next CPU up from this one.
	// If this is the last CPU, wrap around
	cpu_index_t my_index = cpulocal_get_index();
	cpu_index_t target =
		(cpu_index_t)((my_index + 1U) % PLATFORM_MAX_CORES);
	gicr_cpu_t *gc = &CPULOCAL(gicr_cpu);

	assert_preempt_disabled();

	spinlock_acquire_nopreempt(&spi_route_lock);

	gc->online = false;

	spinlock_release_nopreempt(&spi_route_lock);

	// Find an online target CPU before we scan SPI routes
	bool found_target = false;
	while (!found_target) {
		if (platform_cpu_exists(target)) {
			if (gicv3_try_move_spi_to_cpu(target)) {
				found_target = true;
				break;
			}
		}

		// Try the next CPU
		target = (cpu_index_t)((target + 1U) % PLATFORM_MAX_CORES);
		if (target == my_index) {
			// we looped around without finding a target,
			// this should never happen.
			break;
		}
	}

	if (!found_target) {
		panic("Could not find target CPU for SPI migration");
	}
}
