#ifndef DEFINES_H
#define DEFINES_H

// Just to get VSCODE to work
#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <netdb.h>
#include <arpa/inet.h>

#define FREE(x) { free(x); x=NULL; }

#ifdef DEBUG
#define debug_log(string,...) printf(string, ##__VA_ARGS__);
#else
#define debug_log(string,...)
#endif

#endif