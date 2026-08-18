#ifndef _INITGUID_H
#define _INITGUID_H

/* Including <initguid.h> switches DEFINE_GUID (see shlobj.h) from an
 * `extern const GUID` declaration to an actual definition+initializer for
 * every DEFINE_GUID(...) call that follows, matching the real Windows SDK's
 * initguid.h behavior. */
#ifdef DEFINE_GUID
#undef DEFINE_GUID
#endif
#define INITGUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    const GUID name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }

#endif /* _INITGUID_H */
