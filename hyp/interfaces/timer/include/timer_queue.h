// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// TODO: Add functions that work on other CPUs' queues. Important for migrating
// schedulers.

// Initialise a timer object
void
timer_init_object(timer_t *timer, timer_action_t action);

// Returns whether this timer already belongs to a queue
bool
timer_is_queued(timer_t *timer);

// Add a timer object to this CPU's queue with the given absolute timeout
void
timer_enqueue(timer_t *timer, ticks_t timeout);

// Remove a timer object from a timer queue (if queued).
void
timer_dequeue(timer_t *timer);

// Update a timer object with a new absolute timeout. This will add the
// timer to this CPU's queue if not already queued.
void
timer_update(timer_t *timer, ticks_t timeout);

// Return the arch timer frequency
uint32_t
timer_get_timer_frequency(void);

// Return the counter value
ticks_t
timer_get_current_timer_ticks(void);

// Convert counter nanoseconds to ticks
ticks_t
timer_convert_ns_to_ticks(nanoseconds_t ns);

// Convert counter ticks to nanoseconds
nanoseconds_t
timer_convert_ticks_to_ns(ticks_t ticks);

// Get next timeout from cpu local queue
ticks_t
timer_queue_get_next_timeout(void);
