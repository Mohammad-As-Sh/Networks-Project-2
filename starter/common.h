#ifndef COMMON_H_INCLUDED
#define COMMON_H_INCLUDED

#include <stdio.h>

typedef enum {
  LOG_NONE = 0,
  LOG_INFO = 1 << 0,     // 1
  LOG_WARNING = 1 << 1,  // 2
  LOG_DEBUG = 1 << 2,    // 4
  LOG_ALL = LOG_INFO | LOG_WARNING | LOG_DEBUG
} LogLevel;

extern LogLevel verbose;

#define VLOG(level, Fmt, ...)                   \
  do {                                          \
    if (level & verbose) {                      \
      fprintf(stderr, Fmt "\n", ##__VA_ARGS__); \
    }                                           \
  } while (0)

void error(char* msg) __attribute__((noreturn));

#endif