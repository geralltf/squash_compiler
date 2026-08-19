/* Implementation of dns_resolver.h -- see that header's own comment on
 * scope (a real, hand-rolled RFC 1035 UDP DNS client). #include-d
 * directly into sqw_main.c, same single-TU convention as every other
 * SQW/*.c file (see sqw_main.c's own comment on the cross-object-link bug
 * this sidesteps).
 *
 * Written entirely with plain local byte-array indexing (unsigned char
 * buf[i]), never a "struct-field-via-arrow" pointer dereferenced in a
 * comparison -- see net_client.c/php_mini.c's own comments on the real
 * squash codegen bug that makes that pattern unreliable. Plain array
 * indexing is unaffected by that bug and is the natural way to write
 * wire-format parsing anyway.
 *
 * Local-only test hook: if the SQW_DNS_SERVER environment variable is set
 * (format "IP:PORT", e.g. "127.0.0.1:5353"), every resolution goes to
 * THAT server instead of the real one named in /etc/resolv.conf -- this
 * is how this resolver gets exercised against SQS's own tiny local DNS
 * responder (SQS/sqs_dns.c, answers the reserved "sqs.test" test domain
 * with 127.0.0.1) without ever touching a real DNS server. Unset, this
 * resolver behaves exactly as before: the real system resolver, real
 * public fallback. */
#include "dns_resolver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DNS_BUF_MAX 512
#define DNS_DEFAULT_RESOLVER "8.8.8.8"
#define DNS_DEFAULT_PORT 53

/* Determines which resolver (IP + port) to query: SQW_DNS_SERVER if set
 * (this session's local-test override, see this file's top comment),
 * else the first "nameserver X.X.X.X" line in /etc/resolv.conf on port
 * 53, else the well-known public fallback DNS_DEFAULT_RESOLVER on port
 * 53. */
static void dns_get_resolver(char *out_ip, int out_ip_cap, int *out_port) {
    *out_port = DNS_DEFAULT_PORT;

    const char *override = getenv("SQW_DNS_SERVER");
    if (override && override[0]) {
        const char *colon = strchr(override, ':');
        int iplen = colon ? (int)(colon - override) : (int)strlen(override);
        if (iplen >= out_ip_cap) iplen = out_ip_cap - 1;
        memcpy(out_ip, override, (size_t)iplen);
        out_ip[iplen] = 0;
        if (colon) *out_port = atoi(colon + 1);
        return;
    }

    FILE *fp = fopen("/etc/resolv.conf", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof line, fp)) {
            if (strncmp(line, "nameserver", 10) == 0) {
                const char *p = line + 10;
                while (*p == ' ' || *p == '\t') p++;
                int i = 0;
                while (p[i] && p[i] != '\n' && p[i] != '\r' && p[i] != ' ' && i < out_ip_cap - 1) {
                    out_ip[i] = p[i];
                    i++;
                }
                out_ip[i] = 0;
                fclose(fp);
                if (out_ip[0]) return;
                break;
            }
        }
        fclose(fp);
    }
    strncpy(out_ip, DNS_DEFAULT_RESOLVER, out_ip_cap - 1);
    out_ip[out_ip_cap - 1] = 0;
}

/* Encodes "example.com" as DNS wire-format labels: 3 'e' 'x' 'a' 'm' 'p'
 * 'l' 'e' 3 'c' 'o' 'm' 0. Returns the number of bytes written. */
static int dns_encode_name(const unsigned char *hostname, unsigned char *out) {
    int out_i = 0;
    int label_start = 0;
    int i = 0;
    for (;;) {
        unsigned char c = hostname[i];
        if (c == '.' || c == 0) {
            int label_len = i - label_start;
            if (label_len > 0 && label_len <= 63) {
                out[out_i++] = (unsigned char)label_len;
                int k;
                for (k = label_start; k < i; k++) out[out_i++] = hostname[k];
            }
            label_start = i + 1;
            if (c == 0) break;
        }
        i++;
    }
    out[out_i++] = 0;
    return out_i;
}

/* Skips one DNS-wire-format name starting at buf[pos] (handling a single
 * compression pointer, 0xC0 high bits), returning the position right
 * after it. Does not decode the name itself -- callers here only ever
 * need to skip past it. */
static int dns_skip_name(const unsigned char *buf, int pos) {
    for (;;) {
        unsigned char len = buf[pos];
        if (len == 0) { pos += 1; return pos; }
        if ((len & 0xC0) == 0xC0) { pos += 2; return pos; }
        pos += 1 + len;
    }
}

static unsigned int dns_get_u16(const unsigned char *buf, int pos) {
    return ((unsigned int)buf[pos] << 8) | (unsigned int)buf[pos + 1];
}

int sqw_dns_resolve(const char *hostname, char *out_ip, int out_ip_cap) {
    char resolver_ip[64];
    int resolver_port;
    dns_get_resolver(resolver_ip, sizeof resolver_ip, &resolver_port);

    unsigned char query[DNS_BUF_MAX];
    int qi = 0;
    /* Header: ID, flags (standard query, recursion desired), QDCOUNT=1,
     * AN/NS/AR COUNT=0. */
    query[qi++] = 0x13; query[qi++] = 0x37; /* ID, arbitrary fixed value */
    query[qi++] = 0x01; query[qi++] = 0x00; /* flags: RD=1 */
    query[qi++] = 0x00; query[qi++] = 0x01; /* QDCOUNT=1 */
    query[qi++] = 0x00; query[qi++] = 0x00; /* ANCOUNT=0 */
    query[qi++] = 0x00; query[qi++] = 0x00; /* NSCOUNT=0 */
    query[qi++] = 0x00; query[qi++] = 0x00; /* ARCOUNT=0 */

    qi += dns_encode_name((const unsigned char *)hostname, query + qi);
    query[qi++] = 0x00; query[qi++] = 0x01; /* QTYPE=A */
    query[qi++] = 0x00; query[qi++] = 0x01; /* QCLASS=IN */

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;

    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)resolver_port);
    addr.sin_addr.s_addr = inet_addr(resolver_ip);

    long sent = sendto(fd, query, (unsigned long)qi, 0, (struct sockaddr *)&addr, sizeof addr);
    if (sent != qi) { close(fd); return 0; }

    unsigned char resp[DNS_BUF_MAX];
    long n = recvfrom(fd, resp, sizeof resp, 0, (void *)0, (void *)0);
    close(fd);
    if (n < 12) return 0;

    unsigned int ancount = dns_get_u16(resp, 6);
    if (ancount < 1) return 0;

    int pos = 12;
    pos = dns_skip_name(resp, pos); /* skip the question's own name */
    pos += 4; /* QTYPE + QCLASS */

    unsigned int i;
    for (i = 0; i < ancount && pos < (int)n; i++) {
        pos = dns_skip_name(resp, pos);
        if (pos + 10 > (int)n) return 0;
        unsigned int rtype = dns_get_u16(resp, pos);
        unsigned int rdlen = dns_get_u16(resp, pos + 8);
        pos += 10;
        if (rtype == 1 && rdlen == 4 && pos + 4 <= (int)n) {
            snprintf(out_ip, (size_t)out_ip_cap, "%u.%u.%u.%u",
                resp[pos], resp[pos + 1], resp[pos + 2], resp[pos + 3]);
            return 1;
        }
        pos += (int)rdlen;
    }
    return 0;
}
