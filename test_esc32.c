#include "include/stdio.h"

extern void ExitProcess(unsigned int code);

int main(void) {
    char c_nl;  c_nl  = '\n';  if ((int)c_nl  != 10)  { printf("FAIL nl=%d\r\n",  (int)c_nl);  ExitProcess(1); }
    char c_cr;  c_cr  = '\r';  if ((int)c_cr  != 13)  { printf("FAIL cr=%d\r\n",  (int)c_cr);  ExitProcess(1); }
    char c_tab; c_tab = '\t';  if ((int)c_tab != 9)   { printf("FAIL tab=%d\r\n", (int)c_tab); ExitProcess(1); }
    char c_nul; c_nul = '\0';  if ((int)c_nul != 0)   { printf("FAIL nul=%d\r\n", (int)c_nul); ExitProcess(1); }
    char c_bel; c_bel = '\a';  if ((int)c_bel != 7)   { printf("FAIL bel=%d\r\n", (int)c_bel); ExitProcess(1); }
    char c_bsp; c_bsp = '\b';  if ((int)c_bsp != 8)   { printf("FAIL bsp=%d\r\n", (int)c_bsp); ExitProcess(1); }
    char c_ff;  c_ff  = '\f';  if ((int)c_ff  != 12)  { printf("FAIL ff=%d\r\n",  (int)c_ff);  ExitProcess(1); }
    char c_vt;  c_vt  = '\v';  if ((int)c_vt  != 11)  { printf("FAIL vt=%d\r\n",  (int)c_vt);  ExitProcess(1); }
    char c_hx;  c_hx  = '\x41'; if ((int)c_hx != 65)  { printf("FAIL hx=%d\r\n",  (int)c_hx);  ExitProcess(1); }
    char c_hx2; c_hx2 = '\x7F'; if ((int)c_hx2 != 127) { printf("FAIL hx2=%d\r\n",(int)c_hx2); ExitProcess(1); }
    char c_hx3; c_hx3 = '\x00'; if ((int)c_hx3 != 0)  { printf("FAIL hx3=%d\r\n", (int)c_hx3); ExitProcess(1); }
    printf("ALL PASS\r\n");
    ExitProcess(0);
    return 0;
}
