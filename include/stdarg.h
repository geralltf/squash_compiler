#ifndef _STDARG_H
#define _STDARG_H
/* Minimal stdarg.h - variadic args use compiler intrinsics */
typedef char *va_list;
#define va_start(ap,last) (ap = (char*)(&(last)) + sizeof(last))
#define va_arg(ap,type) (*(type*)((ap += sizeof(type)) - sizeof(type)))
#define va_end(ap) (ap = 0)
#endif
