/* A real (hand-rolled, not libc getaddrinfo/gethostbyname -- no such
 * declarations exist anywhere in this project's include/ headers) DNS
 * client: builds a real RFC 1035 query packet for an A record, sends it
 * over a real UDP socket to a real resolver, and parses a real response.
 * Exists so sqw_net_parse_url() (SQW/net_client.c) can resolve an actual
 * HOSTNAME in a URL, not just an IPv4 dotted-quad literal.
 *
 * By default this talks to the resolver named by /etc/resolv.conf
 * (falling back to the well-known public resolver 8.8.8.8 if that file
 * can't be read) -- never SQS, never a hardcoded "the web" endpoint. For
 * local-only testing, setting the SQW_DNS_SERVER environment variable
 * (e.g. "127.0.0.1:5353") overrides this and points every resolution at
 * that server instead -- this is how the resolver's real UDP send/recv
 * path is exercised against SQS's own tiny local DNS responder
 * (SQS/sqs_dns.c, answers the reserved "sqs.test" test domain with
 * 127.0.0.1) without ever making a real DNS query. See dns_resolver.c's
 * own top comment for the override's exact behavior. */
#ifndef SQW_DNS_RESOLVER_H
#define SQW_DNS_RESOLVER_H

/* Resolves `hostname` (e.g. "example.com") to its first IPv4 A-record
 * answer, writing the dotted-quad result into `out_ip` (capacity
 * `out_ip_cap`, at least 16 bytes). Returns 1 on success, 0 on any
 * failure (unreachable resolver, timeout, NXDOMAIN, no A record, malformed
 * response, etc -- this is a minimal client, not a fully robust one: no
 * retries, no TCP fallback for truncated responses, no CNAME-chasing
 * beyond one hop). */
int sqw_dns_resolve(const char *hostname, char *out_ip, int out_ip_cap);

#endif
