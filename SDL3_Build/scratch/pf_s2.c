static int stbi__parse_png_file(stbi__png *z, int scan, int req_comp, unsigned int *palette_buffer, int palette_buffer_len)
{
   stbi__context *s = z->s;
   (void)palette_buffer;(void)palette_buffer_len;(void)req_comp;(void)scan;(void)z;

   for (;;) {
      stbi__pngchunk c = stbi__get_chunk_header(s);
      switch (c.type) {
         case STBI__PNG_TYPE('C','g','B','I'):
            break;
         case STBI__PNG_TYPE('I','H','D','R'): {
            int comp = 0;
            (void)comp;
            break;
         }
         default:
            break;
      }
   }
}
