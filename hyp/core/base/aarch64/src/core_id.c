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
	static const coreid_info_t core_id_data[] = {
		{ .part_num = 0xD03, .core_ID = CORE_ID_CORTEX_A53 },
		{ .part_num = 0xD05, .core_ID = CORE_ID_CORTEX_A55 },
		{ .part_num = 0xD07, .core_ID = CORE_ID_CORTEX_A57 },
		{ .part_num = 0xD08, .core_ID = CORE_ID_CORTEX_A72 },
		{ .part_num = 0xD09, .core_ID = CORE_ID_CORTEX_A73 },
		{ .part_num = 0xD0A, .core_ID = CORE_ID_CORTEX_A75 },
		{ .part_num = 0xD0B, .core_ID = CORE_ID_CORTEX_A76 },
		{ .part_num = 0xD0C, .core_ID = CORE_ID_NEOVERSE_N1 },
		{ .part_num = 0xD0D, .core_ID = CORE_ID_CORTEX_A77 },
		{ .part_num = 0xD0E, .core_ID = CORE_ID_CORTEX_A76AE },
		{ .part_num = 0xD40, .core_ID = CORE_ID_NEOVERSE_V1 },
		{ .part_num = 0xD41, .core_ID = CORE_ID_CORTEX_A78 },
		{ .part_num = 0xD42, .core_ID = CORE_ID_CORTEX_A78AE },
		{ .part_num = 0xD44, .core_ID = CORE_ID_CORTEX_X1 },
		{ .part_num = 0xD46, .core_ID = CORE_ID_CORTEX_A510 },
		{ .part_num = 0xD47, .core_ID = CORE_ID_CORTEX_A710 },
		{ .part_num = 0xD48, .core_ID = CORE_ID_CORTEX_X2 },
		{ .part_num = 0xD49, .core_ID = CORE_ID_NEOVERSE_N2 },
		{ .part_num = 0xD4B, .core_ID = CORE_ID_CORTEX_A78C },
		{ .part_num = 0xD4D, .core_ID = CORE_ID_CORTEX_A715 },
		{ .part_num = 0xD4E, .core_ID = CORE_ID_CORTEX_X3 },
	};

	static const count_t NUM_CORE_ID =
		(count_t)util_array_size(core_id_data);
	core_id_t coreid = CORE_ID_UNKNOWN;

	uint32_t start;

	bool core_identified = false;

	for (start = 0; ((start < NUM_CORE_ID) && (!core_identified));
	     start++) {
		if (partnum == core_id_data[start].part_num) {
			if ((partnum == 0xD81U) || (partnum == 0xD82U)) {
				if ((variant == 0U) && (revision == 0U)) {
					coreid = core_id_data[start].core_ID;
				} else {
					coreid = CORE_ID_UNKNOWN;
				}
			} else {
				coreid = core_id_data[start].core_ID;
			}
			core_identified = true;
		}
	}

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
