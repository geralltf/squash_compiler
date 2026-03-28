#ifndef _CTYPE_H
#define _CTYPE_H

/* Character classification - implemented as expressions, no function calls needed */
#define isdigit(c)  ((c)>='0'&&(c)<='9')
#define isxdigit(c) (isdigit(c)||((c)>='a'&&(c)<='f')||((c)>='A'&&(c)<='F'))
#define isalpha(c)  (((c)>='a'&&(c)<='z')||((c)>='A'&&(c)<='Z'))
#define isalnum(c)  (isalpha(c)||isdigit(c))
#define isspace(c)  ((c)==' '||(c)=='\t'||(c)=='\n'||(c)=='\r'||(c)=='\f'||(c)=='\v')
#define isupper(c)  ((c)>='A'&&(c)<='Z')
#define islower(c)  ((c)>='a'&&(c)<='z')
#define isprint(c)  ((c)>=' '&&(c)<='~')
#define ispunct(c)  (isprint(c)&&!isalnum(c)&&(c)!=' ')
#define toupper(c)  (islower(c)?((c)-32):(c))
#define tolower(c)  (isupper(c)?((c)+32):(c))

#endif
