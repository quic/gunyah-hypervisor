// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

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
#include <preempt.h>
#include <trace.h>
#include <util.h>

#include <events/platform.h>

#include <asm/barrier.h>

#include "event_handlers.h"
#include "gicv3.h"

#if defined(VERBOSE) && VERBOSE
#define GICV3_DEBUG 1
#else
#define GICV3_DEBUG 0
#endif

#define GICD_ENABLE_GET_N(x) ((x) >> 5)
#define GIC_ENABLE_BIT(x)    (uint32_t)(1UL << ((x)&31UL))

// All interrupts will be set to the default priority.
//
// Interrupts with priority zero are presumed to be reserved by EL3.
#define GIC_PRIORITY_DEFAULT 0xA0U

static gicd_t *gicd;
static gicr_t *mapped_gicrs[PLATFORM_MAX_CORES];

typedef struct gicr_cpu {
	ICC_SGIR_EL1_t icc_sgi1r;
	gicr_t *       gicr;
} gicr_cpu_t;
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

static count_t gicv3_irq_max_cache;

static void
gicr_set_percpu(cpu_index_t cpu)
{
	gicr_t *gicr = mapped_gicrs[0];

	psci_mpidr_t mpidr = platform_cpu_index_to_mpidr(cpu);
	uint8_t	     aff0  = psci_mpidr_get_Aff0(&mpidr);
	uint8_t	     aff1  = psci_mpidr_get_Aff1(&mpidr);
	uint8_t	     aff2  = psci_mpidr_get_Aff2(&mpidr);
	uint8_t	     aff3  = psci_mpidr_get_Aff3(&mpidr);

	size_t gicr_stride = 1U << GICR_STRIDE_SHIFT; // 64k for v3, 128k for v4
	GICR_TYPER_t gicr_typer;

	// Search for the redistributor that matches this affinity value. We
	// assume that the stride that separates all redistributors is the same.
	do {
		gicr_typer = atomic_load_relaxed(&gicr->rd.typer);

		if ((GICR_TYPER_get_Aff0(&gicr_typer) == aff0) &&
		    (GICR_TYPER_get_Aff1(&gicr_typer) == aff1) &&
		    (GICR_TYPER_get_Aff2(&gicr_typer) == aff2) &&
		    (GICR_TYPER_get_Aff3(&gicr_typer) == aff3)) {
			break;
		} else {
			gicr = (gicr_t *)((paddr_t)gicr + gicr_stride);
		}

	} while (!GICR_TYPER_get_Last(&gicr_typer));

	if ((GICR_TYPER_get_Aff0(&gicr_typer) != aff0) ||
	    (GICR_TYPER_get_Aff1(&gicr_typer) != aff1) ||
	    (GICR_TYPER_get_Aff2(&gicr_typer) != aff2) ||
	    (GICR_TYPER_get_Aff3(&gicr_typer) != aff3)) {
		panic("gicv3: Unable to find CPU's redistributor.");
	}

	CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr = gicr;
}

count_t
gicv3_irq_max(void)
{
	return gicv3_irq_max_cache;
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
		   (irq < (GIC_SPI_BASE + GIC_SPI_NUM))) {
		type = GICV3_IRQ_TYPE_SPI;
	} else if ((irq >= GIC_SPECIAL_INTIDS_BASE) &&
		   (irq < (GIC_SPECIAL_INTIDS_BASE + GIC_SPECIAL_INTIDS_NUM))) {
		type = GICV3_IRQ_TYPE_SPECIAL;
#if GICv3_EXT_IRQS
	} else if ((irq >= GIC_PPI_EXT_BASE) &&
		   (irq < (GIC_PPI_EXT_BASE + GIC_PPI_EXT_NUM))) {
		type = GICV3_IRQ_TYPE_PPI_EXT;
	} else if ((irq >= GIC_SPI_EXT_BASE) &&
		   (irq < (GIC_SPI_EXT_BASE + GIC_SPI_EXT_NUM))) {
		type = GICV3_IRQ_TYPE_SPI_EXT;
#endif
#if GICv3_HAS_LPI
	} else if (irq >= GIC_LPI_BASE) {
		type = GICV3_IRQ_TYPE_LPI;
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
#if GICv3_EXT_IRQS
	case GICV3_IRQ_TYPE_PPI_EXT:
#endif
		ret = true;
		break;
	case GICV3_IRQ_TYPE_SPI:
#if GICv3_EXT_IRQS
	case GICV3_IRQ_TYPE_SPI_EXT:
#endif
#if GICv3_HAS_LPI
	case GICV3_IRQ_TYPE_LPI:
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
	bool	ret	  = false;
	uint8_t ipriority = 0U;

	assert(irq <= gicv3_irq_max());
	gicr_t *     gicr	= CPULOCAL(gicr_cpu).gicr;
	GICD_TYPER_t gicd_typer = atomic_load_relaxed(&gicd->typer);

	count_t it_lines = GICD_TYPER_get_ITLinesNumber(&gicd_typer) + 1U;
	count_t max_spi_num =
		util_min((it_lines * 32U) - 1U, GIC_SPI_BASE + GIC_SPI_NUM - 1);

#if GICv3_EXT_IRQS
	count_t	     max_spi_ext_num, max_ppi_ext_num;
	GICR_TYPER_t gicr_typer = atomic_load_relaxed(&gicr->rd.typer);

	if (GICD_TYPER_get_ESPI(&gicd_typer)) {
		max_spi_ext_num =
			((GICD_TYPER_get_ESPI_range(&gicd_typer) + 1U) * 32U) +
			GIC_SPI_EXT_BASE - 1U;
	} else {
		max_spi_ext_num = GIC_SPI_EXT_BASE - 1;
	}

	if (GICR_TYPER_get_PPInum(&gicr_typer) == GICR_TYPER_PPINUM_MAX_1087) {
		max_ppi_ext_num = 1087U;
	} else if (GICR_TYPER_get_PPInum(&gicr_typer) ==
		   GICR_TYPER_PPINUM_MAX_1119) {
		max_ppi_ext_num = 1119U;
	} else {
		// No extended PPIs
		max_ppi_ext_num = 0U;
	}
#endif

	if (irq < GIC_SPI_BASE) {
		ipriority = atomic_load_relaxed(&gicr->sgi.ipriorityr[irq]);
	} else if (irq <= max_spi_num) {
		ipriority = atomic_load_relaxed(&gicd->ipriorityr[irq]);
#if GICv3_EXT_IRQS
	} else if ((GICR_TYPER_get_PPInum(&gicr_typer) != 0) &&
		   (irq >= GIC_PPI_EXT_BASE) && (irq <= max_ppi_ext_num)) {
		// Extended PPI
		ipriority = atomic_load_relaxed(
			&gicr->sgi.ipriorityr_e[irq - GIC_PPI_EXT_BASE]);
	} else if (GICD_TYPER_get_ESPI(&gicd_typer) &&
		   (irq >= GIC_SPI_EXT_BASE) && (irq <= max_spi_ext_num)) {
		// Extended SPI
		ipriority = atomic_load_relaxed(
			&gicd->ipriorityr_e[irq - GIC_SPI_EXT_BASE]);
#endif
	} else {
		// No action required as irq is not handled.
	}

	// All interrupts with priority zero are reserved.
	if (ipriority == 0U) {
		ret = true;
	}

	return ret;
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

// In boot_cold we map the distributor and all the redistributors, based on
// their base addresses and sizes read from the device tree. We then initialize
// the distributor.
void
gicv3_handle_boot_cold_init(cpu_index_t cpu)
{
	partition_t *hyp_partition = partition_get_private();

	// FIXME: remove when read from device tree
	paddr_t gicd_base   = PLATFORM_GICD_BASE;
	size_t	gicd_size   = 0x10000U; // GICD is always 64K
	paddr_t gicr_base   = PLATFORM_GICR_BASE;
	size_t	gicr_stride = 1U << GICR_STRIDE_SHIFT;
	size_t	gicr_size   = PLATFORM_MAX_CORES << GICR_STRIDE_SHIFT;

	virt_range_result_t range = hyp_aspace_allocate(
		util_balign_up(gicd_size, gicr_size) + gicr_size);
	if (range.e != OK) {
		panic("gicv3: Address allocation failed.");
	}

	pgtable_hyp_start();

	// Map the distributor
	gicd	    = (gicd_t *)range.r.base;
	error_t ret = pgtable_hyp_map(hyp_partition, (uintptr_t)gicd, gicd_size,
				      gicd_base,
				      PGTABLE_HYP_MEMTYPE_NOSPEC_NOCOMBINE,
				      PGTABLE_ACCESS_RW,
				      VMSA_SHAREABILITY_NON_SHAREABLE);
	if (ret != OK) {
		panic("gicv3: Mapping of distributor failed.");
	}

	// Map the redistributors and calculate their addresses
	mapped_gicrs[0] =
		(gicr_t *)(range.r.base + util_balign_up(gicd_size, gicr_size));
	ret = pgtable_hyp_map(hyp_partition, (uintptr_t)mapped_gicrs[0],
			      gicr_size, gicr_base,
			      PGTABLE_HYP_MEMTYPE_NOSPEC_NOCOMBINE,
			      PGTABLE_ACCESS_RW,
			      VMSA_SHAREABILITY_NON_SHAREABLE);
	if (ret != OK) {
		panic("gicv3: Mapping of redistributors failed.");
	}

	for (cpu_index_t i = 1; cpulocal_index_valid(i); i++) {
		mapped_gicrs[i] =
			(gicr_t *)((paddr_t)mapped_gicrs[i - 1] + gicr_stride);
	}

	pgtable_hyp_commit();

	// Disable the distributor
	atomic_store_relaxed(&gicd->ctlr,
			     (GICD_CTLR_t){ .ns = GICD_CTLR_NS_default() });
	GICD_CTLR_NS_t ctlr = gicd_wait_for_write();

	// If security disabled set all interrupts to group 1
	GICD_CTLR_t ctlr_ds = atomic_load_relaxed(&(gicd->ctlr));
	if (GICD_CTLR_DS_get_DS(&ctlr_ds.ds)) {
		for (index_t i = 0; i < util_array_size(gicd->igroupr); i++) {
			atomic_store_relaxed(&gicd->igroupr[i], 0xffffffff);
		}
	}

	// Calculate the number of supported IRQs
	GICD_TYPER_t typer = atomic_load_relaxed(&gicd->typer);

#if GICv3_EXT_IRQS
	bool espi = GICD_TYPER_get_ESPI(&typer);

	if (espi) {
		count_t espi_range = GICD_TYPER_get_ESPI_range(&typer);
		gicv3_irq_max_cache =
			GIC_SPI_EXT_BASE - 1U + (32U * (espi_range + 1U));
	} else {
		gicr_t *     gicr	= mapped_gicrs[0];
		GICR_TYPER_t gicr_typer = atomic_load_relaxed(&gicr->rd.typer);

		GICR_TYPER_PPInum_t eppi = GICR_TYPER_get_PPInum(&gicr_typer);

		switch (eppi) {
		case GICR_TYPER_PPINUM_MAX_1087:
			gicv3_irq_max_cache = 1087U;
			break;
		case GICR_TYPER_PPINUM_MAX_1119:
			gicv3_irq_max_cache = 1119U;
			break;
		case GICR_TYPER_PPINUM_MAX_31:
		default:
			assert(eppi == GICR_TYPER_PPINUM_MAX_31);
			count_t lines = GICD_TYPER_get_ITLinesNumber(&typer);
			gicv3_irq_max_cache =
				util_min(GIC_SPI_BASE + GIC_SPI_NUM - 1U,
					 (32U * (lines + 1U)) - 1U);
			break;
		}
	}
#else
	count_t lines	    = GICD_TYPER_get_ITLinesNumber(&typer);
	gicv3_irq_max_cache = util_min(GIC_SPI_BASE + GIC_SPI_NUM - 1U,
				       (32U * (lines + 1U)) - 1U);
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

#if GICv3_EXT_IRQS
	// Configure all extended SPIs to the default priority
	for (irq_t i = 0; i < GIC_SPI_EXT_NUM; i++) {
		atomic_store_relaxed(&gicd->ipriorityr_e[i],
				     GIC_PRIORITY_DEFAULT);
	}
#endif

	psci_mpidr_t   mpidr   = platform_cpu_index_to_mpidr(cpu);
	uint8_t	       aff0    = psci_mpidr_get_Aff0(&mpidr);
	uint8_t	       aff1    = psci_mpidr_get_Aff1(&mpidr);
	uint8_t	       aff2    = psci_mpidr_get_Aff2(&mpidr);
	uint8_t	       aff3    = psci_mpidr_get_Aff3(&mpidr);
	GICD_IROUTER_t irouter = GICD_IROUTER_default();
	GICD_IROUTER_set_IRM(&irouter, false);
	GICD_IROUTER_set_Aff0(&irouter, aff0);
	GICD_IROUTER_set_Aff1(&irouter, aff1);
	GICD_IROUTER_set_Aff2(&irouter, aff2);
	GICD_IROUTER_set_Aff3(&irouter, aff3);

	for (irq_t i = 0; i < GIC_SPI_NUM; i++) {
		atomic_store_relaxed(&gicd->irouter[i], irouter);
	}
#if GICv3_EXT_IRQS
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

#if GICv3_EXT_IRQS
	for (index_t i = 0; i < util_array_size(gicd->icenabler_e); i++) {
		atomic_store_relaxed(&gicd->icenabler_e[i], 0xffffffff);
	}
#endif
	(void)gicd_wait_for_write();

	// Set up the cached SGIRs used for IPIs targeting each CPU
	for (cpu_index_t i = 0U; i < PLATFORM_MAX_CORES; i++) {
		mpidr = platform_cpu_index_to_mpidr(i);

		aff0 = psci_mpidr_get_Aff0(&mpidr);
		aff1 = psci_mpidr_get_Aff1(&mpidr);
		aff2 = psci_mpidr_get_Aff2(&mpidr);
		aff3 = psci_mpidr_get_Aff3(&mpidr);

		ICC_SGIR_EL1_t icc_sgi1r = ICC_SGIR_EL1_default();
		ICC_SGIR_EL1_set_TargetList(&icc_sgi1r,
					    (uint16_t)(1U << (aff0 % 16U)));
		ICC_SGIR_EL1_set_RS(&icc_sgi1r, aff0 / 16U);
		ICC_SGIR_EL1_set_Aff1(&icc_sgi1r, aff1);
		ICC_SGIR_EL1_set_Aff2(&icc_sgi1r, aff2);
		ICC_SGIR_EL1_set_Aff3(&icc_sgi1r, aff3);

		CPULOCAL_BY_INDEX(gicr_cpu, i).icc_sgi1r = icc_sgi1r;
	}

	// Set up gicr for each CPU
	for (cpu_index_t i = 0U; i < PLATFORM_MAX_CORES; i++) {
		gicr_set_percpu(i);
	}
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

#if GICv3_EXT_IRQS
	for (irq_t i = 0; i < GIC_PPI_EXT_NUM; i++) {
		atomic_store_relaxed(&gicr->sgi.ipriorityr_e[i],
				     GIC_PRIORITY_DEFAULT);
	}
#endif

	// If security disabled set all interrupts to group 1
	GICD_CTLR_t ctlr_ds = atomic_load_relaxed(&(gicd->ctlr));
	if (GICD_CTLR_DS_get_DS(&ctlr_ds.ds)) {
		atomic_store_relaxed(&gicr->sgi.igroupr0, 0xffffffff);
	}

	// Wake gicr
	GICR_WAKER_t waker = GICR_WAKER_default();
	GICR_WAKER_set_ProcessorSleep(&waker, false);
	atomic_store_release(&gicr->rd.waker, waker);

	// Disable all local IRQs
	atomic_store_relaxed(&gicr->sgi.icenabler0, 0xffffffff);
#if GICv3_EXT_IRQS
	for (index_t i = 0; i < util_array_size(gicr->sgi.icenabler_e); i++) {
		atomic_store_relaxed(&gicr->sgi.icenabler_e, 0xffffffff);
	}
#endif
	gicr_wait_for_write(gicr);

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
	struct asm_ordering_dummy gic_init_order;

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
	ICC_IGRPEN1_EL1_t icc_grpen1 = ICC_IGRPEN1_EL1_default();
	ICC_IGRPEN1_EL1_set_Enable(&icc_grpen1, true);
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
		ICC_HPPIR1_EL1_raw(register_ICC_HPPIR1_EL1_read()));
#endif
}

error_t
gicv3_handle_power_cpu_suspend(void)
{
	// Disable group 1 interrupts
	ICC_IGRPEN1_EL1_t icc_grpen1 = ICC_IGRPEN1_EL1_default();
	ICC_IGRPEN1_EL1_set_Enable(&icc_grpen1, false);
	register_ICC_IGRPEN1_EL1_write_ordered(icc_grpen1, &asm_ordering);

	gicr_t *gicr = CPULOCAL(gicr_cpu).gicr;
#if GICV3_DEBUG
	TRACE_LOCAL(DEBUG, INFO,
		    "gicv3 cpu suspend, en {:#x} act {:#x} grp {:#x} hpp {:#x}",
		    atomic_load_relaxed(&gicr->sgi.isenabler0),
		    atomic_load_relaxed(&gicr->sgi.isactiver0),
		    atomic_load_relaxed(&gicr->sgi.igroupr0),
		    ICC_HPPIR1_EL1_raw(register_ICC_HPPIR1_EL1_read()));
#endif

	// Set ProcessorSleep, so that the redistributor hands over ownership of
	// any pending interrupts before it powers off
	GICR_WAKER_t waker = GICR_WAKER_default();
	GICR_WAKER_set_ProcessorSleep(&waker, true);
	atomic_store_release(&gicr->rd.waker, waker);

	// Wait for gicr to be off
	// Order the write we're waiting for before the loads in the poll
	atomic_device_fence(memory_order_seq_cst);

	GICR_WAKER_t waker_read = atomic_load_relaxed(&gicr->rd.waker);

	while (GICR_WAKER_get_ChildrenAsleep(&waker_read) == 0U) {
		asm_yield();
		waker_read = atomic_load_relaxed(&gicr->rd.waker);
	}

	return OK;
}

void
gicv3_handle_power_cpu_resume(void)
{
	struct asm_ordering_dummy gic_enable_order;

	// Enable group 1 interrupts
	ICC_IGRPEN1_EL1_t icc_grpen1 = ICC_IGRPEN1_EL1_default();
	ICC_IGRPEN1_EL1_set_Enable(&icc_grpen1, true);
	register_ICC_IGRPEN1_EL1_write_ordered(icc_grpen1, &gic_enable_order);
	asm_context_sync_ordered(&gic_enable_order);

	gicr_t *gicr = CPULOCAL(gicr_cpu).gicr;
#if GICV3_DEBUG
	TRACE_LOCAL(DEBUG, INFO,
		    "gicv3 cpu resume, en {:#x} act {:#x} grp {:#x} hpp {:#x}",
		    atomic_load_relaxed(&gicr->sgi.isenabler0),
		    atomic_load_relaxed(&gicr->sgi.isactiver0),
		    atomic_load_relaxed(&gicr->sgi.igroupr0),
		    ICC_HPPIR1_EL1_raw(register_ICC_HPPIR1_EL1_read()));
#endif

	// Clear ProcessorSleep, so that it can start handling interrupts.
	GICR_WAKER_t waker = GICR_WAKER_default();
	GICR_WAKER_set_ProcessorSleep(&waker, false);
	atomic_store_release(&gicr->rd.waker, waker);
}

void
gicv3_irq_enable(irq_t irq)
{
	assert(irq <= gicv3_irq_max());

	gicv3_irq_type_t irq_type = gicv3_get_irq_type(irq);

	switch (irq_type) {
	case GICV3_IRQ_TYPE_SPI:
		atomic_store_release(&gicd->isenabler[GICD_ENABLE_GET_N(irq)],
				     GIC_ENABLE_BIT(irq));
		break;

#if GICv3_EXT_IRQS
	case GICV3_IRQ_TYPE_SPI_EXT:
		// Extended SPI
		atomic_store_release(&gicd->isenabler_e[GICD_ENABLE_GET_N(
					     irq - GIC_SPI_EXT_BASE)],
				     GIC_ENABLE_BIT(irq - GIC_SPI_EXT_BASE));
		break;
#endif
	case GICV3_IRQ_TYPE_SGI:
	case GICV3_IRQ_TYPE_PPI:
#if GICv3_EXT_IRQS
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

	gicr_t *	 gicr	  = CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr;
	gicv3_irq_type_t irq_type = gicv3_get_irq_type(irq);

	switch (irq_type) {
	case GICV3_IRQ_TYPE_SGI:
	case GICV3_IRQ_TYPE_PPI: {
		atomic_store_release(&gicr->sgi.isenabler0,
				     GIC_ENABLE_BIT(irq));
		break;
	}

#if GICv3_EXT_IRQS
	case GICV3_IRQ_TYPE_PPI_EXT: {
		// Extended PPI
		atomic_store_release(&gicr->sgi.isenabler_e[GICD_ENABLE_GET_N(
					     irq - GIC_PPI_EXT_BASE)],
				     GIC_ENABLE_BIT(irq - GIC_PPI_EXT_BASE));
		break;
	}
#endif
	case GICV3_IRQ_TYPE_SPI:
#if GICv3_EXT_IRQS
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
		gicd_wait_for_write();
		break;

#if GICv3_EXT_IRQS
	case GICV3_IRQ_TYPE_SPI_EXT: {
		// Extended SPI
		atomic_store_relaxed(&gicd->icenabler_e[GICD_ENABLE_GET_N(
					     irq - GIC_SPI_EXT_BASE)],
				     GIC_ENABLE_BIT(irq - GIC_SPI_EXT_BASE));
		gicd_wait_for_write();
		break;
	}
#endif
	case GICV3_IRQ_TYPE_SGI:
	case GICV3_IRQ_TYPE_PPI:
#if GICv3_EXT_IRQS
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

#if GICv3_EXT_IRQS
	case GICV3_IRQ_TYPE_SPI_EXT: {
		// Extended SPI
		atomic_store_relaxed(&gicd->icpendr_e[GICD_ENABLE_GET_N(
					     irq - GIC_SPI_EXT_BASE)],
				     GIC_ENABLE_BIT(irq - GIC_SPI_EXT_BASE));
		// As above, there is no way to guarantee completion.
		break;
	}
#endif
	case GICV3_IRQ_TYPE_SGI:
	case GICV3_IRQ_TYPE_PPI:
#if GICv3_EXT_IRQS
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

	gicr_t *	 gicr	  = CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr;
	gicv3_irq_type_t irq_type = gicv3_get_irq_type(irq);

	switch (irq_type) {
	case GICV3_IRQ_TYPE_SGI:
	case GICV3_IRQ_TYPE_PPI: {
		atomic_store_relaxed(&gicr->sgi.icenabler0,
				     GIC_ENABLE_BIT(irq));
		break;
	}
#if GICv3_EXT_IRQS
	case GICV3_IRQ_TYPE_PPI_EXT: {
		// Extended PPI
		atomic_store_relaxed(&gicr->sgi.icenabler_e[GICD_ENABLE_GET_N(
					     irq - GIC_PPI_EXT_BASE)],
				     GIC_ENABLE_BIT(irq - GIC_PPI_EXT_BASE));
		break;
	}
#endif
	case GICV3_IRQ_TYPE_SPI:
#if GICv3_EXT_IRQS
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
	gicr_wait_for_write(CPULOCAL(gicr_cpu).gicr);
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

	gicr_t *	 gicr	  = CPULOCAL_BY_INDEX(gicr_cpu, cpu).gicr;
	gicv3_irq_type_t irq_type = gicv3_get_irq_type(irq);

	// We do not support this behavior for now
	if (trigger == IRQ_TRIGGER_MESSAGE) {
		ret = irq_trigger_result_error(ERROR_ARGUMENT_INVALID);
		goto end_function;
	}

	switch (irq_type) {
	case GICV3_IRQ_TYPE_PPI: {
		uint32_t isenabler0 =
			atomic_load_relaxed(&gicr->sgi.isenabler0);
		bool enabled = (isenabler0 & GIC_ENABLE_BIT(irq)) != 0U;

		if (enabled) {
			gicv3_irq_disable_percpu(irq, cpu);
		}

		register_t icfg =
			(register_t)atomic_load_relaxed(&gicr->sgi.icfgr1);

		if ((trigger == IRQ_TRIGGER_LEVEL_HIGH) ||
		    (trigger == IRQ_TRIGGER_LEVEL_LOW)) {
			bitmap_clear(&icfg, ((irq % 16U) * 2U) + 1U);
		} else {
			bitmap_set(&icfg, ((irq % 16U) * 2U) + 1U);
		}

		atomic_store_relaxed(&gicr->sgi.icfgr1, (uint32_t)icfg);

		if (enabled) {
			gicv3_irq_enable_percpu(irq, cpu);
		}

		// Read back the value in case it could not be changed
		icfg = atomic_load_relaxed(&gicr->sgi.icfgr1);
		ret  = irq_trigger_result_ok(
			 bitmap_isset(&icfg, ((irq % 16U) * 2U) + 1U)
				 ? IRQ_TRIGGER_EDGE_RISING
				 : IRQ_TRIGGER_LEVEL_HIGH);

		break;
	}

#if GICv3_EXT_IRQS
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
#if GICv3_EXT_IRQS
	case GICV3_IRQ_TYPE_SPI_EXT:
#endif
#if GICv3_HAS_LPI
	case GICV3_IRQ_TYPE_LPI:
#endif
	case GICV3_IRQ_TYPE_SPECIAL:
	case GICV3_IRQ_TYPE_RESERVED:
	default:
		// No action required as irq is not handled.
		ret = irq_trigger_result_error(ERROR_UNIMPLEMENTED);
		break;
	}

end_function:
	return ret;
}

irq_trigger_result_t
gicv3_irq_set_trigger(irq_t irq, irq_trigger_t trigger)
{
	irq_trigger_result_t ret;

	assert(irq <= gicv3_irq_max());

	// We do not support this behavior for now
	if (trigger == IRQ_TRIGGER_MESSAGE) {
		ret = irq_trigger_result_error(ERROR_ARGUMENT_INVALID);
		goto end_function;
	}

	gicv3_irq_type_t irq_type = gicv3_get_irq_type(irq);

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
			(register_t)atomic_load_relaxed(&gicr->sgi.icfgr1);

		if ((trigger == IRQ_TRIGGER_LEVEL_HIGH) ||
		    (trigger == IRQ_TRIGGER_LEVEL_LOW)) {
			bitmap_clear(&icfg, ((irq % 16U) * 2U) + 1U);
		} else {
			bitmap_set(&icfg, ((irq % 16U) * 2U) + 1U);
		}

		atomic_store_relaxed(&gicr->sgi.icfgr1, (uint32_t)icfg);

		if (enabled) {
			gicv3_irq_enable(irq);
		}

		// Read back the value in case it could not be changed
		icfg = atomic_load_relaxed(&gicr->sgi.icfgr1);
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

#if GICv3_EXT_IRQS
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
#if GICv3_HAS_LPI
	case GICV3_IRQ_TYPE_LPI:
#endif
	case GICV3_IRQ_TYPE_SPECIAL:
	case GICV3_IRQ_TYPE_RESERVED:
	default:
		// No action required as irq is not handled.
		ret = irq_trigger_result_error(ERROR_UNIMPLEMENTED);
		break;
	}

end_function:
	return ret;
}

error_t
gicv3_spi_set_route(irq_t irq, GICD_IROUTER_t route)
{
	error_t ret;

	switch (gicv3_get_irq_type(irq)) {
	case GICV3_IRQ_TYPE_SPI:
		atomic_store_relaxed(&gicd->irouter[irq - GIC_SPI_BASE], route);
		ret = OK;
		break;
#if GICv3_EXT_IRQS
	case GICV3_IRQ_TYPE_SPI_EXT:
		atomic_store_relaxed(&gicd->irouter_e[irq - GIC_SPI_EXT_BASE],
				     route);
		ret = OK;
		break;
#endif
	case GICV3_IRQ_TYPE_SGI:
	case GICV3_IRQ_TYPE_PPI:
#if GICv3_EXT_IRQS
	case GICV3_IRQ_TYPE_PPI_EXT:
#endif
#if GICv3_HAS_LPI
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

irq_result_t
gicv3_irq_acknowledge(void)
{
	irq_result_t ret = { 0 };

	ICC_IAR1_EL1_t iar =
		register_ICC_IAR1_EL1_read_volatile_ordered(&asm_ordering);

	uint32_t intid = ICC_IAR1_EL1_get_INTID(&iar);

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
		trigger_platform_ipi_event();
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

	ICC_EOIR1_EL1_t eoir = ICC_EOIR1_EL1_default();

	ICC_EOIR1_EL1_set_INTID(&eoir, irq);

	// No need for a barrier here: nothing we do to handle this IRQ
	// before the priority drop will affect whether we get a different
	// IRQ after the drop.

	register_ICC_EOIR1_EL1_write_ordered(eoir, &asm_ordering);
}

void
gicv3_irq_deactivate(irq_t irq)
{
	assert(irq <= gicv3_irq_max());

	ICC_DIR_EL1_t dir = ICC_DIR_EL1_default();

	ICC_DIR_EL1_set_INTID(&dir, irq);

	// Ensure interrupt handling is complete
	__asm__ volatile("dsb sy; isb" ::: "memory");

	register_ICC_DIR_EL1_write_ordered(dir, &asm_ordering);
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
#if GICv3_EXT_IRQS
	case GICV3_IRQ_TYPE_PPI_EXT: {
		// Extended PPI
		atomic_store_relaxed(&gicr->sgi.icactiver_e[GICD_ENABLE_GET_N(
					     irq - GIC_PPI_EXT_BASE)],
				     GIC_ENABLE_BIT(irq - GIC_PPI_EXT_BASE));
		break;
	}
#endif
	case GICV3_IRQ_TYPE_SPI:
#if GICv3_EXT_IRQS
	case GICV3_IRQ_TYPE_SPI_EXT:
#endif
#if GICv3_HAS_LPI
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
