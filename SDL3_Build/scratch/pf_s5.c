static int stbi__parse_png_file(stbi__png *z, int scan, int req_comp, unsigned int *palette_buffer, int palette_buffer_len)
{
   stbi_uc _palette[1024]={0}, pal_img_n=0;
   stbi_uc *palette = _palette;
   stbi_uc has_trans=0, tc[3]={0};
   stbi__uint16 tc16[3]={0};
   stbi__uint32 ioff=0, idata_limit=0, i, pal_len=0;
   int first=1,k,interlace=0, color=0, is_iphone=0;
   stbi__context *s = z->s;
   (void)_palette;(void)palette;(void)has_trans;(void)tc;(void)tc16;(void)ioff;(void)idata_limit;(void)i;(void)pal_len;(void)first;(void)k;(void)interlace;(void)color;(void)is_iphone;(void)pal_img_n;(void)palette_buffer;(void)palette_buffer_len;(void)req_comp;(void)scan;

   for (;;) {
      stbi__pngchunk c = stbi__get_chunk_header(s);
      switch (c.type) {
         case STBI__PNG_TYPE('C','g','B','I'):
            break;
         default:
            break;
      }
   }
}
