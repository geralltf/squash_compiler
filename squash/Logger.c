#include "Logger.h"
#include "sb.h"

extern struct StatisticsLogger* __stats_singleton;

struct StatisticsLogger* stats_new()
{
	struct StatisticsLogger* s = malloc(sizeof(struct StatisticsLogger));
	if (s != NULL)
	{
		s->criticals_count = 0;
		s->errors_count = 0;
		s->warnings_count = 0;
	}
	return s;
}

void stats_init(struct StatisticsLogger** stats)
{
	struct StatisticsLogger* s = *stats;

	s->criticals_count = 0;
	s->errors_count = 0;
	s->warnings_count = 0;

	*stats = s;
}

struct StatisticsLogger* stats_begin()
{
	struct StatisticsLogger* s = stats_new();

	stats_init(&s);

	__stats_singleton = s;

	return s;
}

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

	__stats_singleton->warnings_count++;

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

	__stats_singleton->errors_count++;

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

	__stats_singleton->criticals_count++;

	StringBuilder* sb = sb_create();
	sb_append(sb, "\033[91;47mCRITICAL: \033[0m");
	sb_append(sb, format);
	sb_append(sb, "\n");
	char* format_str = sb_concat(sb);

	vprintf(format_str, args); // Use vprintf to print the formatted message
	// vprintf takes a va_list instead of individual arguments

	va_end(args); // Clean up the va_list
}

void PrintEndStatistics()
{
	struct StatisticsLogger* stats = __stats_singleton;
	LogInformation("************* CRITICALS COUNT: %d, ERRORS: COUNT %d, WARNINGS: COUNT: %d *************", stats->criticals_count, stats->errors_count, stats->warnings_count);
}