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


#include <stdio.h>
#include "ndlz.h"
#include "fastcopy.h"
#include "blosc2-common.h"
//#include <xxhash.h>
#include "xxhash.c"


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

// This is used in LZ4 and seems to work pretty well here too
#define HASH_FUNCTION(v, s, h) {                          \
  v = (s * 2654435761U) >> (32U - h);  \
}


#define LITERAL(ip, op, op_limit, anchor, copy) {        \
  if (NDLZ_UNEXPECT_CONDITIONAL(op + 2 > op_limit))   \
    goto out;                                            \
  *op++ = *anchor++;                                     \
  ip = anchor;                                           \
  copy++;                                                \
  if (NDLZ_UNEXPECT_CONDITIONAL(copy == MAX_COPY)) {  \
    copy = 0;                                            \
    *op++ = MAX_COPY-1;                                  \
  }                                                      \
}

#define IP_BOUNDARY 2

#if defined(__AVX2__)
static uint8_t *get_run_32(uint8_t *ip, const uint8_t *ip_bound, const uint8_t *ref) {
    uint8_t x = ip[-1];
    /* safe because the outer check against ip limit */
    if (ip < (ip_bound - sizeof(int64_t))) {
        int64_t value, value2;
        /* Broadcast the value for every byte in a 64-bit register */
        memset(&value, x, 8);
#if defined(BLOSC_STRICT_ALIGN)
        memcpy(&value2, ref, 8);
#else
        value2 = ((int64_t*)ref)[0];
#endif
        if (value != value2) {
            /* Return the byte that starts to differ */
            while (*ref++ == x) ip++;
            return ip;
        }
        else {
            ip += 8;
            ref += 8;
        }
    }
    if (ip < (ip_bound - sizeof(__m128i))) {
        __m128i value, value2, cmp;
        /* Broadcast the value for every byte in a 128-bit register */
        memset(&value, x, sizeof(__m128i));
        value2 = _mm_loadu_si128((__m128i *) ref);
        cmp = _mm_cmpeq_epi32(value, value2);
        if (_mm_movemask_epi8(cmp) != 0xFFFF) {
            /* Return the byte that starts to differ */
            while (*ref++ == x) ip++;
            return ip;
        } else {
            ip += sizeof(__m128i);
            ref += sizeof(__m128i);
        }
    }
    while (ip < (ip_bound - (sizeof(__m256i)))) {
        __m256i value, value2, cmp;
        /* Broadcast the value for every byte in a 256-bit register */
        memset(&value, x, sizeof(__m256i));
        value2 = _mm256_loadu_si256((__m256i *)ref);
        cmp = _mm256_cmpeq_epi64(value, value2);
        if ((unsigned)_mm256_movemask_epi8(cmp) != 0xFFFFFFFF) {
            /* Return the byte that starts to differ */
            while (*ref++ == x) ip++;
            return ip;
        }
        else {
            ip += sizeof(__m256i);
            ref += sizeof(__m256i);
        }
    }
    /* Look into the remainder */
    while ((ip < ip_bound) && (*ref++ == x)) ip++;
    return ip;
}

#elif defined(__SSE2__)

static uint8_t *get_run_16(uint8_t *ip, const uint8_t *ip_bound, const uint8_t *ref) {
  uint8_t x = ip[-1];

  if (ip < (ip_bound - sizeof(int64_t))) {
    int64_t value, value2;
    /* Broadcast the value for every byte in a 64-bit register */
    memset(&value, x, 8);
#if defined(BLOSC_STRICT_ALIGN)
    memcpy(&value2, ref, 8);
#else
    value2 = ((int64_t*)ref)[0];
#endif
    if (value != value2) {
      /* Return the byte that starts to differ */
      while (*ref++ == x) ip++;
      return ip;
    }
    else {
      ip += 8;
      ref += 8;
    }
  }
  /* safe because the outer check against ip limit */
  while (ip < (ip_bound - sizeof(__m128i))) {
    __m128i value, value2, cmp;
    /* Broadcast the value for every byte in a 128-bit register */
    memset(&value, x, sizeof(__m128i));
    value2 = _mm_loadu_si128((__m128i *)ref);
    cmp = _mm_cmpeq_epi32(value, value2);
    if (_mm_movemask_epi8(cmp) != 0xFFFF) {
      /* Return the byte that starts to differ */
      while (*ref++ == x) ip++;
      return ip;
    }
    else {
      ip += sizeof(__m128i);
      ref += sizeof(__m128i);
    }
  }
  /* Look into the remainder */
  while ((ip < ip_bound) && (*ref++ == x)) ip++;
  return ip;
}

#else
static uint8_t *get_run(uint8_t *ip, const uint8_t *ip_bound, const uint8_t *ref) {
  uint8_t x = ip[-1];
  int64_t value, value2;
  /* Broadcast the value for every byte in a 64-bit register */
  memset(&value, x, 8);
  /* safe because the outer check against ip limit */
  while (ip < (ip_bound - sizeof(int64_t))) {
#if defined(BLOSC_STRICT_ALIGN)
    memcpy(&value2, ref, 8);
#else
    value2 = ((int64_t*)ref)[0];
#endif
    if (value != value2) {
      /* Return the byte that starts to differ */
      while (*ref++ == x) ip++;
      return ip;
    }
    else {
      ip += 8;
      ref += 8;
    }
  }
  /* Look into the remainder */
  while ((ip < ip_bound) && (*ref++ == x)) ip++;
  return ip;
}
#endif


/* Return the byte that starts to differ */
static uint8_t *get_match(uint8_t *ip, const uint8_t *ip_bound, const uint8_t *ref) {
#if !defined(BLOSC_STRICT_ALIGN)
  while (ip < (ip_bound - sizeof(int64_t))) {
    if (*(int64_t*)ref != *(int64_t*)ip) {
      /* Return the byte that starts to differ */
      while (*ref++ == *ip++) {}
      return ip;
    }
    else {
      ip += sizeof(int64_t);
      ref += sizeof(int64_t);
    }
  }
#endif
  /* Look into the remainder */
  while ((ip < ip_bound) && (*ref++ == *ip++)) {}
  return ip;
}


#if defined(__SSE2__)
static uint8_t *get_match_16(uint8_t *ip, const uint8_t *ip_bound, const uint8_t *ref) {
  __m128i value, value2, cmp;

  if (ip < (ip_bound - sizeof(int64_t))) {
    if (*(int64_t *) ref != *(int64_t *) ip) {
      /* Return the byte that starts to differ */
      while (*ref++ == *ip++) {}
      return ip;
    } else {
      ip += sizeof(int64_t);
      ref += sizeof(int64_t);
    }
  }
  while (ip < (ip_bound - sizeof(__m128i))) {
    value = _mm_loadu_si128((__m128i *) ip);
    value2 = _mm_loadu_si128((__m128i *) ref);
    cmp = _mm_cmpeq_epi32(value, value2);
    if (_mm_movemask_epi8(cmp) != 0xFFFF) {
      /* Return the byte that starts to differ */
      return get_match(ip, ip_bound, ref);
    }
    else {
      ip += sizeof(__m128i);
      ref += sizeof(__m128i);
    }
  }
  /* Look into the remainder */
  while ((ip < ip_bound) && (*ref++ == *ip++)) {}
  return ip;
}
#endif


#if defined(__AVX2__)
static uint8_t *get_match_32(uint8_t *ip, const uint8_t *ip_bound, const uint8_t *ref) {

  if (ip < (ip_bound - sizeof(int64_t))) {
    if (*(int64_t *) ref != *(int64_t *) ip) {
      /* Return the byte that starts to differ */
      while (*ref++ == *ip++) {}
      return ip;
    } else {
      ip += sizeof(int64_t);
      ref += sizeof(int64_t);
    }
  }
  if (ip < (ip_bound - sizeof(__m128i))) {
    __m128i value, value2, cmp;
    value = _mm_loadu_si128((__m128i *) ip);
    value2 = _mm_loadu_si128((__m128i *) ref);
    cmp = _mm_cmpeq_epi32(value, value2);
    if (_mm_movemask_epi8(cmp) != 0xFFFF) {
      /* Return the byte that starts to differ */
      return get_match_16(ip, ip_bound, ref);
    }
    else {
      ip += sizeof(__m128i);
      ref += sizeof(__m128i);
    }
  }
  while (ip < (ip_bound - sizeof(__m256i))) {
    __m256i value, value2, cmp;
    value = _mm256_loadu_si256((__m256i *) ip);
    value2 = _mm256_loadu_si256((__m256i *)ref);
    cmp = _mm256_cmpeq_epi64(value, value2);
    if ((unsigned)_mm256_movemask_epi8(cmp) != 0xFFFFFFFF) {
      /* Return the byte that starts to differ */
      while (*ref++ == *ip++) {}
      return ip;
    }
    else {
      ip += sizeof(__m256i);
      ref += sizeof(__m256i);
    }
  }
  /* Look into the remainder */
  while ((ip < ip_bound) && (*ref++ == *ip++)) {}
  return ip;
}
#endif


int ndlz_compress(const int clevel, const void* input, int length,
                     void* output, int maxout) {
  uint8_t* ibase = (uint8_t*)input;
  uint8_t* ip = ibase;
  uint8_t* ip_bound = ibase + length - IP_BOUNDARY;
  uint8_t* ip_limit = ibase + length - 12;
  uint8_t* op = (uint8_t*)output;
  uint8_t* op_limit;
  uint32_t htab[1U << (uint8_t)HASH_LOG];
  uint32_t hval;
  uint32_t seq;
  uint8_t copy;

  // Minimum cratios before issuing and _early giveup_
  // Remind that ndlz is not meant for cratios <= 2 (too costly to decompress)
  double maxlength_[10] = {-1, .07, .1, .15, .25, .45, .5, .5, .5, .5};
  int32_t maxlength = (int32_t)(length * maxlength_[clevel]);
  if (maxlength > (int32_t)maxout) {
    maxlength = (int32_t)maxout;
  }
  op_limit = op + maxlength;

  uint8_t hashlog_[10] = {0, HASH_LOG - 2, HASH_LOG - 1, HASH_LOG, HASH_LOG,
                           HASH_LOG, HASH_LOG, HASH_LOG, HASH_LOG, HASH_LOG};
  uint8_t hashlog = hashlog_[clevel];
  // Initialize the hash table to distances of 0
  for (unsigned i = 0; i < (1U << hashlog); i++) {
    htab[i] = 0;
  }

  /* input and output buffer cannot be less than 16 and 66 bytes or we can get into trouble */
  if (length < 16 || maxout < 66) {
    return 0;
  }

  /* we start with literal copy */
  copy = 4;
  *op++ = MAX_COPY - 1;
  *op++ = *ip++;
  *op++ = *ip++;
  *op++ = *ip++;
  *op++ = *ip++;

  /* main loop */
  while (NDLZ_EXPECT_CONDITIONAL(ip < ip_limit)) {
    const uint8_t* ref;
    uint32_t distance;
    uint8_t* anchor = ip;    /* comparison starting-point */

    /* find potential match */
    seq = NDLZ_READU32(ip);
    HASH_FUNCTION(hval, seq, hashlog)
    ref = ibase + htab[hval];

    /* calculate distance to the match */
    distance = (int32_t)(anchor - ref);

    /* update hash table */
    htab[hval] = (uint32_t) (anchor - ibase);

    if (distance == 0 || (distance >= MAX_DISTANCE)) {
      LITERAL(ip, op, op_limit, anchor, copy)
      continue;
    }

    /* is this a match? check the first 4 bytes */
    if (NDLZ_UNEXPECT_CONDITIONAL(NDLZ_READU32(ref) == NDLZ_READU32(ip))) {
      ref += 4;
    }
    else {
      /* no luck, copy as a literal */
      LITERAL(ip, op, op_limit, anchor, copy)
      continue;
    }

    /* last matched byte */
    ip = anchor + 4;

    /* distance is biased */
    distance--;

    if (NDLZ_UNEXPECT_CONDITIONAL(!distance)) {
      /* zero distance means a run */
#if defined(__AVX2__)
      ip = get_run_32(ip, ip_bound, ref);
#elif defined(__SSE2__)
      ip = get_run_16(ip, ip_bound, ref);
#else
      ip = get_run(ip, ip_bound, ref);
#endif
    }
    else {
#if defined(__AVX2__)
      ip = get_match_32(ip, ip_bound + IP_BOUNDARY, ref);
#elif defined(__SSE2__)
      ip = get_match_16(ip, ip_bound + IP_BOUNDARY, ref);
#else
      ip = get_match(ip, ip_bound + IP_BOUNDARY, ref);
#endif
    }

    /* if we have copied something, adjust the copy count */
    if (copy)
      /* copy is biased, '0' means 1 byte copy */
      *(op - copy - 1) = (uint8_t)(copy - 1);
    else
      /* back, to overwrite the copy count */
      op--;

    /* reset literal counter */
    copy = 0;

    /* length is biased, '1' means a match of 3 bytes */
    /* When we get back by 4 we obtain quite different compression properties.
     * It looks like 4 is more useful in combination with bitshuffle and small typesizes
     * (compress better and faster in e.g. `b2bench blosclz bitshuffle single 6 6291456 1 19`).
     * Worth experimenting with this in the future.  For the time being, use 3 for high clevels. */
    ip -= clevel > 8 ? 3 : 4;
    long len = ip - anchor;

    /* encode the match */
    if (distance < MAX_DISTANCE) {
      if (len < 7) {
        *op++ = (uint8_t)((len << 5U) + (distance >> 8U));
        *op++ = (uint8_t)((distance & 255U));
      }
      else {
        *op++ = (uint8_t)((7U << 5U) + (distance >> 8U));
        for (len -= 7; len >= 255; len -= 255)
          *op++ = 255;
        *op++ = (uint8_t)len;
        *op++ = (uint8_t)((distance & 255U));
      }
    }
    else {
      /* far away, but not yet in the another galaxy... */
      if (len < 7) {
        distance -= MAX_DISTANCE;
        *op++ = (uint8_t)((len << 5U) + 31);
        *op++ = 255;
        *op++ = (uint8_t)(distance >> 8U);
        *op++ = (uint8_t)(distance & 255U);
      }
      else {
        distance -= MAX_DISTANCE;
        *op++ = (7U << 5U) + 31;
        for (len -= 7; len >= 255; len -= 255)
          *op++ = 255;
        *op++ = (uint8_t)len;
        *op++ = 255;
        *op++ = (uint8_t)(distance >> 8U);
        *op++ = (uint8_t)(distance & 255U);
      }
    }

    /* update the hash at match boundary */
    seq = NDLZ_READU32(ip);
    HASH_FUNCTION(hval, seq, hashlog)
    htab[hval] = (uint32_t) (ip++ - ibase);
    seq >>= 8U;
    HASH_FUNCTION(hval, seq, hashlog)
    htab[hval] = (uint32_t) (ip++ - ibase);
    /* assuming literal copy */
    *op++ = MAX_COPY - 1;

  }

  /* left-over as literal copy */
  ip_bound++;
  while (NDLZ_UNEXPECT_CONDITIONAL(ip <= ip_bound)) {
    if (NDLZ_UNEXPECT_CONDITIONAL(op + 2 > op_limit)) goto out;
    *op++ = *ip++;
    copy++;
    if (NDLZ_UNEXPECT_CONDITIONAL(copy == MAX_COPY)) {
      copy = 0;
      *op++ = MAX_COPY - 1;
    }
  }

  /* if we have copied something, adjust the copy length */
  if (copy)
    *(op - copy - 1) = (uint8_t)(copy - 1);
  else
    op--;

  /* marker for ndlz */
  *(uint8_t*)output |= (1U << 5U);

  return (int)(op - (uint8_t*)output);

  out:
  return 0;

}


int ndlz_compress_2(const int clevel, const void* input, int length,
                    void* output, int maxout, uint8_t ndim, uint32_t shape[2]) {

    if (length != (shape[0] * shape[1])) {
        return -1;
    }
    uint8_t* ip = (uint8_t *) input;
    uint8_t* op = (uint8_t *) output;
    uint8_t* op_limit;
    uint32_t htab[1U << 12U];
    uint32_t hval;
    uint8_t* buffercpy;


    // Minimum cratios before issuing and _early giveup_
    // Remind that ndlz is not meant for cratios <= 2 (too costly to decompress)
    double maxlength_[10] = {-1, .07, .1, .15, .25, .45, .5, .5, .5, .5};
    int32_t maxlength = (int32_t)(length * maxlength_[clevel]);
    if (maxlength > (int32_t)maxout) {
        maxlength = (int32_t)maxout;
    }
    op_limit = op + maxlength;

    // Initialize the hash table to distances of 0
    for (unsigned i = 0; i < (1U << 12U); i++) {
        htab[i] = 0;
    }

    /* input and output buffer cannot be less than 16 and 66 bytes or we can get into trouble */
    if (length < 16 || maxout < 66) {
        return 0;
    }

    /* we start with literal copy */
    *op++ = ndim;
    memcpy(op, &shape[0], 4);
    op += 4;
    memcpy(op, &shape[1], 4);
    op += 4;

    uint8_t* obase = op;

    uint32_t i_stop[2];
    for (int i = 0; i < 2; ++i) {
        i_stop[i] = shape[i] / 4;
    }

    /* main loop */
    uint32_t ii[2];
    for (ii[0] = 0; ii[0] < i_stop[0]; ++ii[0]) {
        for (ii[1] = 0; ii[1] < i_stop[1]; ++ii[1]) {      // for each cell
            // int ncell = ii[1] + ii[0] * (shape[1] / 4);
            uint32_t orig = ii[0] * 4 * shape[1] + ii[1];
            for (int i = 0; i < 4; i++) {
                int ind = orig + i * shape[1];
                memcpy(buffercpy, &ip[ind], 4);
                buffercpy += 4;
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
            ref = obase + htab[hval];

            /* calculate distance to the match */
            distance = (int32_t) (anchor - ref);

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
            }
        }
    }
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

// LZ4 wildCopy which can reach excellent copy bandwidth (even if insecure)
static inline void wild_copy(uint8_t *out, const uint8_t* from, uint8_t* end) {
  uint8_t* d = out;
  const uint8_t* s = from;
  uint8_t* const e = end;

  do { memcpy(d,s,8); d+=8; s+=8; } while (d<e);
}

int ndlz_decompress(const void* input, int length, void* output, int maxout) {
  const uint8_t* ip = (const uint8_t*)input;
  const uint8_t* ip_limit = ip + length;
  uint8_t* op = (uint8_t*)output;
  uint32_t ctrl;
  uint8_t* op_limit = op + maxout;
  if (NDLZ_UNEXPECT_CONDITIONAL(length == 0)) {
    return 0;
  }
  ctrl = (*ip++) & 31U;

  while (1) {
    uint8_t* ref = op;
    int32_t len = ctrl >> 5U;
    int32_t ofs = (ctrl & 31U) << 8U;

    if (ctrl >= 32) {
      uint8_t code;
      len--;
      ref -= ofs;
      if (len == 7 - 1) {
        do {
          if (NDLZ_UNEXPECT_CONDITIONAL(ip + 1 >= ip_limit)) {
            return 0;
          }
          code = *ip++;
          len += code;
        } while (code == 255);
      }
      else {
        if (NDLZ_UNEXPECT_CONDITIONAL(ip + 1 >= ip_limit)) {
          return 0;
        }
      }
      code = *ip++;
      ref -= code;

      /* match from 16-bit distance */
      if (NDLZ_UNEXPECT_CONDITIONAL(code == 255)) {
        if (NDLZ_EXPECT_CONDITIONAL(ofs == (31U << 8U))) {
          if (NDLZ_UNEXPECT_CONDITIONAL(ip + 1 >= ip_limit)) {
            return 0;
          }
          ofs = (*ip++) << 8U;
          ofs += *ip++;
          ref = op - ofs - MAX_DISTANCE;
        }
      }

      if (NDLZ_UNEXPECT_CONDITIONAL(op + len + 3 > op_limit)) {
        return 0;
      }

      if (NDLZ_UNEXPECT_CONDITIONAL(ref - 1 < (uint8_t*)output)) {
        return 0;
      }

      if (NDLZ_EXPECT_CONDITIONAL(ip < ip_limit))
        ctrl = *ip++;
      else
        break;

      if (ref == op) {
        /* optimized copy for a run */
        uint8_t b = ref[-1];
        memset(op, b, len + 3);
        op += len + 3;
      }
      else {
        /* copy from reference */
        ref--;
        len += 3;
#ifdef __AVX2__
        if (op - ref <= 16) {
          // This is not faster on a combination of compilers (clang, gcc, icc) or machines, but
          // it is not slower either.  Let's activate here for experimentation.
          op = copy_match_16(op, ref, len);
        }
        else {
#endif
          uint8_t* endcpy = op + len;
          if ((op - ref < 8) || (op_limit - endcpy < 8)) {
            // We absolutely need a copy_match here
            op = copy_match(op, ref, (unsigned) len);
          }
          else {
            wild_copy(op, ref, endcpy);
            op = endcpy;
          }

#ifdef __AVX2__
        }
#endif
      }
    }
    else {
      ctrl++;
      if (NDLZ_UNEXPECT_CONDITIONAL(op + ctrl > op_limit)) {
        return 0;
      }
      if (NDLZ_UNEXPECT_CONDITIONAL(ip + ctrl > ip_limit)) {
        return 0;
      }

      memcpy(op, ip, ctrl); op += ctrl; ip += ctrl;
      // On GCC-6, fastcopy this is still faster than plain memcpy
      // However, using recent CLANG/LLVM 9.0, there is almost no difference
      // in performance.
      // And starting on CLANG/LLVM 10 and GCC 9, memcpy is generally faster.
      // op = fastcopy(op, ip, (unsigned) ctrl); ip += ctrl;

      if (NDLZ_UNEXPECT_CONDITIONAL(ip >= ip_limit)) break;
      ctrl = *ip++;
    }
  }

  return (int)(op - (uint8_t*)output);
}

int ndlz_decompress_2(const void* input, int length, void* output, int maxout) {
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
    int ind;
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
            uint32_t orig = ii[0] * 4 * shape[1] + ii[1];
            for (int i = 0; i < 4; i++) {
                ind = orig + i * shape[1];
                memcpy(&op[ind], buffercpy, 4);
                buffercpy += 4;
            }
        }
    }
    ind += shape[1] + 4;
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
