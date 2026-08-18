#ifndef _STDARG_H
#define _STDARG_H
/* Minimal stdarg.h - variadic args use compiler intrinsics.
 *
 * The callee's prologue spills each incoming variadic register argument
 * into its own fixed 8-byte shadow-space slot (Windows x64 ABI; the SysV
 * register-save area is likewise 8-byte-strided), regardless of the
 * argument's own C type width. So va_arg must always advance by a
 * *minimum* of sizeof(void*) bytes per argument, not by sizeof(type) —
 * e.g. va_arg(ap,int) previously advanced only 4 bytes, which for any
 * second-or-later argument landed ap in the middle of the wrong 8-byte
 * slot (or the upper, typically-zero half of the previous one), reading
 * back 0/garbage instead of the real value. */
typedef char *va_list;
#define _VA_SLOT(type) (sizeof(type) > sizeof(void*) ? sizeof(type) : sizeof(void*))
/* __unix__, not __linux__: this branch is about the SysV AMD64 ABI, which
 * macOS on Intel uses just as much as Linux does (see compiler.c's -macos
 * flag). __unix__ is defined for both. */
#ifdef __unix__
/* SysV/Linux: codegen.c's function prologue homes named (and, for a
 * variadic function, any remaining register-passed) parameters at
 * DESCENDING stack offsets (-8, -16, -24, ... below rbp) -- the opposite
 * direction from the Windows x64 ABI's ascending shadow-space layout the
 * "#else" branch below assumes. So unlike Windows, walking "forward" from
 * the last named parameter here means moving to a MORE NEGATIVE address,
 * not a larger one. */
#define va_start(ap,last) (ap = (char*)(&(last)) - _VA_SLOT(last))
#define va_arg(ap,type) (*(type*)((ap -= _VA_SLOT(type)) + _VA_SLOT(type)))
#else
#define va_start(ap,last) (ap = (char*)(&(last)) + _VA_SLOT(last))
#define va_arg(ap,type) (*(type*)((ap += _VA_SLOT(type)) - _VA_SLOT(type)))
#endif
#define va_end(ap) (ap = 0)
#define va_copy(dst,src) ((dst) = (src))
#endif
