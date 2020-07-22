/*********************************************************************
  Blosc - Blocked Shuffling and Compression Library

  Author: Francesc Alted <francesc@blosc.org>
  Creation date: 2017-08-29

  See LICENSE.txt for details about copyright and rights to use.
**********************************************************************/

#ifndef BTUNE_H
#define BTUNE_H

#include "context.h"

/* The size of L1 cache.  32 KB is quite common nowadays. */
#define L1 (32 * 1024)
/* The size of L2 cache.  256 KB is quite common nowadays. */
#define L2 (256 * 1024)

/* The maximum number of splits in a block for compression */
#define MAX_SPLITS 16            /* Cannot be larger than 128 */


BLOSC_EXPORT void btune_init(void * config, blosc2_context* cctx, blosc2_context* dctx);

void btune_next_blocksize(blosc2_context * context);

void btune_next_cparams(blosc2_context * context);

void btune_update(blosc2_context * context, double ctime);

void btune_free(blosc2_context * context);

/* Conditions for splitting a block before compressing with a codec. */
static int split_block(int compcode, int32_t typesize, int32_t blocksize, bool extended_header) {
  // Normally all the compressors designed for speed benefit from a split.
  return (
    ((compcode == BLOSC_BLOSCLZ) ||
     (compcode == BLOSC_NDLZ) ||
     // Non-IPP LZ4 seems to prefer not to split, specially on floating point
     // (compcode == BLOSC_LZ4) ||
     // For forward compatibility with Blosc1 (http://blosc.org/posts/new-forward-compat-policy/)
     (!extended_header && compcode == BLOSC_LZ4HC) ||
     (!extended_header && compcode == BLOSC_ZLIB) ||
     (compcode == BLOSC_SNAPPY)) &&
     (typesize <= MAX_SPLITS) &&
     (blocksize / typesize) >= BLOSC_MIN_BUFFERSIZE);
}

#endif  /* BTUNE_H */
