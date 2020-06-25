/*
    Copyright (C) 2014  Francesc Alted
    http://blosc.org
    License: BSD 3-Clause (see LICENSE.txt)

    Example program demonstrating use of the Blosc filter from C code.

    To compile this program:

    $ gcc -O many_compressors.c -o many_compressors -lblosc2

    To run:

    $ ./test_ndlz
    Blosc version info: 2.0.0a6.dev ($Date:: 2018-05-18 #$)
    Using 4 threads (previously using 1)
    Using blosclz compressor
    Compression: 4000000 -> 57577 (69.5x)
    Succesful roundtrip!
    Using lz4 compressor
    Compression: 4000000 -> 97276 (41.1x)
    Succesful roundtrip!
    Using lz4hc compressor
    Compression: 4000000 -> 38314 (104.4x)
    Succesful roundtrip!
    Using zlib compressor
    Compression: 4000000 -> 21486 (186.2x)
    Succesful roundtrip!
    Using zstd compressor
    Compression: 4000000 -> 10692 (374.1x)
    Succesful roundtrip!

 */

#include <stdio.h>
#include <blosc2.h>
#include <ndlz.h>
#include <ndlz.c>
#include "test_common.h"


#define SIZE 12*12
#define SHAPE {12,12}
#define CHUNKSHAPE {8,8}

static int test_ndlz(int ndim, uint32_t shape[2]) {
  static float data[SIZE];
  static float data_out[SIZE];
  static float data_dest[SIZE];
  int isize = SIZE, osize = SIZE;
  int dsize = SIZE, csize;

  for (int i = 0; i < SIZE; i++) {
    data[i] = (float) i;
  }

  printf("\n data: \n");
  for (int j = 0; j < SIZE; j++) {
    printf("%f, ", data[j]);
  }

  /* Compress with clevel=5 and shuffle active  */
  csize = ndlz_compress_2(5, data, isize, data_out, osize, ndim, shape);
  if (csize <= 0) {
    printf("Compression error.  Error code: %d\n", csize);
    return csize;
  }

  printf("Compression: %d -> %d (%.1fx)\n", isize, csize, (1. * isize) / csize);

  /* Decompress  */
  dsize = ndlz_decompress_2(data_out, osize, data_dest, dsize);
  if (dsize <= 0) {
    printf("Decompression error.  Error code: %d\n", dsize);
    return dsize;
  }

  for (int i = 0; i < SIZE; i++) {
    if (data[i] != data_dest[i]) {
      printf("Decompressed data differs from original!\n");
      return -1;
    }
  }

  printf("Succesful roundtrip!\n");
  return 0;
}

int main(void) {
  int ndim = 2;
  uint32_t shape[2] = SHAPE;

  /* Run the test. */
  int result = test_ndlz(ndim, shape);
  return result;
}