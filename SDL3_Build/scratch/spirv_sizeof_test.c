#include "triangle_spirv.h"
#include <stdio.h>
int main(void) {
    printf("sizeof(g_triangle_vert_spv)=%zu (expect 1812)\n", sizeof(g_triangle_vert_spv));
    printf("sizeof(g_triangle_frag_spv)=%zu (expect 500)\n", sizeof(g_triangle_frag_spv));
    printf("g_triangle_vert_spv[0]=0x%08x (expect 0x07230203)\n", g_triangle_vert_spv[0]);
    printf("g_triangle_frag_spv[0]=0x%08x (expect 0x07230203)\n", g_triangle_frag_spv[0]);
    return 0;
}
