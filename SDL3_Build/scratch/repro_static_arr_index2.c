#include <stdio.h>

void test1(int i) {
  static const unsigned char a[] = {10,20,30,40,50};
  printf("static const: %d\n", (int)a[i]);
}
void test2(int i) {
  const unsigned char a[] = {10,20,30,40,50};
  printf("const: %d\n", (int)a[i]);
}
void test3(int i) {
  static unsigned char a[] = {10,20,30,40,50};
  printf("static: %d\n", (int)a[i]);
}
void test4(int i) {
  unsigned char a[] = {10,20,30,40,50};
  printf("plain: %d\n", (int)a[i]);
}

int main(void) {
    test1(4);
    test2(4);
    test3(4);
    test4(4);
    return 0;
}
