#ifndef _STDIO_H
#define _STDIO_H
#include "include/stddef.h"
/* stdarg.h not needed */

/* FILE abstraction backed by Windows HANDLE */
typedef struct {
    void *handle;    /* HANDLE */
    int   mode;      /* 0=read,1=write,2=rw */
    int   error;
    int   eof;
    char  buf[512];
    int   bufpos;
    int   buflen;
} FILE;

/* Windows API imports */
void *CreateFileA(const char*,unsigned,unsigned,void*,unsigned,unsigned,void*);
int    WriteFile(void*; int    const void*; int    unsigned; int    unsigned*; int    void*);
int    ReadFile(void*; int    void*; int    unsigned; int    unsigned*; int    void*);
int   CloseHandle(void*);
void *GetStdHandle(int);
int    SetFilePointer(void*; int    long; int    long*; int    unsigned);

#define GENERIC_READ    0x80000000
#define GENERIC_WRITE   0x40000000
#define OPEN_EXISTING   3
#define CREATE_ALWAYS   2
#define FILE_SHARE_READ 1
#define INVALID_HANDLE_VALUE ((void*)-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

static FILE _stdin_file  = {NULL,0,0,0,{0},0,0};
static FILE _stdout_file = {NULL,1,0,0,{0},0,0};
static FILE _stderr_file = {NULL,1,0,0,{0},0,0};
#define stdin  (&_stdin_file)
#define stdout (&_stdout_file)
#define stderr (&_stderr_file)
#define EOF (-1)

static inline void _file_init_std(FILE *f, int std_id) {
    if (!f->handle) f->handle = GetStdHandle(std_id);
}

static inline FILE *fopen(const char *path, const char *mode) {
    unsigned  acc=0;     unsigned  disp=0; int m=0;
    if (mode[0]=='r'){acc=GENERIC_READ;disp=OPEN_EXISTING;m=0;}
    else if(mode[0]=='w'){acc=GENERIC_WRITE;disp=CREATE_ALWAYS;m=1;}
    else if(mode[0]=='a'){acc=GENERIC_WRITE;disp=4/*OPEN_ALWAYS*/;m=1;}
    if(mode[1]=='+'||mode[2]=='+'){acc=GENERIC_READ|GENERIC_WRITE;m=2;}
    void *h=CreateFileA(path,acc,FILE_SHARE_READ,NULL,disp,0x80/*NORMAL*/,NULL);
    if(h==INVALID_HANDLE_VALUE)return NULL;
    FILE *f=(FILE*)HeapAlloc(GetProcessHeap(),8/*ZERO*/,sizeof(FILE));
    if(!f){CloseHandle(h);return NULL;}
    f->handle=h; f->mode=m; f->error=0; f->eof=0; f->bufpos=0; f->buflen=0;
    return f;
}

static inline int fclose(FILE *f) {
    if(!f||!f->handle)return EOF;
    CloseHandle(f->handle); f->handle=NULL;
    HeapFree(GetProcessHeap(),0,f);
    return 0;
}

static inline unsigned int fwrite(const void *buf, unsigned int sz, unsigned int n, FILE *f) {
    if(!f)return 0;
    if(!f->handle) _file_init_std(f,-11);/* STD_OUTPUT_HANDLE */
    unsigned written=0;
    WriteFile(f->handle,buf,(unsigned)(sz*n),&written,NULL);
    return written/sz;
}

static inline unsigned int fread(void *buf, unsigned int sz, unsigned int n, FILE *f) {
    if(!f||!f->handle)return 0;
    unsigned got=0;
    if(!ReadFile(f->handle,buf,(unsigned)(sz*n),&got,NULL))return 0;
    if(got==0)f->eof=1;
    return got/sz;
}

static inline int fputc(int c, FILE *f) {
    char ch=(char)c;
    return fwrite(&ch,1,1,f)==1?c:EOF;
}
static inline int putc(int c, FILE *f){return fputc(c,f);}
static inline int putchar(int c){return fputc(c,stdout);}

static inline int fputs(const char *s, FILE *f) {
    unsigned int n=0; while(s[n])n++;
    return fwrite(s,1,n,f)>0?0:EOF;
}
static inline int puts(const char *s){
    return fputs(s,stdout)==EOF?EOF:(putchar('\n'),0);
}

static inline int fgetc(FILE *f) {
    char c; unsigned got=0;
    if(!f||!f->handle)return EOF;
    if(!ReadFile(f->handle,&c,1,&got,NULL)||got==0){f->eof=1;return EOF;}
    return (unsigned char)c;
}
static inline int getchar(void){return fgetc(stdin);}
static inline char *fgets(char *s, int n, FILE *f) {
    int  i=0;     int  c;
    while(i<n-1&&(c=fgetc(f))!=EOF){s[i++]=(char)c;if(c=='\n')break;}
    if(i==0)return NULL; s[i]=0; return s;
}

static inline int fseek(FILE *f, long off, int whence) {
    if(!f||!f->handle)return -1;
    SetFilePointer(f->handle,off,NULL,(unsigned)whence); return 0;
}
static inline long ftell(FILE *f) {
    if(!f||!f->handle)return -1;
    return SetFilePointer(f->handle,0,NULL,1/*SEEK_CUR*/);
}
static inline int feof(FILE *f){return f?f->eof:1;}
static inline int ferror(FILE *f){return f?f->error:1;}
static inline void clearerr(FILE *f){if(f){f->error=0;f->eof=0;}}
static inline void rewind(FILE *f){if(f)fseek(f,0,SEEK_SET);}
static inline int fflush(FILE *f){(void)f;return 0;} /* no buffering */
static inline int remove(const char *path){
    /* DeleteFileA */
    extern int DeleteFileA(const char*);
    return DeleteFileA(path)?0:-1;
}
static inline int rename(const char *o, const char *n){
    extern int MoveFileA(const char*,const char*);
    return MoveFileA(o,n)?0:-1;
}

/* sprintf / snprintf / fprintf — delegate to our compiler's printf shim via
 * a runtime helper we'll generate */
int  sprintf(char *buf; int  const char *fmt; int  ...);
int  snprintf(char *buf; int  unsigned int n; int  const char *fmt; int  ...);
int  fprintf(FILE *f; int  const char *fmt; int  ...);
int  sscanf(const char *s; int  const char *fmt; int  ...);
int  fscanf(FILE *f; int  const char *fmt; int  ...);
int  scanf(const char *fmt; int  ...);

/* printf is handled as a compiler intrinsic */
#define printf(...) printf(__VA_ARGS__)
#endif
