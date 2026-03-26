#include <stdio.h>

/* extern declaration */
extern void ExitProcess(unsigned int code);

/* ============================================================ */
int main(int argc, char *argv[]) {
	printf("Hello World!\r\n");
	
	FILE* fptr;

	// Open a file in read mode
	fptr = fopen("filename.txt", "r");

	// Store the content of the file
	char myString[100];

	// Read the content and store it inside myString
	fgets(myString, 100, fptr);

	// Print the file content
	printf("%s", myString);

	// Close the file
	fclose(fptr);
	
    ExitProcess(0);
    return 0;
}
