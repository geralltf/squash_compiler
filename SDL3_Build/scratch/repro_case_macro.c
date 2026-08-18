#define STBI__PNG_TYPE(a,b,c,d)  (((unsigned) (a) << 24) + ((unsigned) (b) << 16) + ((unsigned) (c) << 8) + (unsigned) (d))

int main(void) {
    unsigned x = STBI__PNG_TYPE('I','H','D','R');
    switch (x) {
        case STBI__PNG_TYPE('C','g','B','I'):
            x = 1;
            break;
        case STBI__PNG_TYPE('I','H','D','R'): {
            int comp = 3;
            x = comp;
            break;
        }
        default:
            x = 0;
            break;
    }
    return (int)x;
}
