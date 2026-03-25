//#include "include/stdio.h"

/* extern declaration */
extern void ExitProcess(unsigned int code);

/* ============================================================ */
int main(int argc, char *argv[]) {
	char* argchr;
	
    printf("=== Simple Feature Test Suite ===\r\n");
	
	if(argc>2)
	{
		printf("has args:\r\n");
		printf("num args: %d\r\n", argc);
		for(int i=0;i<argc;i++)
		{
			argchr = argv[i]; 
			printf("..%s\r\n", argchr);
			printf("..%s\r\n", argv[i]);
		}
	}
	else
	{
		printf("no args\n");
	}
	
    printf("===========================\r\n");
    ExitProcess(0);
    return 0;
}
