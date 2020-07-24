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

static int test_ndlz(void *data, int nbytes, int typesize, int ndim, int32_t *blockshape) {

  int osize = (17 * nbytes / 16) + 9 + 8 + BLOSC_MAX_OVERHEAD;
  int dsize = nbytes;
  int csize;
  uint8_t *data2 = (uint8_t *) data;
  uint8_t data_out[osize];
  uint8_t data_dest[nbytes];
  blosc2_cparams cparams = BLOSC2_CPARAMS_DEFAULTS;
  blosc2_dparams dparams = BLOSC2_DPARAMS_DEFAULTS;

  /* Create a context for compression */
  cparams.typesize = typesize;
  cparams.compcode = BLOSC_NDLZ;
  cparams.filters[BLOSC2_MAX_FILTERS - 1] = BLOSC_SHUFFLE;
  cparams.clevel = 5;
  cparams.nthreads = 1;
  cparams.ndim = ndim;
  cparams.blockshape = blockshape;
  cparams.blocksize = blockshape[0] * blockshape[1] * typesize;

  /* Create a context for decompression */
  dparams.nthreads = 1;
  dparams.schunk = NULL;

  blosc2_context *cctx;
  blosc2_context *dctx;
  cctx = blosc2_create_cctx(cparams);
  dctx = blosc2_create_dctx(dparams);
/*
  printf("\n data \n");
  for (int i = 0; i < nbytes; i++) {
    printf("%u, ", data2[i]);
  }
*/
  /* Compress with clevel=5 and shuffle active  */
  csize = blosc2_compress_ctx(cctx, nbytes, data, data_out, osize);
  if (csize == 0) {
    printf("Buffer is uncompressible.  Giving up.\n");
    return 0;
  }
  else if (csize < 0) {
    printf("Compression error.  Error code: %d\n", csize);
    return csize;
  }

  printf("Compression: %d -> %d (%.1fx)\n", nbytes, csize, (1. * nbytes) / csize);

  /* Decompress  */
  dsize = blosc2_decompress_ctx(dctx, data_out, data_dest, dsize);
  if (dsize <= 0) {
    printf("Decompression error.  Error code: %d\n", dsize);
    return dsize;
  }

  /* printf("data dest: \n");
  for (int i = 0; i < isize; i++) {
    printf("%u, ", data_dest[i]);
  }

  printf("\n out \n");
  for (int i = 0; i < csize; i++) {
    printf("%u, ", data_out[i]);
  }
  printf("\n dest \n");
  for (int i = 0; i < nbytes; i++) {
    printf("%u, ", data_dest[i]);
  }*/
  for (int i = 0; i < nbytes; i++) {

    if (data2[i] != data_dest[i]) {
      printf("i: %d, data %u, dest %u", i, data2[i], data_dest[i]);
      printf("\n Decompressed data differs from original!\n");
      return -1;
    }
  }

  printf("Succesful roundtrip!\n");
  return dsize - csize;
}

int no_matches() {
  int ndim = 2;
  uint32_t blockshape[2] = {12, 12};
  int isize = (int)(blockshape[0] * blockshape[1]);
  uint8_t data[isize];
  memset(data, 0, isize);
  for (int i = 0; i < isize; i++) {
    data[i] = i;
  }

  /* Run the test. */
  int result = test_ndlz(data, isize, 1, ndim, blockshape);
  return result;
}

int no_matches_pad() {
  int ndim = 2;
  uint32_t blockshape[2] = {11, 13};
  int isize = (int)(blockshape[0] * blockshape[1]);
  uint32_t data[isize];
  memset(data, 0, isize * 4);
  for (int i = 0; i < isize; i++) {
    data[i] = 124557 * i;
    if(i % 2 == 0) {
      data[i] += 87654321;
    }
    if(i % 3 == 0) {
      data[i] -= 87358362;
    }
  }

  /* Run the test. */
  int result = test_ndlz(data, 4 * isize, 4, ndim, blockshape);
  return result;
}

int all_elem_eq() {
  int ndim = 2;
  uint32_t blockshape[2] = {32, 32};
  int isize = (int)(blockshape[0] * blockshape[1]);
  uint32_t data[isize];
  memset(data, 0, isize * 4);
  for (int i = 0; i < isize; i++) {
    data[i] = 0;
  }

  /* Run the test. */
  int result = test_ndlz(data, 4 * isize, 4, ndim, blockshape);
  return result;
}

int all_elem_pad() {
  int ndim = 2;
  uint32_t blockshape[2] = {29, 31};
  int isize = (int)(blockshape[0] * blockshape[1]);
  uint32_t data[isize];
  memset(data, 0, isize * 4);
  for (int i = 0; i < isize; i++) {
    data[i] = 0;
  }

  /* Run the test. */
  int result = test_ndlz(data, 4 * isize, 4, ndim, blockshape);
  return result;
}

int same_cells() {
  int ndim = 2;
  uint32_t blockshape[2] = {32, 32};
  int isize = (int)(blockshape[0] * blockshape[1]);
  uint32_t data[isize];
  memset(data, 0, isize * 4);
  for (int i = 0; i < isize; i += 4) {
    data[i] = 0;
    data[i + 1] = 1111111;
    data[i + 2] = 2;
    data[i + 3] = 1111111;
  }

  /* Run the test. */
  int result = test_ndlz(data, 4 * isize, 4, ndim, blockshape);
  return result;
}

int same_cells_pad() {
  int ndim = 2;
  uint32_t blockshape[2] = {31, 30};
  int isize = (int)(blockshape[0] * blockshape[1]);
  uint32_t data[isize];
  memset(data, 0, isize * 4);
  for (int i = 0; i < blockshape[0]; i++) {
    for (int j = 0; j < blockshape[1]; j += 4) {
      data[i * blockshape[1] + j] = 11111111;
      data[i * blockshape[1] + j + 1] = 222222222;
    }
  }

  /* Run the test. */
  int result = test_ndlz(data, 4 * isize, 4, ndim, blockshape);
  return result;
}

int some_matches() {
  int ndim = 2;
  uint32_t blockshape[2] = {256, 256};
  int isize = (int)(blockshape[0] * blockshape[1]);
  uint8_t data[isize];
  //memset(data, 0, isize);
  for (int i = 0; i < isize; i++) {
    data[i] = (111111111 * i) % 256;
  }
/*  for (int i = isize / 2; i < isize; i++) {
    data[i] = 0;
  }
*/
  /* Run the test. */
  int result = test_ndlz(data, isize, 1, ndim, blockshape);
  return result;
}

int padding_some() {
  int ndim = 2;
  uint32_t blockshape[2] = {15, 14};
  int isize = (int)(blockshape[0] * blockshape[1]);
  uint8_t data[isize];
  memset(data, 0, isize);
  for (int i = 0; i < 2 * isize / 3; i++) {
    data[i] = 0;
  }
  for (int i = 2 * isize / 3; i < isize; i++) {
    data[i] = i;
  }

  /* Run the test. */
  int result = test_ndlz(data, isize, 1, ndim, blockshape);
  return result;
}

int pad_some_32() {
  int ndim = 2;
  uint32_t blockshape[2] = {15, 14};
  int isize = (int)(blockshape[0] * blockshape[1]);
  uint32_t data[isize];
  memset(data, 0, 4 * isize);
  for (int i = 0; i < isize; i++) {
    data[i] = 10000 * i + i;
  }

  /* Run the test. */
  int result = test_ndlz(data, 4 * isize, 4, ndim, blockshape);
  return result;
}

int image1() {
  int ndim = 2;
  uint32_t blockshape[2] = {300, 450};
  int isize = (int)(blockshape[0] * blockshape[1]);
  uint32_t data[isize];

  FILE *f = fopen("/mnt/c/Users/sosca/CLionProjects/c-blosc2/tests/res.bin", "rb");
  fread(data, sizeof(data), 1, f);
  fclose(f);

  /* Run the test. */
  int result = test_ndlz(data, 4 * isize, 4, ndim, blockshape);
  return result;
}

int image2() {
  int ndim = 2;
  uint32_t blockshape[2] = {800, 1200};
  int isize = (int)(blockshape[0] * blockshape[1]);
  uint32_t *data = malloc(isize * 4);

  FILE *f = fopen("/mnt/c/Users/sosca/CLionProjects/c-blosc2/tests/res2.bin", "rb");
  fread(data, sizeof(data), 1, f);
  fclose(f);

  /* Run the test. */
  int result = test_ndlz(data, 4 * isize, 4, ndim, blockshape);
  return result;
}

int image3() {
  int ndim = 2;
  uint32_t blockshape[2] = {256, 256};
  int isize = (int)(blockshape[0] * blockshape[1]);
  uint32_t data[isize];

  FILE *f = fopen("/mnt/c/Users/sosca/CLionProjects/c-blosc2/tests/res3.bin", "rb");
  fread(data, sizeof(data), 1, f);
  fclose(f);

  /* Run the test. */
  int result = test_ndlz(data, 4 * isize, 4, ndim, blockshape);
  return result;
}

int image4() {
  int ndim = 2;
  uint32_t blockshape[2] = {64, 64};
  int isize = (int)(blockshape[0] * blockshape[1]);
  uint32_t data[isize];

  FILE *f = fopen("/mnt/c/Users/sosca/CLionProjects/c-blosc2/tests/res4.bin", "rb");
  fread(data, sizeof(data), 1, f);
  fclose(f);

  /* Run the test. */
  int result = test_ndlz(data, 4 * isize, 4, ndim, blockshape);
  return result;
}

int image5() {
  int ndim = 2;
  uint32_t blockshape[2] = {641, 1140};
  int isize = (int)(blockshape[0] * blockshape[1]);
  uint32_t *data = malloc(isize * 4);

  FILE *f = fopen("/mnt/c/Users/sosca/CLionProjects/c-blosc2/tests/res5.bin", "rb");
  fread(data, sizeof(data), 1, f);
  fclose(f);

  /* Run the test. */
  int result = test_ndlz(data, 4 * isize, 4, ndim, blockshape);
  return result;
}

int main(void) {

  int result;
  /*result = no_matches();
  printf("no_matches: %d obtained \n \n", result);
  result = no_matches_pad();
  printf("no_matches_pad: %d obtained \n \n", result);
  result = all_elem_eq();
  printf("all_elem_eq: %d obtained \n \n", result);
  result = all_elem_pad();
  printf("all_elem_pad: %d obtained \n \n", result);
  result = same_cells();
  printf("same_cells: %d obtained \n \n", result);
  result = same_cells_pad();
  printf("same_cells_pad: %d obtained \n \n", result);
  result = some_matches();
  printf("some_matches: %d obtained \n \n", result);
  result = padding_some();
  printf("pad_some: %d obtained \n \n", result);
  result = pad_some_32();
  printf("pad_some_32: %d obtained \n \n", result);

  result = image1();
  printf("image1 with padding: %d obtained \n \n", result);
  result = image2();
  printf("image2 with NO padding: %d obtained \n \n", result);
  result = image3();
  printf("image3 with NO padding: %d obtained \n \n", result);
  result = image4();
  printf("image4 with NO padding: %d obtained \n \n", result);
*/
  result = image5();
  printf("image5 with padding: %d obtained \n \n", result);
 }