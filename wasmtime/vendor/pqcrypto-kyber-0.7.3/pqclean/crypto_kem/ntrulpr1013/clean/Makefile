# This Makefile can be used with GNU Make or BSD Make

LIB=libntrulpr1013_clean.a
HEADERS=api.h crypto_core_multsntrup1013.h crypto_declassify.h crypto_decode_1013x2393.h crypto_decode_1013x3.h crypto_decode_1013xint16.h crypto_decode_1013xint32.h crypto_decode_256x16.h crypto_decode_256x2.h crypto_encode_1013x2393.h crypto_encode_1013x2393round.h crypto_encode_1013x3.h crypto_encode_1013xint16.h crypto_encode_256x16.h crypto_encode_256x2.h crypto_sort_int32.h crypto_sort_uint32.h crypto_stream_aes256ctr.h crypto_verify_1583.h params.h 
OBJECTS=crypto_core_multsntrup1013.o crypto_decode_1013x2393.o crypto_decode_1013x3.o crypto_decode_1013xint16.o crypto_decode_1013xint32.o crypto_decode_256x16.o crypto_decode_256x2.o crypto_encode_1013x2393.o crypto_encode_1013x2393round.o crypto_encode_1013x3.o crypto_encode_1013xint16.o crypto_encode_256x16.o crypto_encode_256x2.o crypto_sort_int32.o crypto_sort_uint32.o crypto_stream_aes256ctr.o crypto_verify_1583.o kem.o 

CFLAGS=-O3 -Wall -Wextra -Wpedantic -Wvla -Werror -Wredundant-decls -Wmissing-prototypes -std=c99 -I../../../common $(EXTRAFLAGS)

all: $(LIB)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

$(LIB): $(OBJECTS)
	$(AR) -r $@ $(OBJECTS)

clean:
	$(RM) $(OBJECTS)
	$(RM) $(LIB)
