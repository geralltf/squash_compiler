/* constants.h — build/status/error constants for test_program2.c */
#ifndef CONSTANTS_H
#define CONSTANTS_H

/* Build configuration */
#define RELEASE_BUILD 1
#define BUILD_TAG     42
#define ABS_APPROX    1
#define IS_V1         1
#define IS_V2         0
#define DEFAULT_BUF   256

/* Math */
#define PI_INT  3
#define MAX_VAL 1000
#define MIN_VAL 0

/* Error codes */
#define ERR_NONE     0
#define ERR_INVALID  1
#define ERR_OVERFLOW 2

/* Status codes */
#define STATUS_OK    0
#define STATUS_ERROR 1
#define STATUS_DONE  99

#endif

/* Extended error codes */
#define ERR_DIVZERO  4

/* Version */
#define VERSION_MAJOR 1
#define VERSION_MINOR 2
#define VERSION_PATCH 3
