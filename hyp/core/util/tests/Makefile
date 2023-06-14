CC=clang -target aarch64-linux-gnu

CFLAGS=-std=c11 --sysroot=/usr/aarch64-linux-gnu -O3 -flto -Wno-gcc-compat
CPPFLAGS=-D_DEFAULT_SOURCE -D__STDC_WANT_LIB_EXT1__=1 -DHYP_STANDALONE_TEST \
	 -I../../../interfaces/util/include \
	 -I../../../arch/aarch64/include \
	 -I../../../arch/generic/include \
	 -Iqemu/include

SRC=../tests/string.c \
    ../aarch64/src/string.c \
    ../aarch64/src/memset.S \
    ../aarch64/src/memcpy.S

LDFLAGS=-static

OBJ=string-test

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $^ -o $@
