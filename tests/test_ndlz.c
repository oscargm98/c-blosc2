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

  int osize = nbytes + BLOSC_MAX_OVERHEAD;
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
  if (cparams.blocksize < BLOSC_MIN_BUFFERSIZE) {
    printf("\n Blocksize is letter than min \n");
  }

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
/*
   printf("data2: \n");
  for (int i = 0; i < nbytes; i++) {
    printf("%u, ", data2[i]);
  }

  printf("\n out \n");
  for (int i = 0; i < osize; i++) {
    printf("%u, ", data_out[i]);
  }
  printf("\n dest \n");
  for (int i = 0; i < nbytes; i++) {
    printf("%u, ", data_dest[i]);
  }
*/
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
  int32_t shape[2] = {24, 36};
  int32_t blockshape[2] = {12, 12};
  blosc2_frame *frame1 = blosc2_frame_from_file("/mnt/c/Users/sosca/CLionProjects/c-blosc2/examples/ndlz/nomatch.cat");
  blosc2_schunk *sc1 = blosc2_schunk_from_frame(frame1, false);
  int isize = (int)(sc1->chunksize);
  uint8_t *data = malloc(isize);
  blosc2_schunk_decompress_chunk(sc1, 0, data, isize);

  /* Run the test. */
  int result = test_ndlz(data, isize, 1, ndim, blockshape);
  return result;
}

int no_matches_pad() {
  int ndim = 2;
  int32_t shape[2] = {19, 21};
  int32_t blockshape[2] = {11, 13};
  blosc2_frame *frame1 = blosc2_frame_from_file("/mnt/c/Users/sosca/CLionProjects/c-blosc2/examples/ndlz/nomatchpad.cat");
  blosc2_schunk *sc1 = blosc2_schunk_from_frame(frame1, false);
  int isize = (int)(sc1->chunksize);
  uint8_t *data = malloc(isize);
  blosc2_schunk_decompress_chunk(sc1, 0, data, isize);

  /* Run the test. */
  int result = test_ndlz(data, isize, 4, ndim, blockshape);
  return result;
}

int all_elem_eq() {
  int ndim = 2;
  int32_t shape[2] = {64, 64};
  int32_t blockshape[2] = {32, 32};
  blosc2_frame *frame3 = blosc2_frame_from_file("/mnt/c/Users/sosca/CLionProjects/c-blosc2/examples/ndlz/alleq1.cat");
  blosc2_schunk *sc3 = blosc2_schunk_from_frame(frame3, false);
  int isize = (int)(sc3->chunksize);
  uint8_t *data = malloc(isize);
  blosc2_schunk_decompress_chunk(sc3, 0, data, isize);

  /* Run the test. */
  int result = test_ndlz(data, isize, 4, ndim, blockshape);
  return result;
}

int all_elem_pad() {
  int ndim = 2;
  int32_t shape[2] = {29, 31};
  int32_t blockshape[2] = {12, 14};
  blosc2_frame *frame4 = blosc2_frame_from_file("/mnt/c/Users/sosca/CLionProjects/c-blosc2/examples/ndlz/alleqpad.cat");
  blosc2_schunk *sc4 = blosc2_schunk_from_frame(frame4, false);
  int isize = (int)(sc4->chunksize);
  uint8_t *data = malloc(isize);
  blosc2_schunk_decompress_chunk(sc4, 0, data, isize);

  /* Run the test. */
  int result = test_ndlz(data, isize, 4, ndim, blockshape);
  return result;
}

int same_cells() {
  int ndim = 2;
  int32_t shape[2] = {32, 32};
  int32_t blockshape[2] = {25, 23};
  blosc2_frame *frame5 = blosc2_frame_from_file("/mnt/c/Users/sosca/CLionProjects/c-blosc2/examples/ndlz/samecells.cat");
  blosc2_schunk *sc5 = blosc2_schunk_from_frame(frame5, false);
  int isize = (int)(sc5->chunksize);
  uint8_t *data = malloc(isize);
  blosc2_schunk_decompress_chunk(sc5, 0, data, isize);

  /* Run the test. */
  int result = test_ndlz(data, isize, 4, ndim, blockshape);
  return result;
}

int same_cells_pad() {
  int ndim = 2;
  int32_t shape[2] = {31, 30};
  int32_t blockshape[2] = {25, 23};
  blosc2_frame *frame6 = blosc2_frame_from_file("/mnt/c/Users/sosca/CLionProjects/c-blosc2/examples/ndlz/samecellspad.cat");
  blosc2_schunk *sc6 = blosc2_schunk_from_frame(frame6, false);
  int isize = (int)(sc6->chunksize);
  uint8_t *data = malloc(isize);
  blosc2_schunk_decompress_chunk(sc6, 0, data, isize);

  /* Run the test. */
  int result = test_ndlz(data, isize, 4, ndim, blockshape);
  return result;
}

int some_matches() {
  int ndim = 2;
  int32_t shape[2] = {256, 256};
  int32_t blockshape[2] = {64, 64};
  blosc2_frame *frame7 = blosc2_frame_from_file("/mnt/c/Users/sosca/CLionProjects/c-blosc2/examples/ndlz/somematch.cat");
  blosc2_schunk *sc7 = blosc2_schunk_from_frame(frame7, false);
  int isize = (int)(sc7->chunksize);
  uint8_t *data = malloc(isize);
  blosc2_schunk_decompress_chunk(sc7, 0, data, isize);

  /* Run the test. */
  int result = test_ndlz(data, isize, 1, ndim, blockshape);
  return result;
}

int padding_some() {
  int ndim = 2;
  int32_t shape[2] = {215, 233};
  int32_t blockshape[2] = {98, 119};
  blosc2_frame *frame8 = blosc2_frame_from_file("/mnt/c/Users/sosca/CLionProjects/c-blosc2/examples/ndlz/somematchpad.cat");
  blosc2_schunk *sc8 = blosc2_schunk_from_frame(frame8, false);
  int isize = (int)(sc8->chunksize);
  uint8_t *data = malloc(isize);
  blosc2_schunk_decompress_chunk(sc8, 0, data, isize);

  /* Run the test. */
  int result = test_ndlz(data, isize, 1, ndim, blockshape);
  return result;
}

int pad_some_32() {
  int ndim = 2;
  int32_t shape[2] = {37, 29};
  int32_t blockshape[2] = {17, 18};
  blosc2_frame *frame9 = blosc2_frame_from_file("/mnt/c/Users/sosca/CLionProjects/c-blosc2/examples/ndlz/somepad32.cat");
  blosc2_schunk *sc9 = blosc2_schunk_from_frame(frame9, false);
  int isize = (int)(sc9->chunksize);
  uint8_t *data = malloc(isize);
  blosc2_schunk_decompress_chunk(sc9, 0, data, isize);

  /* Run the test. */
  int result = test_ndlz(data, isize, 4, ndim, blockshape);
  return result;
}

int image1() {
  int ndim = 2;
  int32_t shape[2] = {300, 450};
  int32_t blockshape[2] = {77, 65};
  blosc2_frame *framei1 = blosc2_frame_from_file("/mnt/c/Users/sosca/CLionProjects/c-blosc2/examples/ndlz/image1.cat");
  blosc2_schunk *sci1 = blosc2_schunk_from_frame(framei1, false);
  int isize = (int)(sci1->chunksize);
  uint8_t *data = malloc(isize);
  blosc2_schunk_decompress_chunk(sci1, 0, data, isize);

  /* Run the test. */
  int result = test_ndlz(data, isize, 4, ndim, blockshape);
  return result;
}

int image2() {
  int ndim = 2;
  int32_t shape[2] = {800, 1200};
  int32_t blockshape[2] = {117, 123};
  blosc2_frame *framei2 = blosc2_frame_from_file("/mnt/c/Users/sosca/CLionProjects/c-blosc2/examples/ndlz/image2.cat");
  blosc2_schunk *sci2 = blosc2_schunk_from_frame(framei2, false);
  int isize = (int)(sci2->chunksize);
  uint8_t *data = malloc(isize);
  blosc2_schunk_decompress_chunk(sci2, 0, data, isize);

  /* Run the test. */
  int result = test_ndlz(data, isize, 4, ndim, blockshape);
  return result;
}

int image3() {
  int ndim = 2;
  int32_t shape[2] = {256, 256};
  int32_t blockshape[2] = {64, 64};
  blosc2_frame *framei3 = blosc2_frame_from_file("/mnt/c/Users/sosca/CLionProjects/c-blosc2/examples/ndlz/image3.cat");
  blosc2_schunk *sci3 = blosc2_schunk_from_frame(framei3, false);
  int isize = (int)(sci3->chunksize);
  uint8_t *data = malloc(isize);
  blosc2_schunk_decompress_chunk(sci3, 0, data, isize);

  /* Run the test. */
  int result = test_ndlz(data, isize, 4, ndim, blockshape);
  return result;
}

int image4() {
  int ndim = 2;
  int32_t shape[2] = {64, 64};
  int32_t blockshape[2] = {32, 32};
  blosc2_frame *framei4 = blosc2_frame_from_file("/mnt/c/Users/sosca/CLionProjects/c-blosc2/examples/ndlz/image4.cat");
  blosc2_schunk *sci4 = blosc2_schunk_from_frame(framei4, false);
  int isize = (int)(sci4->chunksize);
  uint8_t *data = malloc(isize);
  blosc2_schunk_decompress_chunk(sci4, 0, data, isize);

  /* Run the test. */
  int result = test_ndlz(data, isize, 4, ndim, blockshape);
  return result;
}

int image5() {
  int ndim = 2;
  int32_t shape[2] = {641, 1140};
  int32_t blockshape[2] = {128, 128};
  blosc2_frame *framei5 = blosc2_frame_from_file("/mnt/c/Users/sosca/CLionProjects/c-blosc2/examples/ndlz/image5.cat");
  blosc2_schunk *sci5 = blosc2_schunk_from_frame(framei5, false);
  int isize = (int)(sci5->chunksize);
  uint8_t *data = malloc(isize);
  blosc2_schunk_decompress_chunk(sci5, 0, data, isize);

  /* Run the test. */
  int result = test_ndlz(data, isize, 4, ndim, blockshape);
  free(data);
  return result;
}

int image6() {
  int ndim = 2;
  int32_t shape[2] = {256, 256};
  int32_t blockshape[2] = {64, 64};
  blosc2_frame *framei6 = blosc2_frame_from_file("/mnt/c/Users/sosca/CLionProjects/c-blosc2/examples/ndlz/image6.cat");
  blosc2_schunk *sci6 = blosc2_schunk_from_frame(framei6, false);
  int isize = (int)(sci6->chunksize);
  uint8_t *data = malloc(isize);
  blosc2_schunk_decompress_chunk(sci6, 0, data, isize);

  /* Run the test. */
  int result = test_ndlz(data, isize, 4, ndim, blockshape);
  free(data);
  return result;
}


int main(void) {

  int result;

  result = no_matches();
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
  printf("image2 with  padding: %d obtained \n \n", result);
  result = image3();
  printf("image3 with NO padding: %d obtained \n \n", result);
  result = image4();
  printf("image4 with NO padding: %d obtained \n \n", result);
  result = image5();
  printf("image5 with padding: %d obtained \n \n", result);
  result = image6();
  printf("image6 with NO padding: %d obtained \n \n", result);

}