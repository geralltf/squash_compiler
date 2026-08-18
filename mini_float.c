int printf(const char *fmt, ...);
int main(void) {
    double pi = 3.14159;
    double e  = 2.71828;
    double s  = pi + e;
    int r = (int)(s * 100.0);
    printf("pi=%d e=%d s=%d r=%d\n", (int)(pi*100), (int)(e*100), (int)(s*100), r);
    return r == 585 ? 0 : 1;
}
