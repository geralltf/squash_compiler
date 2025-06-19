#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdarg.h>

void LogInformation(const char* format, ...);
void LogWarning(const char* format, ...);
void LogError(const char* format, ...);
void LogCritical(const char* format, ...);

#endif