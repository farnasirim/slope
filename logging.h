#pragma once

#include <cstdio>
#include <errno.h>

#define assert_p(cond, msg) do { \
  if(!(cond)) { \
    perror(msg); \
    std::abort(); \
  } \
} while(false);
