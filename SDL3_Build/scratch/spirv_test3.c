#include <stdint.h>
#include <stdio.h>
static const uint32_t arr_exact[10] = {
    0x07230203u, 0x00010000u, 0x0008000bu, 0x00000045u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u
};
static const uint32_t arr_partial[453] = {
    0x07230203u, 0x00010000u, 0x0008000bu, 0x00000045u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u
};
int main(void) {
    printf("arr_exact[0]=0x%08x\n", arr_exact[0]);
    printf("arr_partial[0]=0x%08x\n", arr_partial[0]);
    printf("arr_partial[9]=0x%08x (last explicit)\n", arr_partial[9]);
    printf("arr_partial[10]=0x%08x (should be 0, implicit)\n", arr_partial[10]);
    return 0;
}
