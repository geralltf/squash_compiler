#ifndef _MATH_H
#define _MATH_H
/* Math functions using x87 FPU inline assembly via compiler intrinsics.
 * In our compiler, we map these to SSE2/x87 instructions via the codegen. */

/* These are declared as external functions linked from our runtime shim.
 * For programs using our compiler, these map to inlined SSE2/x87 sequences. */

double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double  atan2(double y; double  double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double sqrt(double x);
double cbrt(double x);
double  pow(double base; double  double exp);
double exp(double x);
double exp2(double x);
double log(double x);
double log2(double x);
double log10(double x);
double ceil(double x);
double floor(double x);
double round(double x);
double trunc(double x);
double fabs(double x);
double  fmod(double x; double  double y);
double  modf(double x; double  double *intpart);
double  fma(double x; double  double y; double  double z);
double  hypot(double x; double  double y);

float  sinf(float x);
float  cosf(float x);
float  tanf(float x);
float  sqrtf(float x);
float   powf(float b; float   float e);
float  expf(float x);
float  logf(float x);
float  fabsf(float x);
float  floorf(float x);
float  ceilf(float x);
float  roundf(float x);
float   fmodf(float x; float   float y);

#define M_PI        3.14159265358979323846
#define M_PI_2      1.57079632679489661923
#define M_PI_4      0.78539816339744830962
#define M_E         2.71828182845904523536
#define M_LOG2E     1.44269504088896340736
#define M_LOG10E    0.43429448190325182765
#define M_LN2       0.69314718055994530942
#define M_LN10      2.30258509299404568402
#define M_SQRT2     1.41421356237309504880
#define M_SQRT1_2   0.70710678118654752440
#define HUGE_VAL    (1.0/0.0)
#define INFINITY    (1.0f/0.0f)
#define NAN         (0.0f/0.0f)
#define MAXFLOAT    3.40282347e+38F
#define DBL_MAX     1.7976931348623157e+308
#define FLT_MAX     3.40282347e+38F
#define DBL_MIN     2.2250738585072014e-308
#define FLT_MIN     1.17549435e-38F
#define DBL_EPSILON 2.2204460492503131e-16
#define FLT_EPSILON 1.19209290e-07F

static inline int isnan(double x)  {return x!=x;}
static inline int isinf(double x)  {return !isnan(x)&&isnan(x-x);}
static inline int isfinite(double x){return !isnan(x)&&!isinf(x);}
static inline double fdim(double a, double b){return a>b?a-b:0.0;}
static inline double fmax(double a, double b){return a>b?a:b;}
static inline double fmin(double a, double b){return a<b?a:b;}
static inline float fmaxf(float a,float b){return a>b?a:b;}
static inline float fminf(float a,float b){return a<b?a:b;}
#endif
