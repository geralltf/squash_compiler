int printf(const char *fmt, ...);
int main(void) {
    double pi = 3.14159;
    double e  = 2.71828;
    int gt = (pi > e);
    int ne = (pi != e);
    int add_int = (int)((pi + e) * 100.0);
    printf("gt=%d ne=%d add=%d\n", gt, ne, add_int);
    return 0;
}
