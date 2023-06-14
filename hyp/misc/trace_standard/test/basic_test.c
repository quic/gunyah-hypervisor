// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>
#include <stdio.h>

#include <libgen.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <asm/cpu.h>

#include "trace.h"

// NOTE: should be no more than maximum cpu count, right now it's 8
#define THREAD_CNT 8

int thread_id[THREAD_CNT];

pthread_t tid[THREAD_CNT];

extern void
btrace_dump(void);

cpu_index_t
cpulocal_check_index(cpu_index_t i)
{
	return i;
}

// Simulate local cpu id with pthread id
cpu_index_t
cpulocal_get_index(void)
{
	pthread_t id = pthread_self();
	int	  i  = 0;

	for (i = 0; i < THREAD_CNT; ++i) {
		if (pthread_equal(tid[i], id)) {
			break;
		}
	}

	return i;
}

int
compiler_ffs(long int x)
{
	return ffsl(x);
}

void
preempt_disable(void)
{
}

void
preempt_enable(void)
{
}

paddr_t
get_paddr(void *ptr)
{
	return 0UL;
}

size_t
get_cpu_cnt(void)
{
	return 4;
}

thread_t *
thread_get_self(void)
{
	return (thread_t *)pthread_self();
}

void *
thread_run(void *val)
{
	int	      i		= 0;
	int	     *tid	= (int *)val;
	trace_class_t class_map = 0L;
	trace_id_t    id	= 0;

	switch (*tid) {
	case 0:
		TRACE_SET_CLASS(class_map, SCHED);
		id = TRACE_ID(SWITCH_TO_IDLE);
		break;
	case 1:
		TRACE_SET_CLASS(class_map, SYSCALL);
		id = TRACE_ID(CONTEXT_SWITCH);
		break;
	case 2:
		TRACE_SET_CLASS(class_map, INTERRUPTS);
		id = TRACE_ID(YIELD);
		break;
	case 3:
		TRACE_SET_CLASS(class_map, LOCK);
		id = TRACE_ID(ERROR);
		break;
	default:
		TRACE_SET_CLASS(class_map, SCHED);
		TRACE_SET_CLASS(class_map, LOCK);
		id = TRACE_ID(EXCEPTION);
		break;
	}

	while (i < 10000) {
		if (i == 500 && *tid == 0) {
			trace_clear_class_flags(0x7);
		}
		TRACE_LONG(class_map, id, 0xff, i);
		++i;
	}

	return NULL;
}

// return the cycle count, and here assume 1 micro sec for each cycle
uint64_t
asm_get_timestamp(void)
{
	struct timespec ts;
	int		ret  = 0;
	long		time = 0L;

	ret = clock_gettime(CLOCK_REALTIME, &ts);
	if (ret != 0) {
		return 0L;
	}

	time = ts.tv_sec * 1000000;
	time += ts.tv_nsec / 1000;

	return (uint64_t)time;
}

void
help(char *app_name)
{
	printf("Usage: %s [OPTION]...\n", basename(app_name));
	printf("Run the binary trace test case from host development PC\n\n");
	printf("Arguments:\n");
	printf("\t -s \t\t specify the size of trace buffer, "
	       "default 1024 bytes \n");
	printf("\t -f \t\t specify the enabled event to trace, default 0xF\n");
	printf("\n");
	printf("The trace buffer size should be multiple of cache line, \n"
	       "which normally is 64 bytes\n");

	return;
}

int
main(int argc, char *argv[])
{
	int	   i		 = 0;
	int	   ret		 = 0;
	size_t	   trace_buf_sz	 = 1024;
	register_t enabled_flags = 0xFL;
	int	   opt;

	while ((opt = getopt(argc, argv, "s:f:h")) != -1) {
		switch (opt) {
		case 's':
			trace_buf_sz = atoi(optarg);
			break;
		case 'f':
			enabled_flags = (uint64_t)strtol(optarg, NULL, 16);
			break;
		case 'h':
			help(argv[0]);
			break;
		default:
			help(argv[0]);
			exit(-1);
		}
	}

	btrace_init();

#if 1
	for (i = 0; i < THREAD_CNT; ++i) {
		thread_id[i] = i;
		ret = pthread_create(&tid[i], NULL, thread_run, &thread_id[i]);
		if (ret != 0) {
			printf("Error: failed to create thread.\n");
			exit(-1);
		}
	}

	for (i = 0; i < THREAD_CNT; ++i) {
		pthread_join(tid[i], NULL);
	}
#else
	thread_id[0] = 0;
	thread_run(&thread_id[0]);
#endif

	printf("trace result: \n");

	btrace_dump();

	return 0;
}
