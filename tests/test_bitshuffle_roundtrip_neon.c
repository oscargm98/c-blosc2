/*********************************************************************
  Blosc - Blocked Shuffling and Compression Library

  Roundtrip tests for the NEON-accelerated bitshuffle/bitunshuffle.

  Creation date: 2017-07-28
  Author: Lucian Marc <ruben.lucian@gmail.com>

  See LICENSE.txt for details about copyright and rights to use.
**********************************************************************/

#include "test_common.h"
#include "../blosc/shuffle.h"
#include "../blosc/bitshuffle-generic.h"


/* Include NEON-accelerated shuffle implementation if supported by this compiler.
   TODO: Need to also do run-time CPU feature support here. */
#if defined(SHUFFLE_NEON_ENABLED)
  #include "../blosc/bitshuffle-neon.h"
#else
  #if defined(_MSC_VER)
    #pragma message("NEON shuffle tests not enabled.")
  #else
    #warning NEON shuffle tests not enabled.
  #endif
#endif  /* defined(SHUFFLE_NEON_ENABLED) */


/** Roundtrip tests for the NEON-accelerated bitshuffle/bitunshuffle. */
static int test_bitshuffle_roundtrip_neon(size_t type_size, size_t num_elements,
                                          size_t buffer_alignment, int test_type) {
#if defined(SHUFFLE_NEON_ENABLED)
  size_t buffer_size = type_size * num_elements;

  /* Allocate memory for the test. */
  void* original = blosc_test_malloc(buffer_alignment, buffer_size);
  void* bitshuffled = blosc_test_malloc(buffer_alignment, buffer_size);
  void* bitunshuffled = blosc_test_malloc(buffer_alignment, buffer_size);
  void* tmp_buf = blosc_test_malloc(buffer_alignment, buffer_size);

  /* Fill the input data buffer with random values. */
  blosc_test_fill_random(original, buffer_size);

  /* Bitshuffle/bitunshuffle, selecting the implementations based on the test type. */
  switch (test_type) {
    case 0:
      /* neon/neon */
      bitshuffle_neon(original, bitshuffled, buffer_size, type_size, tmp_buf);
      bitunshuffle_neon(bitshuffled, bitunshuffled, buffer_size, type_size, tmp_buf);
      break;
    case 1:
      /* generic/neon */
      bitshuffle_generic(original, bitshuffled, buffer_size, type_size, tmp_buf);
      bitunshuffle_neon(bitshuffled, bitunshuffled, buffer_size, type_size, tmp_buf);
      break;
    case 2:
      /* neon/generic */
      bitshuffle_neon(original, bitshuffled, buffer_size, type_size, tmp_buf);
      bitunshuffle_generic(bitshuffled, bitunshuffled, buffer_size, type_size, tmp_buf);
      break;
    default:
      fprintf(stderr, "Invalid test type specified (%d).", test_type);
      return EXIT_FAILURE;
  }

  /* The round-tripped data matches the original data when the
     result of memcmp is 0. */
  int exit_code = memcmp(original, bitunshuffled, buffer_size) ?
                  EXIT_FAILURE : EXIT_SUCCESS;

  /* Free allocated memory. */
  blosc_test_free(original);
  blosc_test_free(bitshuffled);
  blosc_test_free(bitunshuffled);
  blosc_test_free(tmp_buf);

  return exit_code;
#else
  return EXIT_SUCCESS;
#endif /* defined(SHUFFLE_NEON_ENABLED) */
}


/** Required number of arguments to this test, including the executable name. */
#define TEST_ARG_COUNT  5

int main(int argc, char** argv) {
  /*  argv[1]: sizeof(element type)
      argv[2]: number of elements
      argv[3]: buffer alignment
      argv[4]: test type
  */

  /*  Verify the correct number of command-line args have been specified. */
  if (TEST_ARG_COUNT != argc) {
    blosc_test_print_bad_argcount_msg(TEST_ARG_COUNT, argc);
    return EXIT_FAILURE;
  }

  /* Parse arguments */
  uint32_t type_size;
  if (!blosc_test_parse_uint32_t(argv[1], &type_size) || (type_size < 1)) {
    blosc_test_print_bad_arg_msg(1);
    return EXIT_FAILURE;
  }

  uint32_t num_elements;
  if (!blosc_test_parse_uint32_t(argv[2], &num_elements) || (num_elements < 1)) {
    blosc_test_print_bad_arg_msg(2);
    return EXIT_FAILURE;
  }

  uint32_t buffer_align_size;
  if (!blosc_test_parse_uint32_t(argv[3], &buffer_align_size)
      || (buffer_align_size & (buffer_align_size - 1))
      || (buffer_align_size < sizeof(void*))) {
    blosc_test_print_bad_arg_msg(3);
    return EXIT_FAILURE;
  }

  uint32_t test_type;
  if (!blosc_test_parse_uint32_t(argv[4], &test_type) || (test_type > 2)) {
    blosc_test_print_bad_arg_msg(4);
    return EXIT_FAILURE;
  }

  /* Run the test. */
  return test_bitshuffle_roundtrip_neon(type_size, num_elements, buffer_align_size, test_type);
}
