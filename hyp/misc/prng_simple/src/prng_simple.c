// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// This PRNG implements a "fast-key-erasure RNG" as described by D.J.Bernstein
// https://blog.cr.yp.to/20170723-random.html
//
// The algorithm ensures that the RNG won't contribute to any failure of
// forward security of its clients. Random data is generated into a buffer
// using a key, then the key used is immediately destroyed, and a new key from
// the first output block is created.
//
// Requests for randomness return data from the buffer. When the buffer is
// exhausted, new randomness is generated, with another new key being generated
// as described above. Additionally, the random bytes returned are cleared from
// the buffer for similar forward security reasons.
//
// This implementation uses the block function from the ChaCha20 stream cipher
// which is used to generate a pseudo-random bitstream in counter mode, and is
// much faster than alternative approaches, such as hash/HMAC based DRGBs, and
// counter-cipher schemes such as AES-CTR-DRBG (which don't immediately destroy
// the key).
//
// Finally, randomness from a HW RNG is added to the key periodically. An
// update timestamp is maintained, and when requesting randomness, if the last
// update was more than 5 minutes go, new randomness is added.

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypregisters.h>

#include <bootmem.h>
#include <compiler.h>
#include <log.h>
#include <panic.h>
#include <platform_prng.h>
#include <platform_timer.h>
#include <prng.h>
#include <spinlock.h>
#include <trace.h>
#include <util.h>

#include <asm/cache.h>
#include <asm/cpu.h>

#include "chacha20.h"
#include "event_handlers.h"

#define WORD_BITS	   32U
#define BLOCK_WORDS	   (512U / WORD_BITS)
#define KEY_WORDS	   (256U / WORD_BITS)
#define BUFFER_KEY_OFFSET  0U
#define BUFFER_DATA_OFFSET KEY_WORDS // first bytes are reserved for the key
#define BUFFER_BLOCKS	   4U
#define BUFFER_WORDS	   (BUFFER_BLOCKS * BLOCK_WORDS)

#define REKEY_TIMEOUT_NS ((uint64_t)300U * 1000000000U) // 300 seconds

extern uint32_t hypervisor_prng_seed[KEY_WORDS];
extern uint64_t hypervisor_prng_nonce;

#define CACHE_LINE_SIZE (1 << CPU_L1D_LINE_BITS)

typedef struct {
	uint32_t alignas(CACHE_LINE_SIZE) key[KEY_WORDS];

	ticks_t key_timestamp;
	ticks_t key_timeout;
	count_t pool_index; // index in units of words

	uint32_t nonce[3];

	uint32_t alignas(CACHE_LINE_SIZE)
		entropy_pool[BUFFER_BLOCKS][BLOCK_WORDS];
} prng_data_t;

static bool prng_initialized = false;

static spinlock_t   prng_lock;
static prng_data_t *prng_data PTR_PROTECTED_BY(prng_lock);

void
prng_simple_handle_boot_runtime_first_init(void)
{
	spinlock_init(&prng_lock);
	spinlock_acquire_nopreempt(&prng_lock);

	void_ptr_result_t ret;

	// Allocate boot entropy pool
	ret = bootmem_allocate(sizeof(prng_data_t), alignof(prng_data_t));
	if (ret.e != OK) {
		panic("unable to allocate boot entropy pool");
	}

	prng_data = (prng_data_t *)ret.r;
	assert(prng_data != NULL);

	(void)memset_s(prng_data, sizeof(*prng_data), 0, sizeof(*prng_data));

	prng_data->pool_index = BUFFER_WORDS; // Buffer is Empty
	(void)memscpy(&prng_data->key, sizeof(prng_data->key),
		      hypervisor_prng_seed, sizeof(prng_data->key));

	// Ensure no stale copies remain in ram
	assert(hypervisor_prng_seed != NULL);
	(void)memset_s(hypervisor_prng_seed, sizeof(hypervisor_prng_seed), 0,
		       sizeof(hypervisor_prng_seed));
	CACHE_CLEAN_INVALIDATE_OBJECT(hypervisor_prng_seed);

	prng_data->key_timestamp = platform_timer_get_current_ticks();
	prng_data->key_timeout =
		platform_timer_convert_ns_to_ticks(REKEY_TIMEOUT_NS);

	uint32_t serial[4];

	error_t err = platform_get_serial(serial);
	if (err != OK) {
		panic("unable to get serial number");
	}

	prng_data->nonce[0] = serial[0];
	prng_data->nonce[1] = serial[1];
	prng_data->nonce[2] = serial[2];

	// Add in some chip specific noise
	prng_data->nonce[1] ^= (uint32_t)(hypervisor_prng_nonce & 0xffffffffU);
	prng_data->nonce[2] ^= (uint32_t)(hypervisor_prng_nonce >> 32);

	// Ensure no stale copies remain in ram
	(void)memset_s(&hypervisor_prng_nonce, sizeof(hypervisor_prng_nonce), 0,
		       sizeof(hypervisor_prng_nonce));
	CACHE_CLEAN_INVALIDATE_OBJECT(hypervisor_prng_nonce);

	prng_initialized = true;
	spinlock_release_nopreempt(&prng_lock);
}

void
prng_simple_handle_boot_hypervisor_start(void)
{
	// FIXME:
	// Post boot prng_data protection
	//  * allocate an unmapped 4K page for the prng_data
	//  * Aarch64 PAN implementation:
	//    - map the page with EL2&0 user-rw permissions
	//    - enable PAN to access the prng_data
	//  * copy the boot prng_data to the new page and zero it afterwards
	//  * update prng_data pointer to new location
}

static bool
add_platform_entropy(void) REQUIRE_SPINLOCK(prng_lock)
{
	error_t ret;
	bool	success;
	platform_prng_data256_t new;

	ret = platform_get_entropy(&new);
	if (ret == OK) {
		// mix in new key entropy
		prng_data->key[0] ^= new.word[0];
		prng_data->key[1] ^= new.word[1];
		prng_data->key[2] ^= new.word[2];
		prng_data->key[3] ^= new.word[3];
		prng_data->key[4] ^= new.word[4];
		prng_data->key[5] ^= new.word[5];
		prng_data->key[6] ^= new.word[6];
		prng_data->key[7] ^= new.word[7];

		// Ensure no stale copy remains on the stack
		(void)memset_s(&new, sizeof(new), 0, sizeof(new));
		CACHE_CLEAN_INVALIDATE_OBJECT(new);

		success = true;
	} else if (ret == ERROR_BUSY) {
		LOG(DEBUG, INFO, "platform_get_entropy busy");
		success = false;
	} else {
		LOG(ERROR, WARN, "platform_get_entropy err: {:d}",
		    (register_t)ret);
		panic("Failed to get platform_get_entropy");
	}

	return success;
}

static void
prng_update(void) REQUIRE_SPINLOCK(prng_lock)
{
	uint32_t counter = 1U;
	count_t	 i;

	ticks_t now = platform_timer_get_current_ticks();

	// Add new key entropy periodically, this is not critical if platform
	// is busy, we'll try again next time.
	if ((now - prng_data->key_timestamp) > prng_data->key_timeout) {
		if (add_platform_entropy()) {
			prng_data->key_timestamp = now;
		}
	}

	// Generate a new set of blocks
	for (i = 0U; i < BUFFER_BLOCKS; i++) {
		chacha20_block(&prng_data->key, counter, &prng_data->nonce,
			       &prng_data->entropy_pool[i]);
		counter++;
	}
	// Nonce must not be repeated for the same key! Even though we re-key
	// below, we increment the nonce anyway!
	prng_data->nonce[0] += 1U;
	if (prng_data->nonce[0] == 0U) {
		// Addition overflow of nonce[0]
		prng_data->nonce[1] += 1U;
		if (prng_data->nonce[1] == 0U) {
			// Addition overflow of nonce[1]
			prng_data->nonce[2] += 1U;
		}
	}

	// Fast key update from block 0
	(void)memscpy(prng_data->key, sizeof(prng_data->key),
		      &prng_data->entropy_pool[0],
		      sizeof(prng_data->entropy_pool[0]));
	// Ensure no stale copies remain in ram
	CACHE_CLEAN_FIXED_RANGE(prng_data->key, 32U);
	// Clear the used bytes just in case
	(void)memset_s(&prng_data->entropy_pool[0],
		       sizeof(prng_data->entropy_pool[0]), 0,
		       BUFFER_DATA_OFFSET * sizeof(uint32_t));
	// Ensure no stale copies remain in ram
	CACHE_CLEAN_FIXED_RANGE(&prng_data->entropy_pool[0],
				BUFFER_DATA_OFFSET * sizeof(uint32_t));

	prng_data->pool_index = BUFFER_DATA_OFFSET;
}

uint64_result_t
prng_get64(void)
{
	uint64_result_t ret;

	assert(prng_initialized);

	spinlock_acquire(&prng_lock);

	count_t index = prng_data->pool_index;

	if (index > (BUFFER_WORDS - (64U / WORD_BITS))) {
		// Not enough buffered randomness, get more
		prng_update();
		index = prng_data->pool_index;
	}
	prng_data->pool_index += (64U / WORD_BITS);

	index_t	  block = index / BLOCK_WORDS;
	index_t	  word	= index % BLOCK_WORDS;
	uint32_t *data	= &prng_data->entropy_pool[block][word];

	ret.r = data[0];
	ret.r |= (uint64_t)data[1] << 32;

	ret.e = OK;
	// Pointer difference in bytes
	ptrdiff_t len = (char *)data - (char *)prng_data->entropy_pool[0];

	assert(len >= 0);

	// Clear used bits
	(void)memset_s(data, sizeof(prng_data->entropy_pool) - (size_t)len, 0,
		       sizeof(ret.r));
	// Ensure used bits are cleared from caches
	CACHE_CLEAN_FIXED_RANGE(data, sizeof(ret.r));

	spinlock_release(&prng_lock);

	return ret;
}
