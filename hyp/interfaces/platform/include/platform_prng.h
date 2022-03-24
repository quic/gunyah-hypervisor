// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

error_t
platform_get_serial(uint32_t data[4]);

error_t
platform_get_random32(uint32_t *data);

error_t
platform_get_rng_uuid(uint32_t data[4]);

error_t
platform_get_entropy(platform_prng_data256_t *data);
