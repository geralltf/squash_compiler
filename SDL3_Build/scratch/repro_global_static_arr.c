#include <stdio.h>
static const unsigned char tbl[16] =
{
   8,8,8,8,8,8,8,8, 9,9,9,9,9,9,9,9
};
int main(void) {
    int i;
    for (i = 0; i < 16; i++) printf("%d ", tbl[i]);
    printf("\n");
    return 0;
}
