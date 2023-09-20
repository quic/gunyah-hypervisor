// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// rootvm_init() is allowed to call partition_get_root().
#define ROOTVM_INIT 1

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <attributes.h>
#include <bitmap.h>
#include <cpulocal.h>
#include <cspace.h>
#include <memdb.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <partition_init.h>
#include <platform_mem.h>
#include <qcbor.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <util.h>
#include <vcpu.h>

#include <events/object.h>
#include <events/rootvm.h>

#include <asm/cache.h>
#include <asm/cpu.h>

#include "boot_init.h"
#include "event_handlers.h"

// FIXME: remove when we have a device tree where to read it from
// dummy value.
#define MAX_CAPS 2048

static void
copy_rm_env_data_to_rootvm_mem(hyp_env_data_t		hyp_env,
			       const rm_env_data_hdr_t *rm_env_data,
			       rt_env_data_t *crt_env, uint32_t env_data_size)
{
	paddr_t hyp_env_phys = hyp_env.env_ipa - hyp_env.me_ipa_base +
			       PLATFORM_ROOTVM_LMA_BASE;
	assert(util_is_baligned(hyp_env_phys, PGTABLE_VM_PAGE_SIZE));

	void *va = partition_phys_map(hyp_env_phys, env_data_size);
	partition_phys_access_enable(va);

	(void)memcpy(va, (void *)crt_env,
		     (rm_env_data->data_payload_size + sizeof(*rm_env_data) +
		      sizeof(*crt_env)));
	CACHE_CLEAN_RANGE((rm_env_data_hdr_t *)va,
			  (rm_env_data->data_payload_size +
			   sizeof(*rm_env_data) + sizeof(*crt_env)));

	partition_phys_access_disable(va);
	partition_phys_unmap(va, hyp_env_phys, env_data_size);
}

static void
rootvm_close_env_data(qcbor_enc_ctxt_t	*qcbor_enc_ctxt,
		      rm_env_data_hdr_t *rm_env_data)
{
	qcbor_err_t	    cb_err;
	const_useful_buff_t payload_out_buff;

	payload_out_buff.ptr = NULL;
	payload_out_buff.len = 0;

	cb_err = QCBOREncode_Finish(qcbor_enc_ctxt, &payload_out_buff);

	if (cb_err != QCBOR_SUCCESS) {
		panic("Env data encoding error, increase the buffer size");
	}

	rm_env_data->data_payload_size = (uint32_t)payload_out_buff.len;
}

typedef struct {
	hyp_env_data_t	   hyp_env;
	qcbor_enc_ctxt_t  *qcbor_enc_ctxt;
	rm_env_data_hdr_t *rm_env_data;
	rt_env_data_t	  *crt_env;
} rootvm_init_env_info;

static rootvm_init_env_info
rootvm_init_env_data(partition_t *root_partition, uint32_t env_data_size)
{
	void_ptr_result_t alloc_ret;
	hyp_env_data_t	  hyp_env; // Local on stack used as context
	qcbor_enc_ctxt_t *qcbor_enc_ctxt;

	rm_env_data_hdr_t *rm_env_data;
	rt_env_data_t	  *crt_env;
	uint32_t	   remaining_size;

	alloc_ret = partition_alloc(root_partition, env_data_size,
				    PGTABLE_VM_PAGE_SIZE);
	if (alloc_ret.e != OK) {
		panic("Allocate env_data failed");
	}
	crt_env = (rt_env_data_t *)alloc_ret.r;
	(void)memset_s(crt_env, env_data_size, 0, env_data_size);

	alloc_ret = partition_alloc(root_partition, sizeof(*qcbor_enc_ctxt),
				    alignof(*qcbor_enc_ctxt));
	if (alloc_ret.e != OK) {
		panic("Allocate cbor_ctxt failed");
	}

	qcbor_enc_ctxt = (qcbor_enc_ctxt_t *)alloc_ret.r;
	memset_s(qcbor_enc_ctxt, sizeof(*qcbor_enc_ctxt), 0,
		 sizeof(*qcbor_enc_ctxt));

	memset_s(&hyp_env, sizeof(hyp_env), 0, sizeof(hyp_env));

	hyp_env.env_data_size = env_data_size;
	remaining_size	      = env_data_size;

	crt_env->signature = ROOTVM_ENV_DATA_SIGNATURE;
	crt_env->version   = 1;

	size_t rm_config_offset =
		util_balign_up(sizeof(*crt_env), alignof(*rm_env_data));
	assert(remaining_size >= (rm_config_offset + sizeof(*rm_env_data)));

	remaining_size -= rm_config_offset;
	rm_env_data =
		(rm_env_data_hdr_t *)((uintptr_t)crt_env + rm_config_offset);

	crt_env->rm_config_offset = rm_config_offset;
	crt_env->rm_config_size	  = remaining_size;

	rm_env_data->signature		 = RM_ENV_DATA_SIGNATURE;
	rm_env_data->version		 = 1;
	rm_env_data->data_payload_offset = sizeof(*rm_env_data);
	rm_env_data->data_payload_size	 = 0U;

	remaining_size -= sizeof(*rm_env_data);

	useful_buff_t qcbor_data_buff;
	qcbor_data_buff.ptr =
		(((uint8_t *)rm_env_data) + rm_env_data->data_payload_offset);
	qcbor_data_buff.len = remaining_size;

	QCBOREncode_Init(qcbor_enc_ctxt, qcbor_data_buff);

	return (rootvm_init_env_info){
		.crt_env	= crt_env,
		.hyp_env	= hyp_env,
		.qcbor_enc_ctxt = qcbor_enc_ctxt,
		.rm_env_data	= rm_env_data,
	};
}

void NOINLINE
rootvm_init(void)
{
	static_assert(SCHEDULER_NUM_PRIORITIES >= (priority_t)3U,
		      "unexpected scheduler configuration");
	static_assert(ROOTVM_PRIORITY <= VCPU_MAX_PRIORITY,
		      "unexpected scheduler configuration");

	thread_create_t params = {
		.scheduler_affinity	  = cpulocal_get_index(),
		.scheduler_affinity_valid = true,
		.scheduler_priority	  = ROOTVM_PRIORITY,
		.scheduler_priority_valid = true,
	};

	partition_t *root_partition = partition_get_root();

	assert(root_partition != NULL);

	platform_add_root_heap(root_partition);

	// Create cspace for root partition
	cspace_create_t cs_params = { NULL };

	cspace_ptr_result_t cspace_ret =
		partition_allocate_cspace(root_partition, cs_params);
	if (cspace_ret.e != OK) {
		goto cspace_fail;
	}
	cspace_t *root_cspace = cspace_ret.r;

	spinlock_acquire_nopreempt(&root_cspace->header.lock);
	if (cspace_configure(root_cspace, MAX_CAPS) != OK) {
		spinlock_release_nopreempt(&root_cspace->header.lock);
		goto cspace_fail;
	}
	spinlock_release_nopreempt(&root_cspace->header.lock);

	if (object_activate_cspace(root_cspace) != OK) {
		goto cspace_fail;
	}

	trigger_object_get_defaults_thread_event(&params);

	// Allocate and setup the root thread
	thread_ptr_result_t thd_ret =
		partition_allocate_thread(root_partition, params);
	if (thd_ret.e != OK) {
		panic("Error allocating root thread");
	}
	thread_t *root_thread = (thread_t *)thd_ret.r;

	vcpu_option_flags_t vcpu_options = vcpu_option_flags_default();

	vcpu_option_flags_set_critical(&vcpu_options, true);

	if (vcpu_configure(root_thread, vcpu_options) != OK) {
		panic("Error configuring vcpu");
	}

	// Attach root cspace to root thread
	if (cspace_attach_thread(root_cspace, root_thread) != OK) {
		panic("Error attaching cspace to root thread");
	}

	// Give the root cspace a cap to itself
	object_ptr_t obj_ptr;

	obj_ptr.cspace		  = root_cspace;
	cap_id_result_t capid_ret = cspace_create_master_cap(
		root_cspace, obj_ptr, OBJECT_TYPE_CSPACE);
	if (capid_ret.e != OK) {
		goto cspace_fail;
	}

	uint32_t env_data_size = 0x4000;

	rootvm_init_env_info info =
		rootvm_init_env_data(root_partition, env_data_size);

	hyp_env_data_t	  hyp_env	 = info.hyp_env;
	qcbor_enc_ctxt_t *qcbor_enc_ctxt = info.qcbor_enc_ctxt;

	rm_env_data_hdr_t *rm_env_data = info.rm_env_data;
	rt_env_data_t	  *crt_env     = info.crt_env;

	QCBOREncode_OpenMap(qcbor_enc_ctxt);

	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "cspace_capid", capid_ret.r);

	// Take extra reference so that the deletion of the master cap does not
	// accidentally destroy the partition.
	root_partition = object_get_partition_additional(root_partition);

	// Create caps for the root partition and thread
	obj_ptr.partition = root_partition;
	capid_ret	  = cspace_create_master_cap(root_cspace, obj_ptr,
						     OBJECT_TYPE_PARTITION);
	if (capid_ret.e != OK) {
		panic("Error creating root partition cap");
	}
	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "partition_capid",
				   capid_ret.r);

	obj_ptr.thread = root_thread;
	capid_ret      = cspace_create_master_cap(root_cspace, obj_ptr,
						  OBJECT_TYPE_THREAD);
	if (capid_ret.e != OK) {
		panic("Error creating root partition cap");
	}
	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "vcpu_capid", capid_ret.r);
	crt_env->vcpu_capid = capid_ret.r;

	// Do a memdb walk to get all the available memory ranges of the root
	// partition and save in the rm_env_data
	if (boot_add_free_range((uintptr_t)root_partition, MEMDB_TYPE_PARTITION,
				qcbor_enc_ctxt) != OK) {
		panic("Error doing the memory database walk");
	}

	trigger_rootvm_init_event(root_partition, root_thread, root_cspace,
				  &hyp_env, qcbor_enc_ctxt);

	QCBOREncode_CloseMap(qcbor_enc_ctxt);

	rootvm_close_env_data(qcbor_enc_ctxt, rm_env_data);

	crt_env->runtime_ipa   = hyp_env.runtime_ipa;
	crt_env->app_ipa       = hyp_env.app_ipa;
	crt_env->app_heap_ipa  = hyp_env.app_heap_ipa;
	crt_env->app_heap_size = hyp_env.app_heap_size;
	crt_env->timer_freq    = hyp_env.timer_freq;
	crt_env->gicd_base     = hyp_env.gicd_base;
	crt_env->gicr_base     = hyp_env.gicr_base;

	// Copy the rm_env_data to the root VM memory
	copy_rm_env_data_to_rootvm_mem(hyp_env, rm_env_data, crt_env,
				       env_data_size);

	// Setup the root VM thread
	if (object_activate_thread(root_thread) != OK) {
		panic("Error activating root thread");
	}

	trigger_rootvm_init_late_event(root_partition, root_thread, root_cspace,
				       &hyp_env);

	scheduler_lock_nopreempt(root_thread);
	// FIXME: eventually pass as dtb, for now the rm_env_data ipa is passed
	// directly.
	bool_result_t power_ret =
		vcpu_poweron(root_thread, vmaddr_result_ok(hyp_env.entry_ipa),
			     register_result_ok(hyp_env.env_ipa));
	if (power_ret.e != OK) {
		panic("Error vcpu poweron");
	}

	// Allow other modules to clean up after root VM creation.
	trigger_rootvm_started_event(root_thread);
	scheduler_unlock_nopreempt(root_thread);
	(void)partition_free(root_partition, crt_env, env_data_size);
	rm_env_data = NULL;

	return;

cspace_fail:
	panic("Error creating root cspace cap");
}
