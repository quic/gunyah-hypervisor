// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypconstants.h>
#include <hypregisters.h>

#include <base.h>
#include <compiler.h>
#include <cpulocal.h>
#include <log.h>
#include <platform_cpu.h>
#include <preempt.h>
#include <trace.h>
#include <util.h>

// FIXME:
#if !defined(MODULE_PLATFORM_SOC_QCOM)
// Platforms may override this with their own implementation
core_id_t WEAK
platform_cpu_get_coreid(MIDR_EL1_t midr)
{
	(void)midr;
	return CORE_ID_UNKNOWN;
}
#endif

static core_id_t
get_core_id(uint16_t partnum, uint8_t variant, uint8_t revision)
{
	static const core_id_info_t core_id_map[] = {
		{ .part_num = 0xD03U, .core_id = CORE_ID_CORTEX_A53 },
		{ .part_num = 0xD05U, .core_id = CORE_ID_CORTEX_A55 },
		{ .part_num = 0xD07U, .core_id = CORE_ID_CORTEX_A57 },
		{ .part_num = 0xD08U, .core_id = CORE_ID_CORTEX_A72 },
		{ .part_num = 0xD09U, .core_id = CORE_ID_CORTEX_A73 },
		{ .part_num = 0xD0AU, .core_id = CORE_ID_CORTEX_A75 },
		{ .part_num = 0xD0BU, .core_id = CORE_ID_CORTEX_A76 },
		{ .part_num = 0xD0CU, .core_id = CORE_ID_NEOVERSE_N1 },
		{ .part_num = 0xD0DU, .core_id = CORE_ID_CORTEX_A77 },
		{ .part_num = 0xD0EU, .core_id = CORE_ID_CORTEX_A76AE },
		{ .part_num = 0xD40U, .core_id = CORE_ID_NEOVERSE_V1 },
		{ .part_num = 0xD41U, .core_id = CORE_ID_CORTEX_A78 },
		{ .part_num = 0xD42U, .core_id = CORE_ID_CORTEX_A78AE },
		{ .part_num = 0xD44U, .core_id = CORE_ID_CORTEX_X1 },
		{ .part_num = 0xD46U, .core_id = CORE_ID_CORTEX_A510 },
		{ .part_num = 0xD47U, .core_id = CORE_ID_CORTEX_A710 },
		{ .part_num = 0xD48U, .core_id = CORE_ID_CORTEX_X2 },
		{ .part_num = 0xD49U, .core_id = CORE_ID_NEOVERSE_N2 },
		{ .part_num = 0xD4BU, .core_id = CORE_ID_CORTEX_A78C },
		{ .part_num = 0xD4DU, .core_id = CORE_ID_CORTEX_A715 },
		{ .part_num = 0xD4EU, .core_id = CORE_ID_CORTEX_X3 },
		{ .part_num = 0xD80U, .core_id = CORE_ID_CORTEX_A520 },
	};
	// List of cores that have specific revisions.
	// If multiple revisions are assigned different core IDs, then keep
	// them sorted by highest (variant_min,revision_min) first.
	static const core_id_rev_info_t core_id_rev_map[] = {
		{ .part_num	= 0xD81U,
		  .core_id	= CORE_ID_CORTEX_A720,
		  .variant_min	= 0,
		  .revision_min = 1 },
		{ .part_num	= 0xD82U,
		  .core_id	= CORE_ID_CORTEX_X4,
		  .variant_min	= 0,
		  .revision_min = 1 }
	};

	core_id_t coreid;
	index_t	  i;

	for (i = 0U; i < util_array_size(core_id_map); i++) {
		if (partnum == core_id_map[i].part_num) {
			coreid = core_id_map[i].core_id;
			goto out;
		}
	}

	for (i = 0U; i < util_array_size(core_id_rev_map); i++) {
		if ((partnum == core_id_rev_map[i].part_num) ||
		    (variant > core_id_rev_map[i].variant_min) ||
		    ((variant == core_id_rev_map[i].variant_min) &&
		     (revision >= core_id_rev_map[i].revision_min))) {
			coreid = core_id_rev_map[i].core_id;
			goto out;
		}
	}

	coreid = CORE_ID_UNKNOWN;
out:
	return coreid;
}

core_id_t
get_current_core_id(void) REQUIRE_PREEMPT_DISABLED
{
	core_id_t coreid;

	assert_cpulocal_safe();

	MIDR_EL1_t midr = register_MIDR_EL1_read();

	uint8_t	 implementer = MIDR_EL1_get_Implementer(&midr);
	uint16_t partnum     = MIDR_EL1_get_PartNum(&midr);
	uint8_t	 variant     = MIDR_EL1_get_Variant(&midr);
	uint8_t	 revision    = MIDR_EL1_get_Revision(&midr);

	if ((char)implementer == 'A') {
		coreid = get_core_id(partnum, variant, revision);
	} else {
		coreid = CORE_ID_UNKNOWN;
	}

	if (coreid == CORE_ID_UNKNOWN) {
		coreid = platform_cpu_get_coreid(midr);
	}

#if defined(VERBOSE) && VERBOSE
	if (coreid == CORE_ID_UNKNOWN) {
		cpu_index_t cpu = cpulocal_get_index();

		LOG(DEBUG, WARN,
		    "detected unknown core ID, cpu: {:d}, MIDR: {:#08x}", cpu,
		    MIDR_EL1_raw(midr));
	}
#endif

	return coreid;
}
