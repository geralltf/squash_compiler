/* spoof_identity.h: the single, shared place that decides "what OS and
 * browser does this binary claim to be" for both SQW (the client, in its
 * outbound User-Agent request header) and SQS (the server, in its
 * outbound Server response header) -- one shared macro so the two can
 * never accidentally drift apart or disagree.
 *
 * Selection is by the REAL platform this binary was actually compiled
 * for (squash defines _WIN32/__APPLE__/__linux__ the same way a real C
 * compiler would -- see lexer.c's own platform-macro comment), and
 * deliberately claims a DIFFERENT OS and browser than the one actually
 * doing the compiling/running, per the exact brief:
 *   compiled for Linux   -> claims Windows + Firefox
 *   compiled for Windows -> claims BSD    + Chrome
 *   compiled for macOS   -> claims Linux  + Chrome
 * (squash's -openbsd target is a real fourth option too, but wasn't part
 * of the 3-way brief -- it falls into the same branch as a generic Unix
 * build below, i.e. same treatment as Linux, since __OpenBSD__ implies
 * __unix__ but not __linux__/__APPLE__/_WIN32.)
 *
 * Every string below is a REAL, standards-compliant User-Agent format --
 * an actual Firefox-on-Windows / Chrome-on-FreeBSD / Chrome-on-Linux UA a
 * genuine install of that browser on that OS would send, not an invented
 * or malformed one. "Anonymous" here means "not identifiable as what
 * this binary actually is," not "blank" or non-standard -- a missing or
 * garbage User-Agent is itself a fingerprinting signal (see RFC 9110
 * §10.1.5: User-Agent is conventionally present; omitting it stands out
 * more than a plausible one does).
 *
 * SQS reuses this exact same string as its own Server header (see
 * SQS/sqs_main.c's own comment on that) so a request/response pair from
 * this project's own traffic always presents ONE consistent identity,
 * never a mix of "spoofed client, unspoofed/absent server" that could
 * itself be a distinguishing signal. Neither SQW nor SQS sends any other
 * header that reveals real platform/build identity (no X-Powered-By, no
 * real hostname/version banners) -- this is the only place either one
 * says anything about what it is. */
#ifndef SQ_SPOOF_IDENTITY_H
#define SQ_SPOOF_IDENTITY_H

#if defined(_WIN32)
/* Compiled for Windows -> claim BSD + Chrome (real Chrome-on-FreeBSD UA format). */
#define SQ_SPOOF_IDENTITY "Mozilla/5.0 (X11; FreeBSD amd64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"
#elif defined(__APPLE__)
/* Compiled for macOS -> claim Linux + Chrome (real Chrome-on-Linux UA format). */
#define SQ_SPOOF_IDENTITY "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"
#else
/* Compiled for Linux (or -openbsd/any other __unix__ target -- see this
 * file's own top comment) -> claim Windows + Firefox (real
 * Firefox-on-Windows UA format). */
#define SQ_SPOOF_IDENTITY "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:132.0) Gecko/20100101 Firefox/132.0"
#endif

#endif /* SQ_SPOOF_IDENTITY_H */
