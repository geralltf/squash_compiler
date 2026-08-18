#ifndef _SYS_STAT_H
#define _SYS_STAT_H
/* Minimal Linux <sys/stat.h> shim. struct stat's field layout below matches
 * the real x86-64/AArch64 Linux glibc ABI exactly (flattening each
 * "struct timespec st_Xtim" pair into two explicit "long" fields instead of
 * a nested struct -- same size/alignment, so the offsets real stat()/
 * fstat() (via libc.so.6) write into are identical) -- this struct is
 * passed BY POINTER to those real libc calls, so an incorrect layout would
 * silently corrupt or misread its fields. */
#include "include/sys/types.h"

struct stat {
    dev_t         st_dev;
    ino_t         st_ino;
    nlink_t       st_nlink;
    mode_t        st_mode;
    uid_t         st_uid;
    gid_t         st_gid;
    unsigned int  __pad0;
    dev_t         st_rdev;
    off_t         st_size;
    blksize_t     st_blksize;
    blkcnt_t      st_blocks;
    long          st_atime;
    long          st_atime_nsec;
    long          st_mtime;
    long          st_mtime_nsec;
    long          st_ctime;
    long          st_ctime_nsec;
    long          __unused[3];
};

/* extern: real syscalls, no body anywhere squash compiles -- see
 * include/unistd.h's comment on the identical fix for why a plain bodyless
 * prototype here is unsafe (codegen.c's cross-object call path can't tell
 * "real external function" apart from "defined in a sibling .sqo" any
 * other way). Confirmed necessary via squash self-hosting itself:
 * linker.c's stat() call was misresolved as an unresolvable static
 * cross-object call once a -c precompile started preferring that path. */
extern int stat(const char *path, struct stat *buf);
extern int fstat(int fd, struct stat *buf);
extern int lstat(const char *path, struct stat *buf);
extern int mkdir(const char *path, mode_t mode);

#define S_IFMT   0170000
#define S_IFSOCK 0140000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000

#define S_ISTYPE(m,t) (((m) & S_IFMT) == (t))
#define S_ISREG(m)  S_ISTYPE(m, S_IFREG)
#define S_ISDIR(m)  S_ISTYPE(m, S_IFDIR)
#define S_ISCHR(m)  S_ISTYPE(m, S_IFCHR)
#define S_ISBLK(m)  S_ISTYPE(m, S_IFBLK)
#define S_ISFIFO(m) S_ISTYPE(m, S_IFIFO)
#define S_ISLNK(m)  S_ISTYPE(m, S_IFLNK)
#define S_ISSOCK(m) S_ISTYPE(m, S_IFSOCK)

#endif
