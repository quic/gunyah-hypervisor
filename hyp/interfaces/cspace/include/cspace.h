// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Get a pointer to the thread's cspace.
// This does not take a reference to the cspace since it is not valid to change
// the cspace of a running thread.
cspace_t *
cspace_get_self(void);

// Lookup an object from a cap in a cspace. The object must be of the specified
// type. If active_only is true, the lookup will check the object state, and
// fail if it is not OBJECT_STATE_ACTIVE; checking the object state is the
// caller's responsibility otherwise. If successful, an additional reference to
// the object will be obtained.
object_ptr_result_t
cspace_lookup_object(cspace_t *cspace, cap_id_t cap_id, object_type_t type,
		     cap_rights_t rights, bool active_only);

// Lookup an object from a cap in a cspace. The object may be of any type, and
// in any state. It is the caller's responsibility to provide locking of the
// object header and to check the object state if required. If successful, an
// additional reference to the object will be obtained.
object_ptr_result_t
cspace_lookup_object_any(cspace_t *cspace, cap_id_t cap_id,
			 cap_rights_generic_t rights, object_type_t *type);

// Create the master cap for an object. The cspace will adopt the reference
// count of the object that was set when initialized after allocation.
cap_id_result_t
cspace_create_master_cap(cspace_t *cspace, object_ptr_t object,
			 object_type_t type);

cap_id_result_t
cspace_copy_cap(cspace_t *target_cspace, cspace_t *parent_cspace,
		cap_id_t parent_id, cap_rights_t rights_mask);

error_t
cspace_delete_cap(cspace_t *cspace, cap_id_t cap_id);

error_t
cspace_revoke_caps(cspace_t *cspace, cap_id_t master_cap_id);

// Configure the cspace.
// The object's header lock must be held and object state must be
// OBJECT_STATE_INIT.
error_t
cspace_configure(cspace_t *cspace, count_t max_caps);

error_t
cspace_attach_thread(cspace_t *cspace, thread_t *thread);
