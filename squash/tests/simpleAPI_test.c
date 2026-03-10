#include <windows.h>
int main()
{ 
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    WriteFile(h, "Hello from PE!\r\n", 17, &written, NULL);
    ExitProcess(0);
}