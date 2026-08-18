#ifndef _DECIMAL_H
#define _DECIMAL_H

/* Decimal floating-point types (C23 / ISO/IEC TS 18661-2) */
/* Simplified: aliased to nearest binary floating-point type */
typedef float       _Decimal32;
typedef double      _Decimal64;
typedef double      _Decimal128;  /* full 128-bit not yet supported — use double */

/* Convenience macros */
#define DECIMAL32_DIG    7
#define DECIMAL64_DIG   16
#define DECIMAL128_DIG  34

#define DEC_INFINITY  (1.0 / 0.0)
#define DEC_NAN       (0.0 / 0.0)

#endif /* _DECIMAL_H */
