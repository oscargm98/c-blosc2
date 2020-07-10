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

#define SHAPE1 32
#define SHAPE2 32
#define SIZE SHAPE1 * SHAPE2
#define SHAPE {SHAPE1, SHAPE2}
#define OSIZE (17 * SIZE / 16) + 9 + 8 + BLOSC_MAX_OVERHEAD

static int test_ndlz(uint8_t *data, int isize, int ndim, uint32_t *shape) {

  int osize = (17 * isize / 16) + 9 + 8 + BLOSC_MAX_OVERHEAD;
  int dsize = isize;
  int csize;
  uint8_t *data_out = malloc(osize);
  uint8_t *data_dest = malloc(isize);
  blosc2_cparams cparams = BLOSC2_CPARAMS_DEFAULTS;

  /* Create a context for compression */
  cparams.typesize = sizeof(data);
  cparams.compcode = BLOSC_NDLZ;
  cparams.filters[BLOSC2_MAX_FILTERS - 1] = BLOSC_SHUFFLE;
  cparams.clevel = 5;
  //cparams.nthreads = 1;
  cparams.ndim = ndim;
  cparams.blockshape = shape;
  cparams.blocksize = isize;
  blosc2_context *cctx;
  cctx = blosc2_create_cctx(cparams);

  /* Compress with clevel=5 and shuffle active  */
  csize = ndlz_compress(cctx, data, isize, data_out, osize);
  if (csize == 0) {
    printf("Buffer is uncompressible.  Giving up.\n");
    return 0;
  }
  else if (csize < 0) {
    printf("Compression error.  Error code: %d\n", csize);
    return csize;
  }

  printf("Compression: %d -> %d (%.1fx)\n", isize, csize, (1. * isize) / csize);

  /* Decompress  */
  dsize = ndlz_decompress(data_out, osize, data_dest, dsize);
  if (dsize <= 0) {
    printf("Decompression error.  Error code: %d\n", dsize);
    return dsize;
  }

  /* printf("data dest: \n");
  for (int i = 0; i < isize; i++) {
    printf("%u, ", data_dest[i]);
  }*/
  for (int i = 0; i < isize; i++) {
    if (data[i] != data_dest[i]) {
      printf("Decompressed data differs from original!\n");
      return -1;
    }
  }

  printf("Succesful roundtrip!\n");
  return dsize - csize;
}

int no_matches() {
  int ndim = 2;
  uint32_t shape[2] = {12, 12};
  int isize = (int)(shape[0] * shape[1]);
  uint8_t data[isize];
  for (int i = 0; i < isize; i++) {
    data[i] = i;
  }

  /* Run the test. */
  int result = test_ndlz(data, isize, ndim, shape);
  return result;
}

int all_matches() {
  int ndim = 2;
  uint32_t shape[2] = {32, 32};
  int isize = (int)(shape[0] * shape[1]);
  uint8_t data[isize];
  for (int i = 0; i < isize; i++) {
    data[i] = 0;
  }

  /* Run the test. */
  int result = test_ndlz(data, isize, ndim, shape);
  return result;
}

int some_matches() {
  int ndim = 2;
  uint32_t shape[2] = {32, 32};
  int isize = (int)(shape[0] * shape[1]);
  uint8_t data[isize];
  for (int i = 0; i < isize; i++) {
    data[i] = i;
  }
  for (int i = SIZE / 2; i < SIZE; i++) {
    data[i] = 0;
  }

  /* Run the test. */
  int result = test_ndlz(data, isize, ndim, shape);
  return result;
}

int padding_some() {
  int ndim = 2;
  uint32_t shape[2] = {15, 14};
  int isize = (int)(shape[0] * shape[1]);
  uint8_t data[isize];
  for (int i = 0; i < 2 * isize / 3; i++) {
    data[i] = 0;
  }
  for (int i = 2 * isize / 3; i < isize; i++) {
    data[i] = i;
  }
  printf("\n data \n");
  for (int i = 0; i < isize; i++) {
    printf("%d, ", data[i]);
  }

  /* Run the test. */
  int result = test_ndlz(data, isize, ndim, shape);
  return result;
}

int image1() {
  int ndim = 2;
  uint32_t shape[2] = {1024, 1024};
  int isize = (int)(shape[0] * shape[1]);
  uint8_t data[isize];
  FILE *f = fopen("out1024x1024.txt", "r");

  char *aux = malloc(3 * SIZE);
  fgets(aux, (3 * SIZE) - 4, f);
  for (int i = 0; i < 15; i += 3) {
    data[i] = (uint8_t) aux;
    printf("%u, ", data[i]);
  }
/*
  char aux;
  for (int i = 0; i < 15; i++) {
    fscanf(f, "%s", &aux);
    data2[i] = (uint8_t) aux;
    printf("%u, ", data2[i]);
    fscanf(f, "%s", &aux);
    fscanf(f, "%s", &aux);
  }
*/

  /* Run the test. */
  int result = test_ndlz(data, isize, ndim, shape);
  return result;
}

int main(void) {
  /*
  int result = no_matches();
  printf("no_matches: %d obtained \n \n", result);
  result = all_matches();
  printf("all_matches: %d obtained \n \n", result);
  result = some_matches();
  printf("some_matches: %d obtained \n \n", result);
  */
  int result = padding_some();
  printf("pad_some: %d obtained \n \n", result);
  /*result = image1();
  printf("image1: %d obtained \n \n", result);
*/
}