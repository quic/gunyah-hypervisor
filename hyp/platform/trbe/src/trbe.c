// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypregisters.h>

#include <cpulocal.h>
#include <hyp_aspace.h>
#include <panic.h>
#include <partition.h>
#include <vet.h>

#include <asm/sysregs.h>

#include "trbe.h"

CPULOCAL_DECLARE_STATIC(trbe_context_t, trbe_contexts);

void
trbe_save_context_percpu(cpu_index_t cpu)
{
	CPULOCAL_BY_INDEX(trbe_contexts, cpu).TRBLIMITR_EL1 =
		register_TRBLIMITR_EL1_read_ordered(&vet_ordering);

	CPULOCAL_BY_INDEX(trbe_contexts, cpu).TRBPTR_EL1 =
		register_TRBPTR_EL1_read_ordered(&vet_ordering);

	CPULOCAL_BY_INDEX(trbe_contexts, cpu).TRBBASER_EL1 =
		register_TRBBASER_EL1_read_ordered(&vet_ordering);

	CPULOCAL_BY_INDEX(trbe_contexts, cpu).TRBSR_EL1 =
		register_TRBSR_EL1_read_ordered(&vet_ordering);

	CPULOCAL_BY_INDEX(trbe_contexts, cpu).TRBMAR_EL1 =
		register_TRBMAR_EL1_read_ordered(&vet_ordering);

	CPULOCAL_BY_INDEX(trbe_contexts, cpu).TRBTRG_EL1 =
		register_TRBTRG_EL1_read_ordered(&vet_ordering);
}

void
trbe_restore_context_percpu(cpu_index_t cpu)
{
	register_TRBLIMITR_EL1_write_ordered(
		CPULOCAL_BY_INDEX(trbe_contexts, cpu).TRBLIMITR_EL1,
		&vet_ordering);

	register_TRBPTR_EL1_write_ordered(
		CPULOCAL_BY_INDEX(trbe_contexts, cpu).TRBPTR_EL1,
		&vet_ordering);

	register_TRBBASER_EL1_write_ordered(
		CPULOCAL_BY_INDEX(trbe_contexts, cpu).TRBBASER_EL1,
		&vet_ordering);

	register_TRBSR_EL1_write_ordered(
		CPULOCAL_BY_INDEX(trbe_contexts, cpu).TRBSR_EL1, &vet_ordering);

	register_TRBMAR_EL1_write_ordered(
		CPULOCAL_BY_INDEX(trbe_contexts, cpu).TRBMAR_EL1,
		&vet_ordering);

	register_TRBTRG_EL1_write_ordered(
		CPULOCAL_BY_INDEX(trbe_contexts, cpu).TRBTRG_EL1,
		&vet_ordering);
}
