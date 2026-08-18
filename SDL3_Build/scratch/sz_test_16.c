#include <stdint.h>
#include <stdio.h>
static const uint32_t arr[16] = {
    0x00000001u, 0x00000002u, 0x00000003u, 0x00000004u, 0x00000005u, 0x00000006u, 0x00000007u, 0x00000008u,
    0x00000009u, 0x0000000au, 0x0000000bu, 0x0000000cu, 0x0000000du, 0x0000000eu, 0x0000000fu, 0x00000010u,
};
int main(void) {
    printf("n=16 arr[0]=0x%08x arr[15]=0x%08x
", arr[0], arr[15]);
    return 0;
}