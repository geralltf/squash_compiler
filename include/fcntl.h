#ifndef _FCNTL_H
#define _FCNTL_H

/* Values match macOS/BSD <sys/fcntl.h>. */
#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_ACCMODE  0x0003
#define O_NONBLOCK 0x00000004
#define O_APPEND   0x00000008
#define O_CREAT    0x00000200
#define O_TRUNC    0x00000400
#define O_EXCL     0x00000800
#define O_CLOEXEC  0x01000000

#define F_DUPFD     0
#define F_GETFD     1
#define F_SETFD     2
#define F_GETFL     3
#define F_SETFL     4
#define F_GETOWN    5
#define F_SETOWN    6
#define F_GETLK     7
#define F_SETLK     8
#define F_SETLKW    9
#define F_FULLFSYNC 51

#define FD_CLOEXEC 1

/* extern, not a plain bodyless prototype: real libc/libSystem exports with
 * no body anywhere squash ever compiles -- see include/unistd.h's header
 * comment for why "extern" (routing through codegen's SYM_IMPORT path)
 * matters instead of a plain prototype here. */
extern int open(const char *path, int oflag, ...);
extern int fcntl(int fd, int cmd, ...);

#endif
