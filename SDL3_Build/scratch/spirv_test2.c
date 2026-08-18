#include <stdint.h>
#include <stdio.h>
static const uint32_t g_triangle_vert_spv[453] = {
    0x07230203u, 0x00010000u, 0x0008000bu, 0x00000045u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u
};
int main(void) {
    printf("vert[0]=0x%08x\n", g_triangle_vert_spv[0]);
    return 0;
}
