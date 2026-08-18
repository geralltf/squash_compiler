#ifndef _AVAILABILITYMACROS_H
#define _AVAILABILITYMACROS_H
/* Minimal shim covering only what SDL3's build config actually branches on
 * (MAC_OS_X_VERSION_MIN_REQUIRED/MAX_ALLOWED range checks, and #ifdef
 * MAC_OS_X_VERSION_10_8/10_12) -- real Apple headers pull in clang
 * attribute machinery squash's preprocessor doesn't implement. Values are
 * set high enough to enable every modern-macOS code path (this build only
 * ever targets a current host). */
#define MAC_OS_X_VERSION_10_0  1000
#define MAC_OS_X_VERSION_10_1  1010
#define MAC_OS_X_VERSION_10_2  1020
#define MAC_OS_X_VERSION_10_3  1030
#define MAC_OS_X_VERSION_10_4  1040
#define MAC_OS_X_VERSION_10_5  1050
#define MAC_OS_X_VERSION_10_6  1060
#define MAC_OS_X_VERSION_10_7  1070
#define MAC_OS_X_VERSION_10_8  1080
#define MAC_OS_X_VERSION_10_9  1090
#define MAC_OS_X_VERSION_10_10 101000
#define MAC_OS_X_VERSION_10_11 101100
#define MAC_OS_X_VERSION_10_12 101200
#define MAC_OS_X_VERSION_10_13 101300
#define MAC_OS_X_VERSION_10_14 101400
#define MAC_OS_X_VERSION_10_15 101500

#define MAC_OS_X_VERSION_MIN_REQUIRED 101300
#define MAC_OS_X_VERSION_MAX_ALLOWED  150000

#define DEPRECATED_ATTRIBUTE
#define DEPRECATED_MSG_ATTRIBUTE(s)
#define AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER
#define AVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER_BUT_DEPRECATED_IN_MAC_OS_X_VERSION_10_9

#endif
