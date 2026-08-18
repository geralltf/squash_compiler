typedef struct { int img_x, img_y, img_n; } ctx;
static void *f(int *x, int *y, int *n, ctx *s, int cond) {
    void *result = 0;
    if (cond) {
        *x = s->img_x;
        *y = s->img_y;
        if (n) {
            *n = s->img_n;
        }
    }
    return result;
}
int main(void) {
    ctx c; c.img_x=1;c.img_y=2;c.img_n=3;
    int x=0,y=0,n=0;
    f(&x,&y,&n,&c,1);
    return y;
}
