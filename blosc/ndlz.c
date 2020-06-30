/*********************************************************************
  Blosc - Blocked Shuffling and Compression Library

  Author: Francesc Alted <francesc@blosc.org>
  Author: Oscar Griñón <oscar@blosc.org>
  Author: Aleix Alcacer <aleix@blosc.org>
  Creation date: 2020-06-12

  See LICENSE.txt for details about copyright and rights to use.
**********************************************************************/

/*********************************************************************
  This codec is meant to leverage multidimensionality for getting
  better compression ratios.  The idea is to look for similarities
  in places that are closer in a euclidean metric, not the typical
  linear one.
**********************************************************************/

#define XXH_NAMESPACE ndlz

#define XXH_INLINE_ALL
#include <xxhash.c>
#include <stdio.h>
#include "ndlz.h"



/*
 * Give hints to the compiler for branch prediction optimization.
 */
#if defined(__GNUC__) && (__GNUC__ > 2)
#define NDLZ_EXPECT_CONDITIONAL(c)    (__builtin_expect((c), 1))
#define NDLZ_UNEXPECT_CONDITIONAL(c)  (__builtin_expect((c), 0))
#else
#define NDLZ_EXPECT_CONDITIONAL(c)    (c)
#define NDLZ_UNEXPECT_CONDITIONAL(c)  (c)
#endif

/*
 * Use inlined functions for supported systems.
 */
#if defined(_MSC_VER) && !defined(__cplusplus)   /* Visual Studio */
#define inline __inline  /* Visual C is not C99, but supports some kind of inline */
#endif

#define MAX_COPY 32U
#define MAX_DISTANCE 65535

#ifdef BLOSC_STRICT_ALIGN
  #define NDLZ_READU16(p) ((p)[0] | (p)[1]<<8)
  #define NDLZ_READU32(p) ((p)[0] | (p)[1]<<8 | (p)[2]<<16 | (p)[3]<<24)
#else
  #define NDLZ_READU16(p) *((const uint16_t*)(p))
  #define NDLZ_READU32(p) *((const uint32_t*)(p))
#endif

#define HASH_LOG (12)


int ndlz_compress(blosc2_context* context, const void* input, int length,
                    void* output, int maxout) {

  int clevel = context->clevel;
  int ndim = context->ndim;  // the number of dimensions of the block
  int32_t* blockshape = context->blockshape;  // the shape of block

  if (length != (blockshape[0] * blockshape[1])) {
    return -1;
  }

  uint8_t* ip = (uint8_t *) input;
  uint8_t* op = (uint8_t *) output;
  uint8_t* op_limit;
  uint32_t htab[1U << 12U];
  uint32_t hval;
  uint8_t *buffercpy = malloc(16 * sizeof(uint8_t));

  //printf("\n ip: \n");
  for (int j = 0; j < length; j++) {
    //printf("%hhu, ",  ip[j]);
  }
  // Minimum cratios before issuing and _early giveup_
  // Remind that ndlz is not meant for cratios <= 2 (too costly to decompress)

  op_limit = op + maxout;

  // Initialize the hash table to distances of 0
  for (unsigned i = 0; i < (1U << 12U); i++) {
    htab[i] = 0;
  }

  /* input and output buffer cannot be less than 16 and 66 bytes or we can get into trouble */
  int overhead = 17 + (blockshape[0] * blockshape[1] / 16 - 1) * 3;
  if (length < 16 || maxout < overhead) {
    printf("Incorrect length or maxout");
    return 0;
  }

  uint8_t* obase = op;

  /* we start with literal copy */
  *op++ = ndim;
  memcpy(op, &blockshape[0], 4);
  op += 4;
  memcpy(op, &blockshape[1], 4);
  op += 4;

  //printf("\n obase %d \n", (int) obase);

  uint32_t i_stop[2];
  for (int i = 0; i < 2; ++i) {
    i_stop[i] = blockshape[i] / 4;
  }

  /* main loop */
  uint32_t ii[2];
  for (ii[0] = 0; ii[0] < i_stop[0]; ++ii[0]) {
    for (ii[1] = 0; ii[1] < i_stop[1]; ++ii[1]) {      // for each cell
      // int ncell = ii[1] + ii[0] * (shape[1] / 4);
      memset(buffercpy, 0, 16);
      uint32_t orig = ii[0] * 4 * blockshape[1] + ii[1] * 4;
      //printf("\n orig: %u \n", orig);
      for (int i = 0; i < 4; i++) {
        int ind = orig + i * blockshape[1];
        memcpy(buffercpy, &ip[ind], 4);
        //printf("\n ip[ind]: %hhu, ", ip[ind]);
        //printf("\n buffcpy: %hhu, ", buffercpy[0]);
        buffercpy += 4;
      }

      buffercpy -= 16;
      //printf("\n Buffercpy: \n");
      for (int j = 0; j < 16; j++) {
        //printf("%hhu, ", buffercpy[j]);
      }

      if (NDLZ_UNEXPECT_CONDITIONAL(op + 16 > op_limit)) {
        return 0;
      }
      const uint8_t *ref;
      uint32_t distance;
      uint8_t* anchor = op;    /* comparison starting-point */

      /* find potential match */
      hval = XXH32(buffercpy, 16, 1);        // calculate cell hash
      hval >>= 32U - 12U;
      //printf("\n hval: %d \n", hval);
      ref = obase + htab[hval];

      /* calculate distance to the match */
      if (htab[hval] == 0) {
        distance = 0;
      } else {
        distance = (int32_t) (anchor - ref);
      }
      //printf("\n distance: %d \n", distance);
      //printf("\n ref: %d \n", ref);

      uint8_t token;
      if (distance == 0 || (distance >= MAX_DISTANCE)) {   // no match
        htab[hval] = (uint32_t) (anchor - obase);     /* update hash table */
        token = 0;
        *op++ = token;
        memcpy(op, buffercpy, 16);
        op += 16;
      } else {   //match
        token = (uint8_t )(1U << 7U);
        *op++ = token;
        uint16_t offset = (uint16_t) (anchor - obase - htab[hval]);
        memcpy(op, &offset, 2);
        op += 2;
        //printf("\n match \n");
      }
    }
  }
  //printf("\n op %d obase %d return %d \n", (int) op, (int) obase, (int)(op - obase));
  return (int)(op - obase);
}


// See https://habr.com/en/company/yandex/blog/457612/
#ifdef __AVX2__

#if defined(_MSC_VER)
#define ALIGNED_(x) __declspec(align(x))
#else
#if defined(__GNUC__)
#define ALIGNED_(x) __attribute__ ((aligned(x)))
#endif
#endif
#define ALIGNED_TYPE_(t, x) t ALIGNED_(x)

static unsigned char* copy_match_16(unsigned char *op, const unsigned char *match, int32_t len)
{
  size_t offset = op - match;
  while (len >= 16) {

    static const ALIGNED_TYPE_(uint8_t, 16) masks[] =
      {
                0,  1,  2,  1,  4,  1,  4,  2,  8,  7,  6,  5,  4,  3,  2,  1, // offset = 0, not used as mask, but for shift
                0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, // offset = 1
                0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,
                0,  1,  2,  0,  1,  2,  0,  1,  2,  0,  1,  2,  0,  1,  2,  0,
                0,  1,  2,  3,  0,  1,  2,  3,  0,  1,  2,  3,  0,  1,  2,  3,
                0,  1,  2,  3,  4,  0,  1,  2,  3,  4,  0,  1,  2,  3,  4,  0,
                0,  1,  2,  3,  4,  5,  0,  1,  2,  3,  4,  5,  0,  1,  2,  3,
                0,  1,  2,  3,  4,  5,  6,  0,  1,  2,  3,  4,  5,  6,  0,  1,
                0,  1,  2,  3,  4,  5,  6,  7,  0,  1,  2,  3,  4,  5,  6,  7,
                0,  1,  2,  3,  4,  5,  6,  7,  8,  0,  1,  2,  3,  4,  5,  6,
                0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  0,  1,  2,  3,  4,  5,
                0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10,  0,  1,  2,  3,  4,
                0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11,  0,  1,  2,  3,
                0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12,  0,  1,  2,
                0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13,  0,  1,
                0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,  0,
                0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,  15, // offset = 16
      };

    _mm_storeu_si128((__m128i *)(op),
                     _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(match)),
                                      _mm_load_si128((const __m128i *)(masks) + offset)));

    match += masks[offset];

    op += 16;
    len -= 16;
  }
  // Deal with remainders
  for (; len > 0; len--) {
    *op++ = *match++;
  }
  return op;
}
#endif


int ndlz_decompress(const void* input, int length, void* output, int maxout) {
  uint8_t* ip = (uint8_t*)input;
  uint8_t* ip_limit = ip + length;
  uint8_t* op = (uint8_t*)output;
  uint8_t ndim;
  uint32_t shape[2];
  uint8_t* buffercpy;
  uint8_t token;
  if (NDLZ_UNEXPECT_CONDITIONAL(length <= 0)) {
    return 0;
  }

  /* we start with literal copy */
  ndim = *ip;
  ip ++;
  memcpy(&shape[0], ip, 4);
  ip += 4;
  memcpy(&shape[1], ip, 4);
  ip += 4;

  uint32_t i_stop[2];
  for (int i = 0; i < 2; ++i) {
    i_stop[i] = shape[i] / 4;
  }

  /* main loop */
  uint32_t ii[2];
  uint32_t ind;
  for (ii[0] = 0; ii[0] < i_stop[0]; ++ii[0]) {
    for (ii[1] = 0; ii[1] < i_stop[1]; ++ii[1]) {      // for each cell
      token = *ip++;
      if (token == 0){    // no match
        buffercpy = ip;
        ip += 16;
      } else if (token == (uint8_t)(1U << 7U)) {  // match
        uint16_t offset = *((uint16_t*) ip);
        buffercpy = ip - offset;
        ip += 2;
      } else {
        printf("Invalid token \n");
        return 0;
      }
      // fill op with buffercpy
      uint32_t orig = ii[0] * 4 * shape[1] + ii[1] * 4;
      for (int i = 0; i < 4; i++) {
        ind = orig + i * shape[1];
        memcpy(&op[ind], buffercpy, 4);
        buffercpy += 4;
      }
    }
  }
  //printf("ind: %d", ind);
  ind += 4;
  if (ind != (shape[0] * shape[1])) {
    printf("Output size is not compatible with embeded shape \n");
    return 0;
  }
  if (ind > maxout) {
    printf("Output size is bigger than max \n");
    return 0;
  }

  return ind;
}
