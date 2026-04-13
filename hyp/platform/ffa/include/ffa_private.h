// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

/* Two FF-A component lists:
 * One for the Secure components, one for Non-Secure components.
 * The list of Secure components is enumerated right after the version
 * negotiations, and remains unchanged. Only the NS components list requires a
 * lock, as VMs can be created or destroyed at runtime.
 */
extern list_t	  ffa_secure_components_list;
extern list_t	  ffa_ns_components_list;
extern spinlock_t ffa_ns_components_list_lock;

/*
 * Iterate ffa_secure_components_list and return the secure component which
 * matches the part_id
 */
ffa_component_t *
ffa_get_secure_component(ffa_partition_id_t part_id);

/*
 * Sets the FF-A subsystem status to active or inactive.
 *
 * This function controls whether the hypervisor's FF-A implementation is
 * considered active. When active, FF-A calls from VMs will be processed;
 * when inactive, they will be rejected.
 */
void
ffa_set_active(bool active);

/*
 * Checks if the FF-A subsystem is currently active.
 *
 * Returns true if the SPMC (Secure Partition Manager Core) is present in
 * the trusted zone and its FF-A version is compatible with the hypervisor's
 * FF-A implementation. This determines whether FF-A calls should be processed.
 *
 * Returns true if FF-A is active and available, false otherwise
 */
bool
ffa_is_active(void);

/*
 * Executes an FF-A SMCCC call to the SPMC.
 *
 * This function performs the actual SMC call to communicate with the SPMC in
 * the secure world. It validates that the call uses the fast call convention
 * (as required by FF-A) and forwards it using SMCCC 1.2 protocol.
 *
 * fn_id: The SMCCC function ID for the FF-A call
 * args: Array of 17 argument registers (x1-x17)
 * ret: Array of 18 return registers (x0-x17) to store results
 */
void
ffa_smccc_call(smccc_function_id_t fn_id,
	       const register_t (*args)[SMCCC_1_2_ARGS],
	       register_t (*ret)[SMCCC_1_2_RETS]);

/*
 * Executes an FF-A SMCCC call which allocates CPU cycles to a SP.
 *
 * This function performs the actual SMC call to communicate with the SPMC in
 * the secure world, which in turn forwards the call to the relevant SP. It
 * validates that the call uses the fast call convention (as required by FF-A)
 * and forwards it using SMCCC 1.2 protocol.
 *
 * fn_id: The SMCCC function ID for the FF-A call
 * args: Array of 17 argument registers (x1-x17)
 * ret: Array of 18 return registers (x0-x17) to store results
 * src_id: Partition ID of the caller.
 */
void
ffa_smccc_run(smccc_function_id_t fn_id,
	      const register_t (*args)[SMCCC_1_2_ARGS],
	      register_t (*ret)[SMCCC_1_2_RETS], ffa_partition_id_t src_id);

/*
 * Executes an FF-A SMCCC call which sends a direct request to a SP.
 *
 * This function performs the actual SMC call to communicate with the SPMC in
 * the secure world, which in turn forwards the call to the relevant SP. It
 * validates that the call uses the fast call convention (as required by FF-A)
 * and forwards it using SMCCC 1.2 protocol.
 *
 * fn_id: The SMCCC function ID for the FF-A call
 * args: Array of 17 argument registers (x1-x17)
 * ret: Array of 18 return registers (x0-x17) to store results
 */
void
ffa_smccc_direct_req(smccc_function_id_t fn_id,
		     const register_t (*args)[SMCCC_1_2_ARGS],
		     register_t (*ret)[SMCCC_1_2_RETS]);

/*
 * Dispatches 32-bit FF-A function calls to their respective handlers.
 *
 * This function parses the FF-A function ID and routes supported 32-bit FF-A
 * calls to their specific handler functions. It also marks the VM's FF-A
 * version as negotiated for non-VERSION calls.
 *
 * Supported functions: FFA_VERSION, FFA_FEATURES, FFA_ID_GET,
 * FFA_MSG_SEND_DIRECT_REQ, FFA_RUN, FFA_SPM_ID_GET
 *
 * fn_id: The SMCCC function ID containing the FF-A function
 * args: Array of 17 argument registers (x1-x17)
 * ret: Array of 18 return registers (x0-x17) to store results
 */
void
ffa_call_handle_functions_32(smccc_function_id_t fn_id,
			     const register_t (*args)[SMCCC_1_2_ARGS],
			     register_t (*ret)[SMCCC_1_2_RETS])
	EXCLUDE_PREEMPT_DISABLED;

/*
 * Dispatches 64-bit FF-A function calls to their respective handlers.
 *
 * This function parses the FF-A function ID and routes supported 64-bit FF-A
 * calls to their specific handler functions. It also marks the VM's FF-A
 * version as negotiated for non-VERSION calls.
 *
 * Supported functions: FFA_MSG_SEND_DIRECT_REQ, FFA_MSG_SEND_DIRECT_REQ2,
 * FFA_PARTITION_INFO_GET_REGS
 *
 * fn_id: The SMCCC function ID containing the FF-A function
 * args: Array of 17 argument registers (x1-x17)
 * ret: Array of 18 return registers (x0-x17) to store results
 */
void
ffa_call_handle_functions_64(smccc_function_id_t fn_id,
			     const register_t (*args)[SMCCC_1_2_ARGS],
			     register_t (*ret)[SMCCC_1_2_RETS])
	EXCLUDE_PREEMPT_DISABLED;

/*
 * Retrieves the hypervisor's supported FF-A version.
 */
ffa_version_t
ffa_get_hyp_version(void);

/*
 * Sets the hypervisor's FF-A version.
 *
 * This is typically called during initialization to establish the hypervisor's
 * FF-A version capabilities.
 */
void
ffa_set_hyp_version(ffa_version_t version);

/*
 * Prepares a 32-bit FF-A error response in the return registers.
 *
 * This helper function formats the return registers to contain an
 * FFA_ERROR response with the specified error code, following the
 * 32-bit FF-A calling convention.
 *
 * err: The FF-A error code to return
 * ret: Array of 18 return registers to populate (w0-w2 used)
 */
void
ffa_set_error(ffa_ret_t err, register_t (*ret)[SMCCC_1_2_RETS]);

/*
 * Prepares a 32-bit FF-A success response in the return registers.
 *
 * This helper function formats the return registers to contain an FFA_SUCCESS
 * response, following the 32-bit FF-A calling convention.
 *
 * ret: Array of 18 return registers to populate (w0-w1 used)
 */
void ffa_success(register_t (*ret)[SMCCC_1_2_RETS]);

/*
 * Prepares a 64-bit FF-A success response in the return registers.
 *
 * This helper function formats the return registers to contain an FFA_SUCCESS
 * response, following the 64-bit FF-A calling convention.
 *
 * ret: Array of 18 return registers to populate (x0-x1 used)
 */
void ffa_success64(register_t (*ret)[SMCCC_1_2_RETS]);

/*
 * Handles the FFA_VERSION function call for FF-A version negotiation.
 *
 * This function implements the FF-A version negotiation protocol. It validates
 * the VM's requested version against the hypervisor's supported version range
 * and stores the negotiated version in the VM's address space.
 * Returns the hypervisor's version or an error if incompatible.
 *
 * arg1: The VM's requested FF-A version (from x1/w1)
 * ret0: Pointer to return register x0/w0 for the response
 */
void
ffa_call_handle_ffa_version(const register_t (*args)[SMCCC_1_2_ARGS],
			    register_t (*ret)[SMCCC_1_2_RETS])
	EXCLUDE_PREEMPT_DISABLED;

/*
 * Handles the FFA_ID_GET function call to retrieve the VM's partition ID.
 *
 * This function returns the calling VM's FF-A partition ID, which is
 * derived from its VMID. The response is formatted as an FFA_SUCCESS
 * with the partition ID in the appropriate register.
 *
 * ret: Array of 18 return registers to populate with the response
 */
void ffa_call_handle_ffa_id_get(register_t (*ret)[SMCCC_1_2_RETS]);

/*
 * Handles the FFA_PARTITION_INFO_GET_REGS function call.
 *
 * This function retrieves information about FF-A partitions
 * that match the specified UUID. It supports pagination through start_index
 * and can return up to 5 partition descriptors per call. The information
 * includes partition ID, execution context count, and properties.
 *
 * args: Array of 17 argument registers containing FF-A UUID and pagination info
 * ret: Array of 18 return registers to populate with partition info
 */
void
ffa_call_handle_ffa_partition_info_get_regs(
	const register_t (*args)[SMCCC_1_2_ARGS],
	register_t (*ret)[SMCCC_1_2_RETS]);

/*
 * Handles the FFA_PARTITION_INFO_GET function call.
 *
 * This function retrieves information about FF-A partitions
 * that match the specified UUID. The information includes partition ID,
 * execution context count, and properties. The descriptors are populated in
 * the RX buffer of the VM.
 *
 * args: Array of 17 argument registers containing FF-A UUID and flag info
 * ret: Array of 18 return registers to populate with the response
 */
void
ffa_call_handle_ffa_partition_info_get(const register_t (*args)[SMCCC_1_2_ARGS],
				       register_t (*ret)[SMCCC_1_2_RETS]);

/*
 * Checks if a 64-bit FF-A function is supported by the hypervisor.
 *
 * This function determines whether the hypervisor implements support for the
 * specified 64-bit FF-A function. Used by FFA_FEATURES to report capability
 * support.
 *
 * function: The FF-A function ID to check
 * Returns true if the 64-bit variant is supported, false otherwise
 */
bool
ffa_call_supported_64(smccc_function_t function);

/*
 * Checks if a 32-bit FF-A function is supported by the hypervisor.
 *
 * This function determines whether the hypervisor implements support for the
 * specified 32-bit FF-A function. Used by FFA_FEATURES to report capability
 * support.
 *
 * function: The FF-A function ID to check
 * Returns true if the 32-bit variant is supported, false otherwise
 */
bool
ffa_call_supported_32(smccc_function_t function);

/*
 * Handles the FFA_FEATURES function call for capability discovery.
 *
 * This function allows VMs to query which FF-A functions and features
 * are supported by the hypervisor. It distinguishes between function IDs
 * (with FAST bit set) and feature IDs, validates the request, and either
 * forwards to the SPMC or returns NOT_SUPPORTED.
 *
 * fn_id: The SMCCC function ID for the FFA_FEATURES call
 * args: Array of 17 argument registers containing the query
 * ret: Array of 18 return registers to populate with the response
 */
void
ffa_call_handle_ffa_features(smccc_function_id_t fn_id,
			     const register_t (*args)[SMCCC_1_2_ARGS],
			     register_t (*ret)[SMCCC_1_2_RETS]);

/*
 * Handles FF-A direct messaging function calls.
 *
 * This function processes FFA_MSG_SEND_DIRECT_REQ and FFA_MSG_SEND_DIRECT_REQ2
 * calls for direct communication between partitions. It validates the source
 * partition ID matches the calling VM and forwards secure partition messages
 * to the SPMC. Non-secure direct messaging is not currently supported.
 *
 * fn_id: The SMCCC function ID (32-bit or 64-bit direct message variant)
 * args: Array of 17 argument registers containing message parameters
 * ret: Array of 18 return registers to populate with the response
 */
void
ffa_call_handle_ffa_msg_send_direct(smccc_function_id_t fn_id,
				    const register_t (*args)[SMCCC_1_2_ARGS],
				    register_t (*ret)[SMCCC_1_2_RETS]);

/*
 * Handles the FFA_RUN function call for partition scheduling.
 *
 * This function processes FFA_RUN calls that are used to schedule and resume
 * execution of FF-A partitions. It validates that the source partition ID
 * matches the calling VM and forwards the call to the SPMC for actual partition
 * scheduling.
 *
 * fn_id: The SMCCC function ID for the FFA_RUN call
 * args: Array of 17 argument registers containing run parameters
 * ret: Array of 18 return registers to populate with the response
 */
void
ffa_call_handle_ffa_run(smccc_function_id_t fn_id,
			const register_t (*args)[SMCCC_1_2_ARGS],
			register_t (*ret)[SMCCC_1_2_RETS]);

/*
 * Converts a hypervisor VMID to an FF-A partition ID.
 *
 * This helper function maps the hypervisor's internal VMID representation to
 * the FF-A partition ID format. The VMID is used directly as the partition ID,
 * limited to the first 15 bits.
 *
 * vmid: The hypervisor VMID to convert
 * Returns the corresponding FF-A partition ID
 */
ffa_partition_id_t
ffa_vmid_to_partid(vmid_t vmid);

/*
 * Converts an FF-A partition ID to a hypervisor VMID.
 *
 * This helper function extracts the hypervisor VMID from an FF-A partition ID
 * by retrieving the partition ID field.
 *
 * part_id: The FF-A partition ID to convert
 * Returns the corresponding hypervisor VMID
 */
vmid_t
ffa_partid_to_vmid(ffa_partition_id_t part_id);

/*
 * Informs secure components about the creation or destruction of a VM.
 *
 * This function iterates through the list of secure components and sends a
 * direct message to each component that has requested to be notified about
 * VM creation or destruction events.
 *
 * vmid: The ID of the VM being created or destroyed.
 * is_create: True if the VM is being created, false if it is being destroyed.
 */
void
ffa_vm_availability(vmid_t vmid, bool is_create) EXCLUDE_PREEMPT_DISABLED;

/*
 * Handles the destruction of a VM.
 *
 * This function is called when a VM is being destructed. It informs secure
 * components about the VM destruction event and performs any necessary cleanup
 * operations related to FF-A functionality.
 *
 * addrspace: The address space of the VM being destructed.
 * ret: return OK if the VM was sdestructed; otherwise, an error code.
 */
error_t
ffa_vm_destruction(addrspace_t *addrspace) EXCLUDE_PREEMPT_DISABLED;

/* Handles the FFA_RXTX_MAP function calls.
 *
 * This function processes FF-A calls related to mapping RXTX buffers for
 * communication between VM and Hypervisor. The Hypervisor does not forward
 * the FFA_RXTX_MAP request to the SPMC.
 *
 * Note that messages are transmitted between the VM and SP by copying them
 * from the sender's TX buffer to the receiver's RX buffer. This operation
 * is performed by the SPMC and applies only to indirect messaging.
 * Since indirect messaging is currently unsupported, the SPMC has no
 * requirement to know the buffer mapping. Consequently, the Hypervisor does
 * not forward this mapping to the SPMC.
 *
 * fn_id: The SMCCC function ID for the FFA_RXTX_MAP call
 * args: Array of 17 argument registers containing mapping parameters
 * ret: Array of 18 return registers to populate with the response
 */
void
ffa_call_handle_ffa_rxtx_map(smccc_function_id_t fn_id,
			     const register_t (*args)[SMCCC_1_2_ARGS],
			     register_t (*ret)[SMCCC_1_2_RETS]);

/*
 * Handles the FFA_RXTX_UNMAP function calls.
 *
 * This function processes FF-A calls related to unmapping RXTX buffers.
 *
 * fn_id: The SMCCC function ID for the FFA_RXTX_UNMAP call
 * args: Array of 17 argument registers containing unmapping parameters
 * ret: Array of 18 return registers to populate with the response
 */
void
ffa_call_handle_ffa_rxtx_unmap(smccc_function_id_t fn_id,
			       const register_t (*args)[SMCCC_1_2_ARGS],
			       register_t (*ret)[SMCCC_1_2_RETS]);

/*
 * Handles FFA_RX_RELEASE function calls.
 *
 * This function releases the ownership back to hypervisor. VM must call RELEASE
 * after finishing accessing RX buffer.
 *
 * fn_id: The SMCCC function ID for the FF-A RX common call
 * args: Array of 17 argument registers
 * ret: Array of 18 return registers to populate with the response
 */
void
ffa_call_handle_ffa_rx_release(smccc_function_id_t fn_id,
			       const register_t (*args)[SMCCC_1_2_ARGS],
			       register_t (*ret)[SMCCC_1_2_RETS]);

/*
 * Checks whether an FF-A RXTX buffer is valid.
 *
 * This function checks whether the provided FF-A RXTX buffer is valid.
 * If the buffer size is zero which means the buffer is not mapped, it
 * is not valid.
 *
 * rxtx_buffer: Pointer to the FF-A RXTX buffer structure to validate.
 * Returns true if the buffer is valid, false otherwise.
 */
bool
ffa_rxtx_buffer_is_valid(ffa_rxtx_buffer_t *rxtx_buffer)
	REQUIRE_SPINLOCK(rxtx_buffer -> lock);

size_t
ffa_rxtx_buffer_size(ffa_rxtx_buffer_t *rxtx_buffer)
	REQUIRE_SPINLOCK(rxtx_buffer -> lock);

/*
 * Retrieve the Hypervisor mapped RX buffer address.
 */
uint8_t *
ffa_rxtx_buffer_get_rx(ffa_rxtx_buffer_t *rxtx_buffer)
	REQUIRE_SPINLOCK(rxtx_buffer -> lock);

/*
 * Retrieve the Hypervisor mapped TX buffer address.
 */
uint8_t *
ffa_rxtx_buffer_get_tx(ffa_rxtx_buffer_t *rxtx_buffer)
	REQUIRE_SPINLOCK(rxtx_buffer -> lock);

/*
 * Retrieve the Hypervisor's private copy of the TX buffer. is_tx_busy must
 * be already set before calling.
 */
uint8_t *
ffa_rxtx_buffer_get_hyp_private(ffa_rxtx_buffer_t *rxtx_buffer);

/*
 * Checks whether the hypervisor is the owner of the RX buffer.
 *
 * This function checks whether the hypervisor is the owner of the
 * RX buffer in the provided FF-A RXTX buffer.
 *
 * rxtx_buffer: Pointer to the FF-A RXTX buffer structure to check.
 * Returns true if the hypervisor is the owner, false otherwise.
 */
bool
ffa_rxtx_buffer_hyp_is_rx_owner(ffa_rxtx_buffer_t *rxtx_buffer)
	REQUIRE_SPINLOCK(rxtx_buffer -> lock);

/*
 * Set the hypervisor ownership status of the RX buffer.
 */
void
ffa_rxtx_buffer_set_hyp_is_rx_owner(ffa_rxtx_buffer_t *rxtx_buffer,
				    bool	       hyp_owned)
	REQUIRE_SPINLOCK(rxtx_buffer -> lock);

/*
 * Checks whether the TX buffer is in use.
 */
bool
ffa_rxtx_buffer_is_tx_busy(ffa_rxtx_buffer_t *rxtx_buffer)
	REQUIRE_SPINLOCK(rxtx_buffer -> lock);

/*
 * Set the hypervisor ownership status of the VM's TX buffer.
 */
void
ffa_rxtx_buffer_set_is_tx_busy(ffa_rxtx_buffer_t *rxtx_buffer, bool tx_busy)
	REQUIRE_SPINLOCK(rxtx_buffer -> lock);

/*
 * Retrieves the configuration of the FF-A RXTX buffers.
 *
 * This function provides information about the total size and alignment
 * requirements for the RXTX buffers.
 *
 * Returns a structure containing the RXTX buffer information.
 */
ffa_rxtx_features_buffer_info_t
ffa_get_rxtx_buffer_sizes(void);

/*
 * Retrieves the maximum size allowed for an FF-A RXTX buffer.
 *
 * This function returns the maximum supported size for the RX or TX buffer.
 * RX and TX buffer maximum sizes are always identical.
 *
 * Returns the maximum size of an FF-A RX or TX buffer in bytes.
 */
size_t
ffa_get_ffa_rxtx_max_buffer_size(void);

/*
 * Retrieves the minimum alignment requirement for an FF-A RXTX buffer.
 *
 * This function returns the minimum memory alignment (in bytes) that must be
 * satisfied when allocating FF-A RX or TX buffers. RX and TX buffer alignment
 * requirements are always identical.
 *
 * Returns the minimum alignment in bytes.
 */
size_t
ffa_get_ffa_rxtx_min_alignment(void);

/*
 * Creates and initializes the hypervisor's own FF-A RXTX buffer.
 *
 * This function allocates and sets up the memory region to be used by the
 * hypervisor for its FF-A communication buffers with the SPMC.
 *
 * page_count: The number of memory pages to allocate for the buffer.
 * Returns True if the buffer was successfully created, false otherwise.
 */
bool
ffa_create_hyp_rxtx_buffer(count_t page_count);

/*
 * Retrieves the hypervisor's TX buffer pointer and size.
 */
void
ffa_hyp_tx_buffer_get_tx(uint8_t **ffa_hyp_tx_buffer_ptr,
			 size_t	  *ffa_hyp_rxtx_buffer_size_ptr);

/*
 * Acquires exclusive access to the hypervisor's TX buffer.
 *
 * This function attempts to acquire exclusive access to the hypervisor's
 * TX buffer for communication with the SPMC. It uses atomic operations to
 * ensure thread-safe acquisition without blocking other CPUs.
 *
 * Returns true if the buffer was successfully acquired, false if it is
 * already in use by another CPU.
 */
bool
ffa_acquire_hyp_tx_buffer(void);

/*
 * Releases exclusive access to the hypervisor's TX buffer.
 *
 * This function releases the hypervisor's TX buffer after use, allowing
 * other CPUs to acquire it. It uses atomic operations to ensure thread-safe
 * release.
 */
void
ffa_release_hyp_tx_buffer(void);

/*
 * Frees the VM's FF-A RXTX buffer.
 *
 * This function releases the memory allocated for the VM's FF-A
 * RXTX buffer.
 *
 * addrspace: Pointer to the VM's address space.
 * is_shutdown: Indicates if this is called during VM shutdown.
 * ret: return OK if the buffer was freed; otherwise, an error code.
 */
error_t
ffa_free_vm_rxtx_buffer(addrspace_t *addrspace, bool is_shutdown);

/*
 * Acquires exclusive access to the TX buffer.
 *
 * This function safely retrieves the TX buffer from the provided FF-A RXTX
 * buffer structure, ensuring that no others can access it during the operation.
 * Note that the "tx_buffer_hyp_private" should be the only way for accessing
 * the TX buffer.
 *
 * rxtx_buffer: Pointer to the FF-A RXTX buffer structure.
 * len: The length of the fragment to be copied.
 * Returns True if the buffer was successfully acquired, false otherwise.
 */
bool
ffa_acquire_tx_buffer(ffa_rxtx_buffer_t *rxtx_buffer, size_t len);

/*
 * Releases exclusive acess to the TX buffer access.
 *
 * This function releases exclusive acess to the TX buffer.
 *
 * rxtx_buffer: Pointer to the FF-A RXTX buffer structure.
 * len: The length of the fragment to be zeroed.
 */
void
ffa_release_tx_buffer(ffa_rxtx_buffer_t *rxtx_buffer, size_t len);

/*
 * Handles FF-A memory lend and share operations.
 *
 * This function processes memory sharing or lending requests from a VM,
 * validates the transaction, and forwards it to the SPMC. It supports both
 * single and fragmented transfers. For non-identity mapped memory, it performs
 * IPA-to-PA translation before sending the memory descriptor to the SPMC.
 *
 * fn_id The SMCCC function ID of the request.
 * args An array of registers containing the call arguments.
 * ret An array of registers to store the return values.
 */
void
ffa_call_handle_ffa_mem_lend_share(smccc_function_id_t fn_id,
				   const register_t (*args)[SMCCC_1_2_ARGS],
				   register_t (*ret)[SMCCC_1_2_RETS]);

/*
 * Handles the transmission of a memory transaction fragment.
 *
 * This function is invoked by a VM to send subsequent fragments of a memory
 * transaction initiated via FFA_MEM_LEND or FFA_MEM_SHARE. It validates the
 * fragment, updates the transaction state, and forwards the data to the SPMC.
 * Fragmented sharing is only supported for identity-mapped memory regions.
 *
 * @param fn_id The SMCCC function ID for the fragment transmission.
 * @param args An array of registers containing the call arguments, including
 *             the memory handle and fragment length.
 * @param ret An array of registers for the return values from the call.
 */
void
ffa_call_handle_ffa_mem_frag_tx(smccc_function_id_t fn_id,
				const register_t (*args)[SMCCC_1_2_ARGS],
				register_t (*ret)[SMCCC_1_2_RETS]);

/*
 * Handles an FF-A memory reclaim request.
 *
 * This function is called by a VM to reclaim a memory region previously
 * lent or shared. It validates the handle, updates the transaction state
 * to RECLAIMING, and forwards the request to the SPMC. The hypervisor
 * trusts the SPMC to manage the reclaim process with all borrowers and
 * finalizes the cleanup only after the SPMC confirms completion.
 *
 * fn_id The SMCCC function ID for the reclaim operation.
 * args An array of registers containing the call arguments, including
 *             the memory handle to be reclaimed.
 * ret An array of registers for the return values from the call.
 */
void
ffa_call_handle_ffa_mem_reclaim(smccc_function_id_t fn_id,
				const register_t (*args)[SMCCC_1_2_ARGS],
				register_t (*ret)[SMCCC_1_2_RETS]);

/*
 * Cleans up all FF-A memory transactions for a VM.
 *
 * This function reclaims all shared memory by forwarding FFA_MEM_RECLAIM
 * calls to the SPMC during VM shutdown.
 *
 * addrspace: Pointer to the address space being cleaned up.
 * Returns OK on success, ERROR_RETRY if a transaction is busy, or other errors.
 */
error_t
ffa_memory_cleanup(addrspace_t *addrspace);
