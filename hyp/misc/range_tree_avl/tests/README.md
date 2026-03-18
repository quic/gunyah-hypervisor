# Range Tree AVL Tests

This directory contains tests for the range tree AVL module.

- `unit_test.c`: unit tests. Run on `aarch64` targets to verify correctness of
lock-free code.

## All Tests

### Build

From this directory:

```sh
make
```

**Prerequisite:** `$(GUNYAH_BUILD)` must point to a built Gunyah output tree so
the generated headers/sources referenced by the Makefile are available.

To configure optimizations and debug symbols, use the `OPTFLAGS` and `DBGFLAGS`
Make variables respectively. For example, to use `-O3` and disable debug
symbols:

```sh
make OPTFLAGS="-O3" DBGFLAGS=""
```

## Unit Test

### Build Unit Test

From this directory:

```sh
make ut
```

### Run Unit Test

From this directory:

```sh
./ut
```
