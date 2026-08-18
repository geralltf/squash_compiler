typedef int (*put_func)(const void *pBuf, int len, void *pUser);

static int my_putter(const void *pBuf, int len, void *pUser) {
    int *counter = (int*)pUser;
    *counter += len;
    return 1;
}

int main(void) {
    put_func fp = my_putter;
    int counter = 0;
    char buf[16];
    int n = 5;
    if (!(*fp)(buf, n, &counter)) {
        return 1;
    }
    return counter;
}
