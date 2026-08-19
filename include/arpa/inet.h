#ifndef _ARPA_INET_H
#define _ARPA_INET_H
#include "include/netinet/in.h"

extern unsigned int inet_addr(const char *cp);
extern int inet_pton(int af, const char *src, void *dst);
extern const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);

#endif /* _ARPA_INET_H */
