#ifndef DLFCN_H
#define DLFCN_H

#define RTLD_LAZY   1
#define RTLD_NOW    2
#define RTLD_GLOBAL 256
#define RTLD_LOCAL  0

/* extern (not a plain bodyless prototype): real libc/libSystem exports with
 * no body anywhere squash ever compiles -- see include/unistd.h's header
 * comment for why "extern" (routing through codegen's SYM_IMPORT path)
 * matters here instead of a plain prototype, which a "-c" precompiled .sqo
 * would instead speculatively treat as an unresolved cross-object call. */
extern void *dlopen (const char *filename, int flags);
extern void *dlsym  (void *handle, const char *symbol);
extern int   dlclose(void *handle);
extern char *dlerror(void);

#endif /* DLFCN_H */
