int f(int x, int y) {
    switch (x) {
        case 1: {
            int z = y + 1;
            return z;
        }
        default:
            return y;
    }
}
int main(void) {
    return f(1, 5);
}
