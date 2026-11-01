#include "common.h"

#include <stdio.h>
#include <stdlib.h>

LogLevel verbose = LOG_ALL;

/*
 * error - wrapper for perror. Exits the program.
 */
void error(char* msg) {
  perror(msg);
  exit(1);
}