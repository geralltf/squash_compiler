#ifndef _ENDIAN_H
#define _ENDIAN_H
/* Minimal Linux <endian.h> shim -- squash currently only targets little-
 * endian hosts (x86-64, AArch64), so this is a fixed answer rather than a
 * real runtime/compile-time byte-order probe. */
#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN    4321
#define __PDP_ENDIAN    3412
#define __BYTE_ORDER    __LITTLE_ENDIAN
#define LITTLE_ENDIAN   __LITTLE_ENDIAN
#define BIG_ENDIAN      __BIG_ENDIAN
#define BYTE_ORDER      __BYTE_ORDER
#endif
