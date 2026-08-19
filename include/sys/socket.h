#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H
/* BSD sockets, hand-declared to match the real Linux/glibc ABI exactly --
 * same convention as include/pthread.h and include/unistd.h (this is a
 * real libc.so.6 export surface squash links against at runtime, not an
 * internal squash type), added for SQW's HTTP(S) client (net_client.c)
 * and SQS's HTTP(S) server (SQS/sqs_main.c). Only the calls/constants
 * those two actually use -- not a complete <sys/socket.h>. */
#include "include/sys/types.h"

typedef unsigned int socklen_t;
typedef unsigned short sa_family_t;

/* Generic sockaddr -- real size is 16 bytes on Linux (2-byte family +
 * 14 bytes of family-specific data), matched exactly since this is what
 * accept()/connect() etc. actually read/write through a cast pointer. */
struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

#define AF_INET     2
#define SOCK_STREAM 1
#define SOL_SOCKET  1
#define SO_REUSEADDR 2
#define SHUT_RDWR   2

extern int socket(int domain, int type, int protocol);
extern int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
extern int listen(int sockfd, int backlog);
extern int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
extern int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
extern long send(int sockfd, const void *buf, unsigned long len, int flags);
extern long recv(int sockfd, void *buf, unsigned long len, int flags);
extern int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
extern int shutdown(int sockfd, int how);

#endif /* _SYS_SOCKET_H */
