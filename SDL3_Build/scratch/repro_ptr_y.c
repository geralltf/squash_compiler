typedef struct { int img_y; } ctx;
static int f(ctx *j, int *x, int *y, int *comp) {
    if (x) *x = j->img_y;
    if (y) *y = j->img_y;
    if (comp) *comp = 3;
    return 1;
}
int main(void) {
    ctx c; c.img_y = 42;
    int x=0,y=0,comp=0;
    f(&c, &x, &y, &comp);
    return y;
}
