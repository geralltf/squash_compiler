typedef struct {
    unsigned long long m_size, m_capacity;
    unsigned char *m_pBuf;
    int m_expandable;
} out_buffer;

typedef int (*put_func)(const void *pBuf, int len, void *pUser);

static int my_putter(const void *pBuf, int len, void *pUser) {
    out_buffer *p = (out_buffer *)pUser;
    unsigned long long new_size = p->m_size + len;
    if (new_size > p->m_capacity) {
        unsigned long long new_capacity;
        unsigned char *pNew_buf;
        new_capacity = p->m_capacity;
        if (!p->m_expandable) return 0;
        do { new_capacity = new_capacity < 128 ? 128 : new_capacity << 1; } while (new_size > new_capacity);
        pNew_buf = (unsigned char*)realloc(p->m_pBuf, new_capacity);
        if (!pNew_buf) return 0;
        p->m_pBuf = pNew_buf;
        p->m_capacity = new_capacity;
    }
    int i;
    for (i = 0; i < len; i++) p->m_pBuf[p->m_size + i] = ((const unsigned char*)pBuf)[i];
    p->m_size = new_size;
    return 1;
}

typedef struct {
    put_func m_pPut_buf_func;
    void *m_pPut_buf_user;
} compressor;

int main(void) {
    out_buffer ob;
    ob.m_size = 0; ob.m_capacity = 4; ob.m_pBuf = (unsigned char*)malloc(4); ob.m_expandable = 1;

    compressor c;
    c.m_pPut_buf_func = my_putter;
    c.m_pPut_buf_user = &ob;
    compressor *d = &c;

    unsigned char data[64];
    int i;
    for (i = 0; i < 64; i++) data[i] = (unsigned char)i;

    if (!(*d->m_pPut_buf_func)(data, 64, d->m_pPut_buf_user)) {
        return 1;
    }
    return (int)ob.m_size;
}
