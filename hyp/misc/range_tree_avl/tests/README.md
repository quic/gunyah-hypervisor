# Range Tree AVL Tests

This directory contains tests for the range tree AVL module.

- `unit_test.c`: unit tests. Run on `aarch64` targets to verify correctness of
lock-free code.
- `random_test.c`: runs random operation sequences against the range tree.

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

## Random Test

### Build Random Test

From this directory:

```sh
make rt
```

### Generate Random Test Cases

From this directory:

```sh
./range_tree_testcase.py -o testcases.txt
```

**Prerequisite:** the Python `transitions` module must be available.

To generate a reproducible test case file with a specific random seed and number
of operations:

```sh
./range_tree_testcase.py -o testcases.txt -s 1 -m 100000
```

Use the generated `testcases.txt` as input to `rt`.

### Run Random Test

From this directory:

```sh
./rt -f testcases.txt
```

To test parallel lookups during range tree updates run:

```sh
./rt -f testcases.txt -p
```
