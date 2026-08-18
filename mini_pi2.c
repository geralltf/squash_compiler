int printf(const char *fmt, ...);
int main(void) {
    double pi = 3.14159;
    double e  = 2.71828;
    double sum = pi + e;
    double prod = pi * 100.0;
    int ipi  = (int)(pi * 100.0);
    int ie   = (int)(e  * 100.0);
    int isum = (int)(sum * 100.0);
    printf("ipi=%d ie=%d isum=%d\n", ipi, ie, isum);
    return 0;
}
