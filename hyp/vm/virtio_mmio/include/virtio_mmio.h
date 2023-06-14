// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

error_t
virtio_mmio_configure(virtio_mmio_t *virtio_mmio, memextent_t *memextent,
		      count_t vqs_num);

error_t
virtio_mmio_backend_bind_virq(virtio_mmio_t *virtio_mmio, vic_t *vic,
			      virq_t virq);

void
virtio_mmio_backend_unbind_virq(virtio_mmio_t *virtio_mmio);

error_t
virtio_mmio_frontend_bind_virq(virtio_mmio_t *virtio_mmio, vic_t *vic,
			       virq_t virq);

void
virtio_mmio_frontend_unbind_virq(virtio_mmio_t *virtio_mmio);
