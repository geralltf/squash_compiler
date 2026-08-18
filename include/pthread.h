#ifndef PTHREAD_H
#define PTHREAD_H

typedef unsigned long pthread_t;

/* These are all opaque objects that the C library writes through — the
 * only thing that matters is that they are AT LEAST as large as the real
 * implementation's, since pthread_*_init() writes the full object. Get it
 * wrong and the write runs off the end of a stack local and corrupts
 * whatever follows, with no diagnostic anywhere.
 *
 * glibc and Apple's libpthread disagree on every one of these sizes, so
 * they have to be picked per platform. The macOS numbers come straight
 * from <sys/_pthread/_pthread_types.h>: each object there is a `long`
 * signature word followed by char __opaque[__PTHREAD_*_SIZE__], and the
 * 64-bit sizes are ATTR 56, MUTEX 56, COND 40, MUTEXATTR 8, CONDATTR 8,
 * ONCE 8 — so 64, 64, 48, 16, 16 and 16 bytes in total, i.e. the long
 * counts below. */
#ifdef __APPLE__
typedef struct { long a; long b; long c; long d; long e; long f; long g; long h; } pthread_mutex_t;
typedef struct { long a; long b; long c; long d; long e; long f; } pthread_cond_t;
typedef struct { long a; long b; long c; long d; long e; long f; long g; long h; } pthread_attr_t;
typedef struct { long a; long b; } pthread_mutexattr_t;
typedef struct { long a; long b; } pthread_condattr_t;
typedef struct { long a; long b; } pthread_once_t;
typedef unsigned long pthread_key_t;

/* Apple's statically-initialized forms are NOT all-zero: libpthread checks
 * a signature word to tell an initialized-but-untouched object from
 * uninitialized memory. A {0} mutex is rejected at first lock. Values from
 * <pthread/pthread_impl.h>. */
#define PTHREAD_MUTEX_INITIALIZER  {0x32AAABA7, 0}
#define PTHREAD_COND_INITIALIZER   {0x3CB0B1BB, 0}
#define PTHREAD_ONCE_INIT          {0x30B1BCBA, 0}
#else
typedef struct { long a; long b; long c; long d; long e; } pthread_mutex_t;
typedef struct { long a; long b; long c; long d; long e; long f; } pthread_cond_t;
typedef struct { long a; long b; long c; long d; long e; long f; long g; } pthread_attr_t;
typedef int   pthread_mutexattr_t;
typedef int   pthread_condattr_t;
typedef unsigned int pthread_key_t;
typedef int   pthread_once_t;

#define PTHREAD_MUTEX_INITIALIZER  {0}
#define PTHREAD_COND_INITIALIZER   {0}
#define PTHREAD_ONCE_INIT          0
#endif

/* extern (not a plain bodyless prototype): real libc/libSystem exports with
 * no body anywhere squash ever compiles -- see include/unistd.h's header
 * comment for why "extern" (routing through codegen's SYM_IMPORT path)
 * matters here instead of a plain prototype, which a "-c" precompiled .sqo
 * (like SDL3_Build/sdl_common.sqo) would instead speculatively treat as an
 * unresolved cross-object call and fail to link. */
extern int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *start_routine, void *arg);
extern int pthread_join(pthread_t thread, void **retval);
extern int pthread_detach(pthread_t thread);
extern pthread_t pthread_self(void);
extern int pthread_equal(pthread_t t1, pthread_t t2);
extern void pthread_exit(void *retval);

extern int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
extern int pthread_mutex_destroy(pthread_mutex_t *mutex);
extern int pthread_mutex_lock(pthread_mutex_t *mutex);
extern int pthread_mutex_trylock(pthread_mutex_t *mutex);
extern int pthread_mutex_unlock(pthread_mutex_t *mutex);

extern int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
extern int pthread_cond_destroy(pthread_cond_t *cond);
extern int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
extern int pthread_cond_signal(pthread_cond_t *cond);
extern int pthread_cond_broadcast(pthread_cond_t *cond);

extern int pthread_attr_init(pthread_attr_t *attr);
extern int pthread_attr_destroy(pthread_attr_t *attr);
extern int pthread_attr_setstacksize(pthread_attr_t *attr, int stacksize);
extern int pthread_attr_getstacksize(const pthread_attr_t *attr, int *stacksize);
extern int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate);

extern int pthread_mutexattr_init(pthread_mutexattr_t *attr);
extern int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
extern int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);
#define PTHREAD_MUTEX_RECURSIVE 1

#define PTHREAD_CREATE_JOINABLE  0
#define PTHREAD_CREATE_DETACHED  1

#endif
