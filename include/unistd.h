#ifndef _UNISTD_H
#define _UNISTD_H
#include "include/stddef.h"
#include "include/sys/types.h"

/* extern (not a plain bodyless prototype): these are all real syscalls/
 * libc.so.6 exports with no body anywhere squash ever compiles. Without
 * "extern", codegen.c's cross-object call-emission path (the SYM_FUNC
 * branch, gated on cg->sqo_precompile/cg->prefer_static_calls — see
 * codegen.h's sqo_precompile comment) can't tell "declared but never
 * defined here because it's a real external function" apart from
 * "declared but never defined here because a SIBLING .sqo defines it"
 * (e.g. squash's own my_strdup()) — both look like an identical plain
 * bodyless prototype. "extern" routes these through the OTHER branch
 * instead (SYM_IMPORT, dll=="extern"), which never speculatively picks a
 * deferred cross-object call for a bodyless name. Confirmed necessary via
 * squash self-hosting itself: diag.c's isatty() and linker.c's stat() were
 * both misresolved as unresolvable "static" cross-object calls once a -c
 * precompile started preferring that path for any plain bodyless SYM_FUNC. */
extern int     close(int fd);
extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
extern int     unlink(const char *path);
extern int     rmdir(const char *path);
extern int     access(const char *path, int mode);
extern long    lseek(int fd, long offset, int whence);
extern void   *sbrk(long increment);
extern int     usleep(unsigned int usec);
extern unsigned int sleep(unsigned int seconds);
extern int     isatty(int fd);
extern int     dup(int fd);
extern int     dup2(int oldfd, int newfd);
extern int     pipe(int fds[2]);
extern pid_t   fork(void);
extern pid_t   getpid(void);
extern int     execvp(const char *file, char *const argv[]);

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

/* sysconf() names -- values match real glibc's bits/confname.h exactly,
 * since sysconf() itself is a real libc.so.6 call (see emit_linux_libc_call
 * in codegen.c) whose argument the kernel/libc interprets by this numeric
 * value, not by macro name. */
extern long sysconf(int name);
#define _SC_PAGESIZE                 30
#define _SC_PAGE_SIZE                _SC_PAGESIZE
#define _SC_NPROCESSORS_ONLN         84
#define _SC_NPROCESSORS_CONF         83
#define _SC_PHYS_PAGES               85
#define _SC_LEVEL1_DCACHE_LINESIZE   190
#define _SC_LEVEL1_ICACHE_LINESIZE   188

#endif
