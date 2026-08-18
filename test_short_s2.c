int printf(const char *fmt, ...);
struct TestS { short a; short b; int c; };
int main() {
    printf("sizeof short=%d sizeof TestS=%d\n", (int)sizeof(short), (int)sizeof(struct TestS));
    return 0;
}
