# Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: BSD-3-Clause

# Path to the common host test infrastructure, relative to this Makefile.
COMMON_TEST_DIR := ../../../tests/host

# Module-specific include paths (range_tree interface).
MODULE_INCLUDES := -I../../../interfaces/range_tree/include
MODULE_INCLUDES += -I.

MODULE_DEFS += -DMAX_NR_SPINLOCKS=8

include $(COMMON_TEST_DIR)/Makefile.common

ROOTSRC := ../src/range_tree_avl.c ./helpers.c
ROOTOBJ := $(patsubst %.c,%.o,$(ROOTSRC))

RANDOM_TEST := rt
RAN_SUBSRC  := ./random_test.c
RAN_SUBOBJ  := $(RAN_SUBSRC:%.c=%.o)

UNIT_TEST   := ut
UNIT_SUBSRC := ./unit_test.c
UNIT_SUBOBJ := $(UNIT_SUBSRC:%.c=%.o)

all: $(RANDOM_TEST) $(UNIT_TEST)

$(RANDOM_TEST): $(ROOTOBJ) $(COMMON_OBJ) $(RAN_SUBOBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

$(UNIT_TEST): $(ROOTOBJ) $(COMMON_OBJ) $(UNIT_SUBOBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

clean:
	rm -f *.o $(ROOTOBJ) $(COMMON_OBJ) $(RANDOM_TEST) $(UNIT_TEST)
