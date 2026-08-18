int printf(const char *fmt, ...);
int main(void) {
    double x = 3.14159;
    int as_int = (int)x;
    double y = (double)7;
    int yi = (int)y;
    printf("as_int=%d yi=%d\n", as_int, yi);
    return 0;
}
