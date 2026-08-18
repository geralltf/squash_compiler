#ifndef _SHLOBJ_H
#define _SHLOBJ_H

/* Minimal shim for SDL3's filesystem/windows/SDL_sysfilesystem.c, which
 * needs shlobj.h for: DEFINE_GUID, CSIDL_* constants, SHGetFolderPathW
 * (old, always-present API) and the GUID-based SHGetKnownFolderPath (newer
 * API, loaded dynamically via GetProcAddress at runtime, so it only needs
 * a function-pointer typedef here, not a direct declaration/import). */

#include <windows.h>

/* DEFINE_GUID normally declares an `extern const GUID` (storage/initializer
 * supplied elsewhere) unless <initguid.h> was included first, which flips
 * it to actually define+initialize the GUID at the call site. Matches the
 * real Windows SDK's guiddef.h/initguid.h split. */
#ifndef DEFINE_GUID
#ifdef INITGUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    const GUID name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }
#else
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    extern const GUID name
#endif
#endif

typedef WCHAR *PWSTR;

/* CSIDL_* — pre-Vista special-folder IDs (SHGetFolderPathW) */
#define CSIDL_DESKTOP        0x0000
#define CSIDL_PERSONAL       0x0005
#define CSIDL_MYDOCUMENTS    0x0005
#define CSIDL_MYMUSIC        0x000d
#define CSIDL_MYVIDEO        0x000e
#define CSIDL_TEMPLATES      0x0015
#define CSIDL_APPDATA        0x001a
#define CSIDL_MYPICTURES     0x0027
#define CSIDL_PROFILE        0x0028
#define CSIDL_FLAG_CREATE    0x8000

#define SHGFP_TYPE_CURRENT 0
#define SHGFP_TYPE_DEFAULT 1

HRESULT SHGetFolderPathW(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPWSTR pszPath);

#endif /* _SHLOBJ_H */
