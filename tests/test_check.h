#pragma once
#include <stdio.h>

// Unlike assert, evaluate both actions and checks when Release defines NDEBUG.
#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    return 1; \
  } \
} while (0)
