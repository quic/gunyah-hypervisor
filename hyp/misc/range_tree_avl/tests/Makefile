# Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: BSD-3-Clause

ARCH=$(shell uname -m)
CC=clang
OPTFLAGS ?= -O1
DBGFLAGS ?= -g

GENERAL_INCLUDES=-I../../../interfaces/range_tree/include
GENERAL_INCLUDES+=-I$(GUNYAH_BUILD)/include
GENERAL_INCLUDES+=-I$(GUNYAH_BUILD)/events/include
GENERAL_INCLUDES+=-I../../../interfaces/base/include
GENERAL_INCLUDES+=-I../../../interfaces/partition/include
GENERAL_INCLUDES+=-I../../../interfaces/spinlock/include
GENERAL_INCLUDES+=-I../../../interfaces/util/include
GENERAL_INCLUDES+=-I../../../interfaces/platform/include
GENERAL_INCLUDES+=-I../../../interfaces/rcu/include
GENERAL_INCLUDES+=-I../../../arch/armv8/include
GENERAL_INCLUDES+=-I../../../arch/generic/include
GENERAL_INCLUDES+=-I.
GENERAL_INCLUDES+=-imacros ../../../interfaces/util/include/attributes.h
GENERAL_INCLUDES+=-imacros ../../../core/spinlock_ticket/include/spinlock_attrs.h
GENERAL_INCLUDES+=-imacros ../../../core/preempt/include/preempt_attrs.h
GENERAL_INCLUDES+=-imacros ../../../interfaces/rcu/include/rcu_attrs.h
GENERAL_DEFS+=-DHOST_TEST
GENERAL_DEFS+=-DHYP_STANDALONE_TEST
GENERAL_DEFS+=-DVERBOSE=1

ifeq ($(ARCH),x86_64)
	CFLAGS+=--target=x86_64-linux-gnu
	CFLAGS+=-m64 -mcx16
else ifeq ($(ARCH),aarch64)
	CFLAGS+=--target=aarch64-linux-gnu
else
	$(error Unsupported architecture: $(ARCH))
endif
CFLAGS+=-std=gnu18
CFLAGS+=$(OPTFLAGS)
CFLAGS+=$(DBGFLAGS)
CFLAGS+=-Wall -Werror -Wno-gcc-compat -Wno-gnu-alignof-expression
CFLAGS+=-ffunction-sections -fdata-sections
CFLAGS+=-fsanitize=address -fno-omit-frame-pointer
CFLAGS+=$(GENERAL_INCLUDES)
CFLAGS+=$(GENERAL_DEFS)

LDFLAGS=-lstdc++ -lpthread

ROOTSRC=../src/range_tree_avl.c ./stub.c ./plookups.c \
	$(GUNYAH_BUILD)/autogen/core/base/src/accessors.c \
	$(GUNYAH_BUILD)/autogen/core/base/src/hypresult.c
ROOTOBJ=$(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(ROOTSRC)))

RANDOM_TEST=rt
RAN_SUBSRC=./random_test.c
RAN_SUBOBJ=$(RAN_SUBSRC:%.c=%.o)

UNIT_TEST=ut
UNIT_SUBSRC=./unit_test.c
UNIT_SUBOBJ=$(UNIT_SUBSRC:%.c=%.o)

all: $(RANDOM_TEST) $(UNIT_TEST)

$(RANDOM_TEST):$(ROOTOBJ) $(RAN_SUBOBJ)
	$(CC) $(CFLAGS) ${LDFLAGS} $(ROOTOBJ) $(RAN_SUBOBJ) -o $@

$(UNIT_TEST):$(ROOTOBJ) $(UNIT_SUBOBJ)
	$(CC) $(CFLAGS) ${LDFLAGS} $(ROOTOBJ) $(UNIT_SUBOBJ) -o $@

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(BIN) $(ROOTOBJ) $(SUBOBJ) ${RANDOM_TEST} ${UNIT_TEST}
