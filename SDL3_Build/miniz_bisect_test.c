/* Manually replicates tdefl_write_image_to_png_file_in_memory_ex's logic
 * section by section (NOT modifying the real miniz.h — this is a separate
 * scratch file) with dbgmark() calls between each section, to isolate
 * exactly which part crashes when run against the real sdl_core.inc build. */
#include "sdl_core.inc"

int main(void)
{
    dbgmark("main() entered");
    if (!SDL_Init(0)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    dbgmark("after SDL_Init");

    int w = 4, h = 4, num_chans = 4, bpl = 16;
    Uint8 img[4*4*4];
    int ii;
    for (ii = 0; ii < 4*4*4; ii++) img[ii] = (Uint8)(ii * 3);
    const void *pImage = img;
    mz_uint level = 6;
    mz_bool flip = MZ_FALSE;
    mz_uint8 *plte = NULL; int plte_size = 0;
    mz_uint8 *trns = NULL; int trns_size = 0;
    size_t pLen_out_val = 0;
    size_t *pLen_out = &pLen_out_val;

    dbgmark("before MZ_MALLOC(sizeof(tdefl_compressor))");
    tdefl_compressor *pComp = (tdefl_compressor *)MZ_MALLOC(sizeof(tdefl_compressor));
    dbgmark("after MZ_MALLOC pComp");
    int i, y, z = 0;
    mz_uint32 c;
    *pLen_out = 0;
    size_t data_start, data_size;
    if (!pComp) { SDL_Log("pComp alloc failed"); return 1; }

    tdefl_output_buffer out_buf;
    MZ_CLEAR_OBJ(out_buf); out_buf.m_expandable = MZ_TRUE;
    out_buf.m_capacity = 57+MZ_MAX(64, (1+w*num_chans)*h);
    dbgmark("before MZ_MALLOC(out_buf.m_capacity)");
    out_buf.m_pBuf = (mz_uint8*)MZ_MALLOC(out_buf.m_capacity);
    dbgmark("after MZ_MALLOC out_buf.m_pBuf");
    if (!out_buf.m_pBuf) { SDL_Log("out_buf alloc failed"); return 1; }

    dbgmark("before header write block");
    {
        static const mz_uint8 chans[] = {0x00, 0x00, 0x04, 0x02, 0x06};
        mz_uint8 pnghdr[33]={ 0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,
                              0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
                              0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                              0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
        pnghdr[18] = (mz_uint8)(w>>8);
        pnghdr[19] = (mz_uint8)(w>>0);
        pnghdr[22] = (mz_uint8)(h>>8);
        pnghdr[23] = (mz_uint8)(h>>0);
        if (num_chans == 1 && plte_size > 0)
            pnghdr[25] = 3;
        else
            pnghdr[25] = (chans[num_chans]);
        dbgmark("before mz_crc32");
        c = (mz_uint32)mz_crc32(MZ_CRC32_INIT,pnghdr+12,17);
        dbgmark("after mz_crc32");
        for (i=0; i<4; ++i, c<<=8) ((mz_uint8*)(pnghdr+29))[i] = (mz_uint8)(c>>24);
        dbgmark("after crc byte loop");
        if (!tdefl_output_buffer_putter(pnghdr, sizeof(pnghdr), &out_buf)) { SDL_Log("putter failed"); return 1; }
        dbgmark("after tdefl_output_buffer_putter(pnghdr)");
    }
    dbgmark("after header write block");

    static const mz_uint s_tdefl_png_num_probes[11] = { 0, 1, 6, 32,  16, 32, 128, 256,  512, 768, 1500 };
    if (!tdefl_output_buffer_putter("\0\0\0\0\x49\x44\x41\x54", 8, &out_buf)) { SDL_Log("IDAT hdr putter failed"); return 1; }
    dbgmark("after IDAT chunk header putter");
    data_start = out_buf.m_size;

    dbgmark("before tdefl_init");
    tdefl_init(pComp, tdefl_output_buffer_putter, &out_buf, s_tdefl_png_num_probes[MZ_MIN(10, level)] | TDEFL_WRITE_ZLIB_HEADER);
    dbgmark("after tdefl_init");

    for (y = 0; y < h; ++y) {
        tdefl_compress_buffer(pComp, &z, 1, TDEFL_NO_FLUSH);
        dbgmark("after tdefl_compress_buffer(&z)");
        tdefl_compress_buffer(pComp, (mz_uint8*)pImage + (flip ? (h - 1 - y) : y) * bpl, w * num_chans, TDEFL_NO_FLUSH);
        dbgmark("after tdefl_compress_buffer(row)");
    }
    dbgmark("after compress loop");

    if (tdefl_compress_buffer(pComp, NULL, 0, TDEFL_FINISH) != TDEFL_STATUS_DONE) { SDL_Log("finish failed"); return 1; }
    dbgmark("after TDEFL_FINISH");

    data_size = out_buf.m_size-data_start;
    (out_buf.m_pBuf+data_start-8)[0] = (mz_uint8)(data_size>>24);
    (out_buf.m_pBuf+data_start-8)[1] = (mz_uint8)(data_size>>16);
    (out_buf.m_pBuf+data_start-8)[2] = (mz_uint8)(data_size>> 8);
    (out_buf.m_pBuf+data_start-8)[3] = (mz_uint8)(data_size>> 0);
    dbgmark("after IDAT size write");

    if (!tdefl_output_buffer_putter("\0\0\0\0\0\0\0\0\x49\x45\x4e\x44\xae\x42\x60\x82", 16, &out_buf)) { SDL_Log("footer putter failed"); return 1; }
    dbgmark("after footer putter");
    c = (mz_uint32)mz_crc32(MZ_CRC32_INIT,out_buf.m_pBuf+data_start-4, data_size+4);
    dbgmark("after footer crc32");
    for (i=0; i<4; ++i, c<<=8) (out_buf.m_pBuf+out_buf.m_size-16)[i] = (mz_uint8)(c >> 24);
    dbgmark("after footer crc byte loop");

    *pLen_out = out_buf.m_size;
    MZ_FREE(pComp);
    dbgmark("all done");

    SDL_Log("PNG written in memory: %d bytes", (int)*pLen_out);
    SDL_Quit();
    return 0;
}
