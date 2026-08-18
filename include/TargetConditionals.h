#ifndef _TARGETCONDITIONALS_H
#define _TARGETCONDITIONALS_H
/* Minimal shim: squash only ever targets desktop macOS (see -macos in
 * compiler.c), so this hardcodes the "real Mac, not a simulator/other Apple
 * platform" branch rather than pulling in Apple's real header (which uses
 * clang-specific machinery squash's preprocessor doesn't implement). */
#define TARGET_OS_MAC        1
#define TARGET_OS_OSX        1
#define TARGET_OS_IPHONE     0
#define TARGET_OS_IOS        0
#define TARGET_OS_TV         0
#define TARGET_OS_WATCH      0
#define TARGET_OS_VISION     0
#define TARGET_OS_SIMULATOR  0
#define TARGET_OS_EMBEDDED   0
#define TARGET_OS_MACCATALYST 0
#define TARGET_OS_UIKITFORMAC 0
#define TARGET_OS_WIN32       0
#define TARGET_OS_LINUX       0

#if defined(__aarch64__)
#define TARGET_CPU_ARM64  1
#define TARGET_CPU_X86_64 0
#else
#define TARGET_CPU_ARM64  0
#define TARGET_CPU_X86_64 1
#endif
#define TARGET_RT_64_BIT 1
#define TARGET_RT_BIG_ENDIAN 0
#define TARGET_RT_LITTLE_ENDIAN 1

#endif
