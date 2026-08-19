/* A tiny local-only DNS responder, purely for exercising SQW's real DNS
 * resolver (SQW/dns_resolver.c) end to end without ever touching a real
 * DNS server. Listens on UDP 127.0.0.1:SQS_DNS_PORT (5353, not the
 * privileged real port 53 -- binding 53 needs root) and answers queries
 * for the one reserved test domain SQS_DNS_TEST_DOMAIN ("sqs.test" --
 * ".test" is an IANA-reserved TLD set aside specifically for testing,
 * guaranteed to never resolve on the real internet) with an A record
 * pointing at 127.0.0.1. Any other name gets a real, well-formed response
 * with zero answers (NXDOMAIN-shaped, not a real RCODE=3 -- good enough
 * for this test tool) rather than being ignored, so a resolver querying
 * for the wrong name gets a clean negative rather than a timeout.
 *
 * #include-d directly into sqs_main.c, same single-TU convention as
 * SQS/php_mini.c. Written entirely with plain local byte-array indexing
 * (unsigned char buf[i]), matching SQW/dns_resolver.c's own convention --
 * see that file's top comment on the real squash codegen bug this avoids
 * (comparing a dereferenced struct-field pointer against a literal is
 * unreliable; plain array indexing is unaffected). */
#include <stdio.h>
#include <string.h>

#define SQS_DNS_PORT 5353
#define SQS_DNS_TEST_DOMAIN "sqs.test"
#define SQS_DNS_BUF_MAX 512

/* Decodes a DNS-wire-format name (length-prefixed labels, terminated by a
 * zero length byte) starting at buf[pos] into a dotted string. Only needs
 * to handle a real client's OWN query, which never contains a compression
 * pointer in its question section, but bails safely (returns an empty
 * name) if one is seen anyway rather than mis-parsing. Returns the
 * position right after the name. */
static int sqs_dns_decode_name(const unsigned char *buf, int pos, int buflen, char *out, int outcap) {
    int oi = 0;
    for (;;) {
        if (pos >= buflen) { out[0] = 0; return pos; }
        unsigned char len = buf[pos];
        if (len == 0) { pos += 1; break; }
        if ((len & 0xC0) == 0xC0) { out[0] = 0; return pos + 2; }
        pos += 1;
        if (oi > 0 && oi < outcap - 1) out[oi++] = '.';
        int k;
        for (k = 0; k < (int)len && pos < buflen; k++) {
            if (oi < outcap - 1) out[oi++] = (char)buf[pos];
            pos++;
        }
    }
    out[oi] = 0;
    return pos;
}

static void sqs_dns_put_u16(unsigned char *buf, int pos, unsigned int val) {
    buf[pos] = (unsigned char)((val >> 8) & 0xFF);
    buf[pos + 1] = (unsigned char)(val & 0xFF);
}

/* Case-insensitive compare, since DNS names are case-insensitive and a
 * real resolver may send either case. */
static int sqs_dns_name_eq(const char *a, const char *b) {
    int i;
    for (i = 0; ; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        if (ca == 0) return 1;
    }
}

static void *sqs_dns_serve(void *arg) {
    (void)arg;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { fprintf(stderr, "SQS-DNS: socket() failed\n"); return NULL; }

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SQS_DNS_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        fprintf(stderr, "SQS-DNS: bind() failed on 127.0.0.1:%d\n", SQS_DNS_PORT);
        return NULL;
    }
    fprintf(stderr, "SQS-DNS: ready on 127.0.0.1:%d (test domain \"%s\" -> 127.0.0.1)\n",
        SQS_DNS_PORT, SQS_DNS_TEST_DOMAIN);
    fflush(stderr);

    for (;;) {
        unsigned char req[SQS_DNS_BUF_MAX];
        struct sockaddr_in peer;
        socklen_t peerlen = sizeof peer;
        long n = recvfrom(fd, req, sizeof req, 0, (struct sockaddr *)&peer, &peerlen);
        if (n < 12) continue;

        char qname[256];
        int qend = sqs_dns_decode_name(req, 12, (int)n, qname, sizeof qname);
        if (qend + 4 > (int)n) continue; /* malformed: no room for QTYPE/QCLASS */

        char peer_ip[64];
        inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof peer_ip);
        int match = sqs_dns_name_eq(qname, SQS_DNS_TEST_DOMAIN);
        fprintf(stderr, "SQS-DNS: query for \"%s\" from %s:%d -> %s\n",
            qname, peer_ip, (int)ntohs(peer.sin_port), match ? "127.0.0.1" : "(no match)");
        fflush(stderr);

        unsigned char resp[SQS_DNS_BUF_MAX];
        int rlen = 0;
        /* Header: echo the client's own ID, QR=1 (response) + RD + RA,
         * RCODE=0, QDCOUNT=1 (we echo the question back), ANCOUNT=1 if
         * matched else 0, NS/AR COUNT=0. */
        resp[0] = req[0]; resp[1] = req[1];
        resp[2] = 0x81; resp[3] = 0x80;
        sqs_dns_put_u16(resp, 4, 1);
        sqs_dns_put_u16(resp, 6, match ? 1 : 0);
        sqs_dns_put_u16(resp, 8, 0);
        sqs_dns_put_u16(resp, 10, 0);
        rlen = 12;

        /* Echo the question section verbatim (name + QTYPE + QCLASS) --
         * real DNS responses always include the question they're
         * answering. */
        int qsec_len = qend + 4 - 12;
        memcpy(resp + rlen, req + 12, (size_t)qsec_len);
        rlen += qsec_len;

        if (match) {
            /* Answer RR: NAME as a compression pointer back to the
             * question's name at offset 12 (0xC00C, standard technique),
             * TYPE=1 (A), CLASS=1 (IN), TTL=60, RDLENGTH=4, RDATA=127.0.0.1. */
            resp[rlen++] = 0xC0; resp[rlen++] = 0x0C;
            sqs_dns_put_u16(resp, rlen, 1); rlen += 2;
            sqs_dns_put_u16(resp, rlen, 1); rlen += 2;
            resp[rlen++] = 0; resp[rlen++] = 0; resp[rlen++] = 0; resp[rlen++] = 60;
            sqs_dns_put_u16(resp, rlen, 4); rlen += 2;
            resp[rlen++] = 127; resp[rlen++] = 0; resp[rlen++] = 0; resp[rlen++] = 1;
        }

        sendto(fd, resp, (unsigned long)rlen, 0, (struct sockaddr *)&peer, peerlen);
    }
    return NULL;
}
