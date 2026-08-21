#ifndef _ZLIB_H
#define _ZLIB_H
/* Minimal hand-declared surface of the real system zlib (linked via "-lz",
 * same convention as include/openssl/ssl.h -- hand-matched to the real ABI
 * rather than pulling in the genuine system zlib.h, which relies on macros
 * squash's C frontend doesn't need to support. Only the raw inflate() API
 * is declared: PNG's IDAT stream is plain zlib-wrapped DEFLATE, decoded via
 * inflate()'s ordinary return-code error handling (Z_OK/Z_STREAM_END/...),
 * NOT libpng's setjmp-based error API -- see SQW/img_decode_png.c's own
 * top comment for why this project binds zlib directly instead of libpng.
 *
 * z_stream's field layout below matches the real zlib.h exactly (verified
 * against zlib 1.2/1.3's public struct, unchanged across those versions on
 * Linux LP64: uLong/size_t-width fields are 8 bytes, same as this project's
 * only target here, Makefile.SQW.linux's "-linux -64"). Only the fields
 * this decoder actually touches (next_in/avail_in/next_out/avail_out) are
 * ever read after inflateInit_(); the rest just need to occupy the right
 * offsets so real zlib's own internal reads/writes into this struct land
 * correctly. */
#include "include/stddef.h"

typedef unsigned char Bytef;
typedef unsigned int uInt;
typedef unsigned long uLong;

typedef void *(*sqw_zlib_alloc_func)(void *opaque, unsigned int items, unsigned int size);
typedef void (*sqw_zlib_free_func)(void *opaque, void *address);

struct internal_state;

typedef struct z_stream_s {
    Bytef    *next_in;
    uInt      avail_in;
    uLong     total_in;

    Bytef    *next_out;
    uInt      avail_out;
    uLong     total_out;

    const char *msg;
    struct internal_state *state;

    sqw_zlib_alloc_func zalloc;
    sqw_zlib_free_func  zfree;
    void               *opaque;

    int   data_type;
    uLong adler;
    uLong reserved;
} z_stream;

typedef z_stream *z_streamp;

#define Z_OK            0
#define Z_STREAM_END    1
#define Z_NEED_DICT     2
#define Z_ERRNO       (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR  (-3)
#define Z_MEM_ERROR   (-4)
#define Z_BUF_ERROR   (-5)
#define Z_VERSION_ERROR (-6)

#define Z_NO_FLUSH 0
#define Z_FINISH   4

#define ZLIB_VERSION "1.2.11"

/* Real exported symbols are inflateInit_/inflateInit2_ (the "Init"/"Init2"
 * names are macros in real zlib.h that splice in the version string and
 * sizeof(z_stream) -- redefined the same way here, not as fake standalone
 * functions, so this compiles to the exact same real call a normal C
 * program linking real zlib would make, matching include/openssl/ssl.h's
 * own SSL_CTX_set_min_proto_version precedent. */
extern int inflateInit_(z_streamp strm, const char *version, int stream_size);
extern int inflate(z_streamp strm, int flush);
extern int inflateEnd(z_streamp strm);

#define inflateInit(strm) inflateInit_((strm), ZLIB_VERSION, (int)sizeof(z_stream))

#endif /* _ZLIB_H */
