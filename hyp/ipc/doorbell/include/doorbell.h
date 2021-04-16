// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Sets flags of the doorbell. Returns old flags
doorbell_flags_result_t
doorbell_send(doorbell_t *doorbell, doorbell_flags_t new_flags);

// Reads and clears the flags of the doorbell. Returns old flags.
doorbell_flags_result_t
doorbell_receive(doorbell_t *doorbell, doorbell_flags_t clear_flags);

// Clears all flags and sets all bits in the mask of the doorbell.
error_t
doorbell_reset(doorbell_t *doorbell);

// Sets the masks of the doorbell. The Enable Mask is the mask of set flags
// that will cause an assertion of the virtual interrupt bound to the doorbell.
// The Ack Mask controls which flags should be automatically cleared when the
// interrupt is asserted.
error_t
doorbell_mask(doorbell_t *doorbell, doorbell_flags_t enable_mask,
	      doorbell_flags_t ack_mask);

// Binds a Doorbell to a virtual interrupt.
error_t
doorbell_bind(doorbell_t *doorbell, vic_t *vic, virq_t virq);

// Unbinds a Doorbell from a virtual interrupt.
void
doorbell_unbind(doorbell_t *doorbell);
