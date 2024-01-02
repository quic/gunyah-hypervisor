// © 2023 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

error_t
set_data_sel_ev_bits(const virtio_mmio_t *virtio_mmio, uint32_t subsel,
		     uint32_t size, vmaddr_t data);
error_t
set_data_sel_abs_info(const virtio_mmio_t *virtio_mmio, uint32_t subsel,
		      uint32_t size, vmaddr_t data);
