#ifndef _NETINET_IN_H
#define _NETINET_IN_H
/* IPv4 address structures, matching the real Linux ABI exactly -- see
 * include/sys/socket.h's own comment on why this is hand-declared rather
 * than assumed. */
#include "include/sys/socket.h"

struct in_addr {
    unsigned int s_addr; /* network byte order */
};

/* Real size 16 bytes, padded with sin_zero to match struct sockaddr's own
 * 16-byte size -- bind()/connect() take a struct sockaddr*, so the two
 * must be layout-compatible when a caller casts &addr_in to
 * (struct sockaddr *). */
struct sockaddr_in {
    sa_family_t    sin_family;
    unsigned short sin_port;   /* network byte order */
    struct in_addr sin_addr;
    unsigned char  sin_zero[8];
};

#define INADDR_ANY 0u

extern unsigned short htons(unsigned short hostshort);
extern unsigned short ntohs(unsigned short netshort);
extern unsigned int   htonl(unsigned int hostlong);
extern unsigned int   ntohl(unsigned int netlong);

#endif /* _NETINET_IN_H */
