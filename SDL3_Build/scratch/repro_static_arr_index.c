#include <stdio.h>
static void dbgprint(int v) { printf("v=%d\n", v); }

void test(int num_chans) {
  static const unsigned char chans[] = {0x00, 0x00, 0x04, 0x02, 0x06};
  unsigned char result = chans[num_chans];
  dbgprint((int)result);
}

int main(void) {
    test(4);
    test(3);
    test(1);
    return 0;
}
