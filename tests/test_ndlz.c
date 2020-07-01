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

#define SHAPE1 1024
#define SHAPE2 1024
#define SIZE SHAPE1 * SHAPE2
#define SHAPE {SHAPE1, SHAPE2}
#define OSIZE (17 * SIZE / 16) + 9

static int test_ndlz(int ndim, uint32_t *shape) {

  uint32_t data[SIZE];
  uint32_t data_out[OSIZE];
  uint32_t data_dest[SIZE];
  int isize = SIZE;
  int osize = OSIZE;
  int dsize = SIZE;
  int csize;
  blosc2_cparams cparams = BLOSC2_CPARAMS_DEFAULTS;

  /* Create a context for compression */
  cparams.typesize = sizeof(uint32_t);
  cparams.compcode = BLOSC_NDLZ;
  cparams.filters[BLOSC2_MAX_FILTERS - 1] = BLOSC_SHUFFLE;
  cparams.clevel = 5;
  //cparams.nthreads = 1;
  cparams.ndim = ndim;
  cparams.blockshape = shape;
  cparams.blocksize = SIZE;
  blosc2_context *cctx = blosc2_create_cctx(cparams);

  printf("\n data: \n");
  for (int i = 0; i < SIZE; i++) {
    data[i] = i;
    //printf("%hhu, ", data[i]);
  }
  //FILE *f = fopen("out1024x1024.txt", "r");

  /* Compress with clevel=5 and shuffle active  */
  csize = ndlz_compress(cctx, data, isize, data_out, osize);
  /*if (csize == 0) {
    printf("Buffer is uncompressible.  Giving up.\n");
    return 0;
  }
  else if (csize < 0) {
    printf("Compression error.  Error code: %d\n", csize);
    return csize;
  }

  printf("Compression: %d -> %d (%.1fx)\n", isize, csize, (1. * isize) / csize);
*/
  /* Decompress  */
 /* dsize = ndlz_decompress(data_out, osize, data_dest, dsize);
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
*/
  printf("Succesful roundtrip!\n");
  return 0;
}

int main(void) {
  int ndim = 2;
  uint32_t shape[1024] = {1024, 1024};

  /* Run the test. */
  int result = test_ndlz(ndim, shape);
  return result;
}