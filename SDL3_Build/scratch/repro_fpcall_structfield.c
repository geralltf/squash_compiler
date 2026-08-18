typedef int (*put_func)(const void *pBuf, int len, void *pUser);

typedef struct {
    put_func m_pPut_buf_func;
    void *m_pPut_buf_user;
    int dummy;
} Compressor;

static int my_putter(const void *pBuf, int len, void *pUser) {
    int *counter = (int*)pUser;
    *counter += len;
    return 1;
}

int main(void) {
    Compressor c;
    c.m_pPut_buf_func = my_putter;
    int counter = 0;
    c.m_pPut_buf_user = &counter;

    Compressor *d = &c;
    char buf[16];
    int n = 5;
    if (!(*d->m_pPut_buf_func)(buf, n, d->m_pPut_buf_user)) {
        return 1;
    }
    return counter;
}
