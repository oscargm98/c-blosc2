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

  if (length == context->leftover) {
    printf("\n Leftover block is not supported \n", length, context->leftover);
    return 0;
  }

  if (length != (blockshape[0] * blockshape[1])) {
    printf("\n length %d, size %d \n", length, (blockshape[0] * blockshape[1]));
    printf("Length not equal to blocksize \n");
    return -1;
  }

  uint8_t* ip = (uint8_t *) input;
  uint8_t* op = (uint8_t *) output;
  uint8_t* op_limit;
  uint32_t htab[1U << 12U];
  uint32_t hval;
  uint8_t* bufcell = malloc(16 * sizeof(uint8_t));
  uint32_t hrcoup[1U << 12U];
  uint32_t hrthr[1U << 12U];
  uint8_t* bufrcoup = malloc(8 * sizeof(uint8_t));
  uint8_t* bufrthr = malloc(12 * sizeof(uint8_t));
  uint8_t *buffer2 = malloc(16 * sizeof(uint8_t));


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

  uint32_t i_stop[2];
  for (int i = 0; i < 2; ++i) {
    i_stop[i] = (blockshape[i] + 3) / 4;
  }

  /* main loop */
  uint32_t padding[2];
  uint32_t ii[2];
  for (ii[0] = 0; ii[0] < i_stop[0]; ++ii[0]) {
    for (ii[1] = 0; ii[1] < i_stop[1]; ++ii[1]) {      // for each cell
      // int ncell = ii[1] + ii[0] * (shape[1] / 4);
      memset(bufcell, 0, 16);
      uint8_t token;
      uint32_t orig = ii[0] * 4 * blockshape[1] + ii[1] * 4;
      if (((blockshape[0] % 4 != 0) && (ii[0] == i_stop[0] - 1)) || ((blockshape[1] % 4 != 0) && (ii[1] == i_stop[1] - 1))) {
        token = 0;                                   // padding -> literal copy
        *op++ = token;
        if (ii[0] == i_stop[0] - 1) {
          padding[0] = (blockshape[0] % 4 == 0) ? 4 : blockshape[0] % 4;
        } else {
          padding[0] = 4;
        }
        if (ii[1] == i_stop[1] - 1) {
          padding[1] = (blockshape[1] % 4 == 0) ? 4 : blockshape[1] % 4;
        } else {
          padding[1] = 4;
        }
        for (int i = 0; i < padding[0]; i++) {
          memcpy(op, &ip[orig + i * blockshape[1]], padding[1]);
          op += padding[1];
        }
      }
      else {
        for (int i = 0; i < 4; i++) {
          int ind = orig + i * blockshape[1];
          memcpy(bufcell, &ip[ind], 4);
          bufcell += 4;
        }
        bufcell -= 16;

        if (NDLZ_UNEXPECT_CONDITIONAL(op + 16 > op_limit)) {
          printf("\n op %p, op_limit %p \n", op, op_limit);
          return 0;
        }
        const uint8_t* ref;
        uint32_t distance;
        uint8_t* anchor = op;    /* comparison starting-point */

        /* find potential match */
        hval = XXH32(bufcell, 16, 1);        // calculate cell hash
        hval >>= 32U - 12U;
        ref = obase + htab[hval];

        /* calculate distance to the match */
        if (htab[hval] == 0) {
          distance = 0;
        } else {
          bool same = true;
          *buffer2 = obase + htab[hval];
          for(int i = 0; i < 16; i++){
            if(bufcell[i] != buffer2[i]) {
              same = false;
            }
          }
          if (same) {
            distance = (int32_t) (anchor - ref);
          } else {
            distance = 0;
          }
        }

        bool alleq = true;
        for (int i = 1; i < 16; i++) {
          if (bufcell[i] != bufcell[0]) {
            alleq = false;
            break;
          }
        }
        if (alleq) {                              // all elements of the cell equal
          token = (uint8_t) (1U << 6U);
          *op++ = token;
          *op++ = bufcell[0];

        } else if (distance == 0 || (distance >= MAX_DISTANCE)) {   // no cell match
          bool literal = true;

          // three rows
          for(int i = 0; i < 2; i++) {
            memcpy(bufrthr, &bufcell[i * 4], 4);
            for (int j = i + 1; j < 3; j++) {
              memcpy(&bufrthr[4], &bufcell[j * 4], 4);
              for (int k = j + 1; k < 4; k++) {
                memcpy(&bufrthr[8], &bufcell[k * 4], 4);
                hval = XXH32(bufrthr, 12, 1);        // calculate trio hash
                hval >>= 32U - 12U;
                ref = obase + hrthr[hval];
                /* calculate distance to the match */
                bool same = true;
                uint16_t offset;
                if (hrthr[hval] != 0) {
                  memset(buffer2, 0, 16);
                  *buffer2 = obase + hrthr[hval];
                  for (int l = 0; l < 12; l++) {
                    if (bufrthr[l] != buffer2[l]) {
                      same = false;
                    }
                  }
                  offset = (uint16_t) (anchor - obase - hrthr[hval]);
                } else {
                  same = false;
                  if ((j - i == 1) && (k - j == 1)) {
                    hrthr[hval] = (uint32_t) (anchor + i * 4 - obase);     /* update hash table */
                  }
                }
                if (same) {
                  distance = (int32_t) (anchor + i * 4 - ref);
                } else {
                  distance = 0;
                }
                if ((distance != 0) && (distance < MAX_DISTANCE)) {
                  literal = false;
                  if (i == 1) {
                    token = (uint8_t) (7U << 5U);
                  } else {
                    token = (uint8_t) ((7U << 5U) | ((j + k - 2) << 3U));
                  }
                  *op++ = token;
                  memcpy(op, &offset, 2);
                  op += 2;
                  for (int l = 0; l < 4; l++) {
                    if ((l != i) && (l != j) && (l != k)) {
                      memcpy(op, &bufcell[4 * l], 4);
                      op += 4;
                      break;
                    }
                  }
                }
              }
            }
          }

          if (literal) {
            // rows couples
            for(int i = 0; i < 3; i++) {
              memcpy(bufrcoup, &bufcell[i * 4], 4);
              for (int j = i + 1; j < 4; j++) {
                memcpy(&bufrcoup[4], &bufcell[j * 4], 4);
                hval = XXH32(bufrcoup, 8, 1);        // calculate couple hash
                hval >>= 32U - 12U;
                ref = obase + hrcoup[hval];
                /* calculate distance to the match */
                bool same = true;
                uint16_t offset;
                if (hrcoup[hval] != 0) {
                  *buffer2 = obase + hrcoup[hval];
                  for(int k = 0; k < 8; k++){
                    if(bufrcoup[k] != buffer2[k]) {
                      same = false;
                    }
                  }
                  offset = (uint16_t) (anchor - obase - hrcoup[hval]);
                } else {
                  same = false;
                  if (j - i == 1) {
                    hrcoup[hval] = (uint32_t) (anchor + i * 4 - obase);     /* update hash table */
                  }
                }
                if (same) {
                  distance = (int32_t) (anchor + i * 4 - ref);
                } else {
                  distance = 0;
                }
                if ((distance != 0) && (distance < MAX_DISTANCE)) {
                  literal = false;
                  if (i == 2) {
                    token = (uint8_t )(1U << 7U);
                  } else {
                    token = (uint8_t )((1U << 7U) | (i << 5U) | (j << 3U));
                  }
                  *op++ = token;
                  memcpy(op, &offset, 2);
                  op += 2;
                  for (int k = 0; k < 4; k++) {
                    if ((k != i) && (k != j)) {
                      memcpy(op, &bufcell[4 * k], 4);
                      op += 4;
                    }
                  }
                }
              }
            }
          }

          if (literal) {
            htab[hval] = (uint32_t) (anchor - obase);     /* update hash table */
            token = 0;
            *op++ = token;
            memcpy(op, bufcell, 16);
            op += 16;
          }

        } else {   // cell match
          token = (uint8_t )((1U << 7U) | (1U << 6U));
          *op++ = token;
          uint16_t offset = (uint16_t) (anchor - obase - htab[hval]);
          memcpy(op, &offset, 2);
          op += 2;
        }

      }
      if((op - obase) > length) {
        //printf("Compressed data is bigger than input! \n", op-obase, length);
        return 0;
      }
      //printf("token %u, pad [%u, %u] \n", token, padding[0], padding[1]);
    }
  }
  free(bufcell);
  free(bufrcoup);
  free(bufrthr);
  free(buffer2);

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
  uint32_t blockshape[2];
  uint32_t eshape[2];
  uint8_t* buffercpy = malloc(16 * sizeof(uint8_t));
  uint8_t token;
  if (NDLZ_UNEXPECT_CONDITIONAL(length <= 0)) {
    return 0;
  }

  /* we start with literal copy */
  ndim = *ip;
  ip ++;
  memcpy(&blockshape[0], ip, 4);
  ip += 4;
  memcpy(&blockshape[1], ip, 4);
  ip += 4;
  eshape[0] = ((blockshape[0] + 3) / 4) * 4;
  eshape[1] = ((blockshape[1] + 3) / 4) * 4;

  memset(op, 0, blockshape[0] * blockshape[1]);

  uint32_t i_stop[2];
  for (int i = 0; i < 2; ++i) {
    i_stop[i] = eshape[i] / 4;
  }

 // printf("\n decomp \n");

  /* main loop */
  uint32_t ii[2];
  uint32_t padding[2];
  uint32_t ind;
  uint8_t cell_aux[16];
  for (ii[0] = 0; ii[0] < i_stop[0]; ++ii[0]) {
    for (ii[1] = 0; ii[1] < i_stop[1]; ++ii[1]) {      // for each cell
      if (ii[0] == i_stop[0] - 1) {
        padding[0] = (blockshape[0] % 4 == 0) ? 4 : blockshape[0] % 4;
      } else {
        padding[0] = 4;
      }
      if (ii[1] == i_stop[1] - 1) {
        padding[1] = (blockshape[1] % 4 == 0) ? 4 : blockshape[1] % 4;
      } else {
        padding[1] = 4;
      }
      token = *ip++;
    //  printf("token %u, pad [%u, %u] \n", token, padding[0], padding[1]);
      if (token == 0){    // no match
        buffercpy = ip;
        ip += padding[0] * padding[1];
      } else if (token == (uint8_t)((1U << 7U) | (1U << 6U))) {  // cell match
        uint16_t offset = *((uint16_t*) ip);
        buffercpy = ip - offset;
        ip += 2;
      } else if (token == (uint8_t)(1U << 6U)) { // whole cell of same element
        buffercpy = cell_aux;
        memset(buffercpy, *ip, 16);
        ip++;
      } else if ((token >= 224) && (token <= 255)) { // three rows match
        buffercpy = malloc(16);
        uint16_t offset = *((uint16_t*) ip);
        ip += 2;
        int i, j, k;
        if ((token >> 3U) == 28) {
          i = 1;
          j = 2;
          k = 3;
        } else {
          i = 0;
          if ((token >> 3U) < 30) {
            j = 1;
            k = 2;
          } else {
            k = 3;
            if ((token >> 3U) == 30) {
              j = 1;
            } else {
              j = 2;
            }
          }
        }
        memcpy(&buffercpy[i * 4], ip - offset, 4);
        memcpy(&buffercpy[j * 4], ip - offset + 4, 4);
        memcpy(&buffercpy[k * 4], ip - offset + 8, 4);
        for (int l = 0; l < 3; l++) {
          if ((l != i) && (l != j) && (l != k)) {
            memcpy(&buffercpy[l * 4], ip, 4);
            ip += 4;
            break;
          }
        }

      } else if ((token >= 128) && (token <= 191)){ // rows couples match
        buffercpy = malloc(16);
        uint16_t offset = *((uint16_t*) ip);
        ip += 2;
        int i, j;
        if (token == 128) {
          i = 2;
          j = 3;
        } else {
          i = (token - 128) >> 5U;
          j = ((token - 128) >> 3U) - (i << 2U);
        }
        memcpy(&buffercpy[i * 4], ip - offset, 4);
        memcpy(&buffercpy[j * 4], ip - offset + 4, 4);
        for (int k = 0; k < 4; k++) {
          if ((k != i) && (k != j)) {
            memcpy(&buffercpy[k * 4], ip, 4);
            ip += 4;
          }
        }
      } else {
        printf("Invalid token \n");
        return 0;
      }
      // fill op with buffercpy

      uint32_t orig = ii[0] * 4 * blockshape[1] + ii[1] * 4;
      for (int i = 0; i < 4; i++) {
        if (i < padding[0]) {
          ind = orig + i * blockshape[1];
          memcpy(&op[ind], buffercpy, padding[1]);
        }
        buffercpy += padding[1];
      }

    }
  }
  ind += padding[1];

  if (ind != (blockshape[0] * blockshape[1])) {
    printf("Output size is not compatible with embeded blockshape \n");
    return 0;
  }
  if (ind > maxout) {
    printf("Output size is bigger than max \n");
    return 0;
  }

  return ind;
}
