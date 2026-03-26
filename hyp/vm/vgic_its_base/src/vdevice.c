// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypconstants.h>
#include <hypcontainers.h>

#include <atomic.h>
#include <compiler.h>
#include <qcbor.h>
#include <thread.h>
#include <trace.h>
#include <util.h>

#include "event_handlers.h"
#include "vgic.h"
#include "vgic_its_base.h"

// Qualcomm's JEP106 identifier is 0x70, with no continuation bytes. This is
// used in the virtual GITS_IIDR.
#define JEP106_IDENTITY 0x70U
#define JEP106_CONTCODE 0x0U
#define IIDR_PRODUCTID	(uint8_t)'G' /* For "Gunyah" */
#define IIDR_VARIANT	0U
#define IIDR_REVISION	0U

typedef struct {
	uint64_t read_value;
	bool	 ret;
	uint8_t	 pad[7];
} vgic_its_reg_info_t;

static vgic_its_reg_info_t
vgic_its_read_write_access(vgic_its_t *vgic_its, size_t offset, bool is_write,
			   uint64_t write_value)
{
	bool	 ret	    = false;
	uint64_t read_value = 0U;

	switch (offset) {
	case offsetof(gits_t, ctl.ctlr):
		if (is_write) {
			GITS_CTLR_t ctlr =
				GITS_CTLR_cast((uint32_t)write_value);
			vgic_its_set_enabled(vgic_its,
					     GITS_CTLR_get_Enabled(&ctlr));
		} else {
			read_value = GITS_CTLR_raw(vgic_its->ctlr);
		}
		ret = true;
		break;
	case offsetof(gits_t, ctl.iidr):
		if (!is_write) {
			GITS_IIDR_t iidr = GITS_IIDR_default();
			GITS_IIDR_set_Implementer_Identity(&iidr,
							   JEP106_IDENTITY);
			GITS_IIDR_set_Implementer_ContCode(&iidr,
							   JEP106_CONTCODE);
			GITS_IIDR_set_ProductID(&iidr, IIDR_PRODUCTID);
			GITS_IIDR_set_Variant(&iidr, IIDR_VARIANT);
			GITS_IIDR_set_Revision(&iidr, IIDR_REVISION);
			read_value = GITS_IIDR_raw(iidr);
			ret	   = true;
		}
		break;
	case offsetof(gits_t, ctl.typer):
		if (!is_write) {
			GITS_TYPER_t typer = GITS_TYPER_default();
			GITS_TYPER_set_Physical(&typer, true);
			GITS_TYPER_set_ITT_entry_size(
				&typer, sizeof(vgic_its_itte_t) - 1U);
			GITS_TYPER_set_ID_bits(&typer,
					       VGIC_ITS_EVENT_BITS - 1U);
			// Note: all 32 bits of device ID are in principle
			// supported in software even if the hardware supports
			// fewer. Any extra device IDs are only usable through
			// software assertion with INT commands, which use a
			// reserved low device ID rather than the virtual one.
			GITS_TYPER_set_Devbits(&typer, 31U);
			// FIXME: QC Gunyah issue #143
			GITS_TYPER_set_SEIs(&typer, false);
			// Redistributors are always addressed by index
			GITS_TYPER_set_PTA(&typer, false);
			GITS_TYPER_set_CIDbits(&typer, VGIC_ITS_IC_BITS - 1U);
			GITS_TYPER_set_CIL(&typer, true);

			read_value = GITS_TYPER_raw(typer);
			ret	   = true;
		}
		break;
	case offsetof(gits_t, ctl.cbaser):
	case (offsetof(gits_t, ctl.cbaser) + 4U):
		if (is_write) {
			vgic_its_set_command_queue_base(
				vgic_its, GITS_CBASER_cast(write_value));
		} else {
			read_value =
				GITS_CBASER_raw(vgic_its->command_queue_base);
		}
		ret = true;
		break;
	case offsetof(gits_t, ctl.cwriter):
	case (offsetof(gits_t, ctl.cwriter) + 4U):
		if (is_write) {
			vgic_its_set_command_queue_write(
				vgic_its, GITS_CWRITER_cast(write_value));
		} else {
			read_value =
				GITS_CWRITER_raw(vgic_its->command_queue_write);
		}
		ret = true;
		break;
	case offsetof(gits_t, ctl.creadr):
	case (offsetof(gits_t, ctl.creadr) + 4U):
		if (!is_write) {
			read_value =
				GITS_CREADR_raw(vgic_its->command_queue_read);
			ret = true;
		}
		break;
	case OFS_GITS_CTL_BASER(0U):
	case OFS_GITS_CTL_BASER(0U) + 4U:
		if (is_write) {
			vgic_its_set_device_table_base(
				vgic_its, GITS_BASER_cast(write_value));
		} else {
			read_value =
				GITS_BASER_raw(vgic_its->device_table_base);
		}
		ret = true;
		break;
	case OFS_GITS_CTL_BASER(1U):
	case OFS_GITS_CTL_BASER(1U) + 4U:
		if (is_write) {
			vgic_its_set_collection_table_base(
				vgic_its, GITS_BASER_cast(write_value));
		} else {
			read_value =
				GITS_BASER_raw(vgic_its->collection_table_base);
		}
		ret = true;
		break;
	case OFS_GITS_CTL_BASER(2U):
	case OFS_GITS_CTL_BASER(2U) + 4U:
	case OFS_GITS_CTL_BASER(3U):
	case OFS_GITS_CTL_BASER(3U) + 4U:
	case OFS_GITS_CTL_BASER(4U):
	case OFS_GITS_CTL_BASER(4U) + 4U:
	case OFS_GITS_CTL_BASER(5U):
	case OFS_GITS_CTL_BASER(5U) + 4U:
	case OFS_GITS_CTL_BASER(6U):
	case OFS_GITS_CTL_BASER(6U) + 4U:
	case OFS_GITS_CTL_BASER(7U):
	case OFS_GITS_CTL_BASER(7U) + 4U:
		// Unused base registers are WI / RAZ
		read_value = 0U;
		ret	   = true;
		break;
	case offsetof(gits_t, xlate.translater):
		if (is_write) {
			GITS_TRANSLATER_t translater =
				GITS_TRANSLATER_cast((uint32_t)write_value);
			vgic_its_translate(
				vgic_its,
				GITS_TRANSLATER_get_event_id(&translater));
			ret = true;
		}
		break;
	case offsetof(gits_t, PIDR2):
		if (!is_write) {
			read_value = VGIC_PIDR2;
			ret	   = true;
		}
		break;
	default:
		// Nothing to do
		break;
	}

	return (vgic_its_reg_info_t){
		.read_value = read_value,
		.ret	    = ret,
	};
}

static uint64_t
vgic_its_write_access(const vgic_its_t *vgic_its, size_t offset,
		      const register_t *value, bool is_high_word,
		      bool is_low_word)
{
	uint64_t write_value = 0U;

	uint64_t old_value;
	switch (offset & ~util_mask(8U)) {
	case offsetof(gits_t, ctl.cbaser):
		old_value = GITS_CBASER_raw(vgic_its->command_queue_base);
		break;
	case offsetof(gits_t, ctl.cwriter):
		old_value = GITS_CWRITER_raw(vgic_its->command_queue_write);
		break;
	case OFS_GITS_CTL_BASER(0U):
		old_value = GITS_BASER_raw(vgic_its->device_table_base);
		break;
	case OFS_GITS_CTL_BASER(1U):
		old_value = GITS_BASER_raw(vgic_its->collection_table_base);
		break;
	default:
		old_value = 0U;
		break;
	}
	if (is_high_word) {
		write_value = (old_value & util_mask(32U)) | (*value << 32U);
	} else if (is_low_word) {
		write_value = (old_value & ~util_mask(32U)) | (uint32_t)*value;
	} else {
		write_value = *value;
	}

	return write_value;
}

static vcpu_trap_result_t
vgic_its_access(vgic_its_t *vgic_its, size_t offset, size_t access_size,
		register_t *value, bool is_write)
{
	assert(vgic_its != NULL);
	assert(value != NULL);

	bool ret = false;

	if (is_write) {
		TRACE(VGIC, INFO, "VITS {:#x} write {:#x} @ {:#x}/{:d}",
		      (uintptr_t)vgic_its, *value, offset, access_size);
	}

	// Check access offset & size
	bool is_high_word = false;
	bool is_low_word  = false;
	// All 64-bit ITS registers are 32-bit accessible
	switch (offset & ~4U) {
	case offsetof(gits_t, ctl.typer):
	case offsetof(gits_t, ctl.cbaser):
	case offsetof(gits_t, ctl.cwriter):
	case offsetof(gits_t, ctl.creadr):
	case OFS_GITS_CTL_BASER(0U):
	case OFS_GITS_CTL_BASER(1U):
	case OFS_GITS_CTL_BASER(2U):
	case OFS_GITS_CTL_BASER(3U):
	case OFS_GITS_CTL_BASER(4U):
	case OFS_GITS_CTL_BASER(5U):
	case OFS_GITS_CTL_BASER(6U):
	case OFS_GITS_CTL_BASER(7U):
		if (access_size == 4U) {
			// Determine which half is being accessed
			is_high_word = ((offset & 4U) != 0U);
			is_low_word  = !is_high_word;
		} else if (access_size != 8U) {
			goto out;
		} else {
			// 64-bit access, nothing special to do
		}
		break;
	default:
		// Not a 64-bit register; check for 32-bit registers

		if ((offset == offsetof(gits_t, ctl.ctlr)) ||
		    (offset == offsetof(gits_t, ctl.iidr)) ||
		    (offset == offsetof(gits_t, PIDR2))) {
			// 32-bit registers
			if (access_size != 4U) {
				goto out;
			}
		} else if (offset == offsetof(gits_t, xlate.translater)) {
			// 32-bit register; low 16 bits can be written
			// separately but this does not need to be merged with
			// the old value as this register is write-only / RAZ
			if ((access_size != 4U) && (access_size != 2U)) {
				goto out;
			}
		} else {
			// Unknown register
			goto out;
		}
		break;
	}

	// Obtain the value being written, for write accesses
	uint64_t write_value = 0U;
	if (is_write) {
		write_value = vgic_its_write_access(vgic_its, offset, value,
						    is_high_word, is_low_word);
	}

	vgic_its_reg_info_t reg_info = vgic_its_read_write_access(
		vgic_its, offset, is_write, write_value);
	uint64_t read_value = reg_info.read_value;
	ret		    = reg_info.ret;

	// Update the value being read, for read accesses
	if (ret && !is_write) {
		if (is_high_word) {
			*value = read_value >> 32U;
		} else if (is_low_word) {
			*value = (uint32_t)read_value;
		} else {
			*value = read_value;
		}
	}

	if (!is_write) {
		TRACE(VGIC, INFO, "VITS {:#x} read {:#x} @ {:#x}/{:d}",
		      (uintptr_t)vgic_its, *value, offset, access_size);
	}

out:
	return ret ? VCPU_TRAP_RESULT_EMULATED : VCPU_TRAP_RESULT_FAULT;
}

vcpu_trap_result_t
vgic_its_base_handle_vdevice_access(vdevice_type_t type, vdevice_t *vdevice,
				    size_t offset, size_t access_size,
				    register_t *value, bool is_write)
{
	assert(type == VDEVICE_TYPE_VGIC_ITS);
	assert(vdevice != NULL);

	vgic_its_t *vgic_its = vgic_its_container_of_gits_device(vdevice);
	return vgic_its_access(vgic_its, offset, access_size, value, is_write);
}

vcpu_trap_result_t
vgic_its_base_handle_vdevice_access_fixed_addr(vmaddr_t ipa, size_t access_size,
					       register_t *value, bool is_write)
{
	vcpu_trap_result_t ret;

	if ((ipa >= PLATFORM_GITS_BASE) &&
	    (ipa <
	     PLATFORM_GITS_BASE + (VGIC_ITS_MAX_NUM << GITS_STRIDE_SHIFT))) {
		vic_t  *vic	     = thread_get_self()->vgic_vic;
		index_t vgic_its_num = (index_t)((ipa - PLATFORM_GITS_BASE) >>
						 GITS_STRIDE_SHIFT);
		size_t	offset = ipa - (size_t)PLATFORM_GITS_BASE -
				 ((size_t)vgic_its_num << GITS_STRIDE_SHIFT);

		if ((vic != NULL) && vic->allow_fixed_vmaddr) {
			assert(vgic_its_num <
			       util_array_size(vic->vgic_its_ptrs));

			vgic_its_t *vgic_its = atomic_load_consume(
				&vic->vgic_its_ptrs[vgic_its_num]);

			if (vgic_its != NULL) {
				ret = vgic_its_access(vgic_its, offset,
						      access_size, value,
						      is_write);
			} else {
				ret = VCPU_TRAP_RESULT_UNHANDLED;
			}
		} else {
			ret = VCPU_TRAP_RESULT_UNHANDLED;
		}
	} else {
		ret = VCPU_TRAP_RESULT_UNHANDLED;
	}

	return ret;
}
