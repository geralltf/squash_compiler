#include "Logger.h"
#include "sb.h"

void LogInformation(const char* format, ...)
{
	va_list args;
	va_start(args, format); // Initialize va_list with the format string

	StringBuilder* sb = sb_create();
	sb_append(sb, "\033[97mINFO: \033[0m");
	sb_append(sb, format);
	sb_append(sb, "\n");
	char* format_str = sb_concat(sb);

	vprintf(format_str, args); // Use vprintf to print the formatted message
	// vprintf takes a va_list instead of individual arguments

	va_end(args); // Clean up the va_list
}

void LogWarning(const char* format, ...)
{
	va_list args;
	va_start(args, format); // Initialize va_list with the format string

	StringBuilder* sb = sb_create();
	sb_append(sb, "\033[32mWARN: \033[0m");
	sb_append(sb, format);
	sb_append(sb, "\n");
	char* format_str = sb_concat(sb);

	vprintf(format_str, args); // Use vprintf to print the formatted message
	// vprintf takes a va_list instead of individual arguments

	va_end(args); // Clean up the va_list
}

void LogError(const char* format, ...)
{
	va_list args;
	va_start(args, format); // Initialize va_list with the format string

	StringBuilder* sb = sb_create();
	sb_append(sb, "\033[91mERROR: \033[0m");
	sb_append(sb, format);
	sb_append(sb, "\n");
	char* format_str = sb_concat(sb);

	vprintf(format_str, args); // Use vprintf to print the formatted message
	// vprintf takes a va_list instead of individual arguments

	va_end(args); // Clean up the va_list
}

void LogCritical(const char* format, ...)
{
	va_list args;
	va_start(args, format); // Initialize va_list with the format string

	StringBuilder* sb = sb_create();
	sb_append(sb, "\033[91;47mCRITICAL: \033[0m");
	sb_append(sb, format);
	sb_append(sb, "\n");
	char* format_str = sb_concat(sb);

	vprintf(format_str, args); // Use vprintf to print the formatted message
	// vprintf takes a va_list instead of individual arguments

	va_end(args); // Clean up the va_list
}