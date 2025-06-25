#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

//struct StatisticsLogger;

typedef struct StatisticsLogger
{
    int criticals_count;
    int errors_count;
    int warnings_count;
} statistics_logger_t;

extern struct StatisticsLogger* __stats_singleton;

struct StatisticsLogger* stats_new();
void stats_init(struct StatisticsLogger** stats);
struct StatisticsLogger* stats_begin();
void LogInformation(const char* format, ...);
void LogWarning(const char* format, ...);
void LogError(const char* format, ...);
void LogCritical(const char* format, ...);
void PrintEndStatistics();

#endif