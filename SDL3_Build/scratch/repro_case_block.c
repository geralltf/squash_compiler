#define MK(a,b,c,d) (((unsigned)(a)<<24)+((unsigned)(b)<<16)+((unsigned)(c)<<8)+(unsigned)(d))
int main(void) {
    int x = 5;
    switch (x) {
        case 1:
            x = 2;
            break;
        case 2: {
            int y = 3;
            x = y;
            break;
        }
        default:
            x = 0;
            break;
    }
    return x;
}
