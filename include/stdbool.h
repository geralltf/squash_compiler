#ifndef _STDBOOL_H
#define _STDBOOL_H

/* C99/C11 bool support. _Bool is a genuinely 1-byte type in real C (and in
 * every real ABI squash targets) — it must NOT be a 4-byte int. Structs
 * with "bool" fields interleaved among other fields (extremely common in
 * real-world C, e.g. all of SDL3's core structs) get every field AFTER the
 * first bool laid out at the wrong offset otherwise, since this typedef
 * feeds directly into squash's struct-size/field-offset computation. */
typedef unsigned char _Bool;
typedef _Bool bool;
#define true  1
#define false 0
#define __bool_true_false_are_defined 1

#endif /* _STDBOOL_H */
