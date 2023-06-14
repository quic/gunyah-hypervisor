# © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

CC=clang

INCLUDE=-I../../../interfaces/gpt/include
INCLUDE+=-I../../../../build/qemu/unittests-qemu/debug/include
INCLUDE+=-I../../../../build/qemu/unittests-qemu/debug/events/include
INCLUDE+=-I../../../../build/qemu/unittests-qemu/debug/events/gpt/include
INCLUDE+=-I../../../../build/qemu/unittests-qemu/debug/objects/include
INCLUDE+=-I../../../interfaces/cpulocal/include
INCLUDE+=-I../../../interfaces/log/include
INCLUDE+=-I../../../interfaces/object/include
INCLUDE+=-I../../../interfaces/partition/include
INCLUDE+=-I../../../interfaces/preempt/include
INCLUDE+=-I../../../interfaces/rcu/include
INCLUDE+=-I../../../interfaces/trace/include
INCLUDE+=-I../../../interfaces/util/include
INCLUDE+=-I../../../misc/log_standard/include
INCLUDE+=-I../../../arch/armv8/include
INCLUDE+=-imacros ../../../interfaces/util/include/attributes.h
INCLUDE+=-imacros ../../../core/spinlock_ticket/include/spinlock_attrs.h
INCLUDE+=-imacros ../../../core/preempt/include/preempt_attrs.h
INCLUDE+=-imacros ../../../interfaces/rcu/include/rcu_attrs.h

DEF=-DHYP_STANDALONE_TEST
DEF+=-DUNIT_TESTS

CFLAGS+=-m64 -mcx16 -std=gnu18 -O1 -g
CFLAGS+=-Wall -Werror -Wno-gcc-compat -Wno-gnu-alignof-expression
CFLAGS+=-ffunction-sections -fdata-sections
CFLAGS+=-fsanitize=address -fno-omit-frame-pointer
#CFLAGS+=--coverage
CFLAGS+=$(INCLUDE)
CFLAGS+=$(DEF)

LDFLAGS+=-Wl,--gc-sections

SRC=../src/gpt.c ../src/gpt_tests.c host_tests.c
SRC+=../../../core/util/src/bitmap.c
SRC+=../../../misc/log_standard/src/string_util.c
SRC+=../../../../build/qemu/unittests-qemu/debug/hyp/core/base/accessors.c
SRC+=../../../../build/qemu/unittests-qemu/debug/hyp/core/base/hypresult.c

OBJ=$(patsubst %.c,%.o,$(SRC))

TARGET=gpt_tests

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJ) -o $@

default: $(TARGET)

clean:
	rm -f $(TARGET) $(OBJ)
