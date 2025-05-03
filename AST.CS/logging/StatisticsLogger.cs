using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace SquashC.Compiler
{
    public class StatisticsLogger : ILogger
    {
        private ILogger _log { get; set; }
        public int CriticalsCount { get; private set; } = 0;
        public int ErrorsCount { get; private set; } = 0;
        public int WarningsCount { get; private set; } = 0;

        public StatisticsLogger(ILogger logger)
        {
            this._log = logger;
        }

        public IDisposable BeginScope<TState>(TState state) where TState : notnull
        {
            return null;
        }

        public bool IsEnabled(LogLevel logLevel)
        {
            return true;
        }

        public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception exception, Func<TState, Exception, string> formatter)
        {
            // Count each type of log level.
            if (logLevel == LogLevel.Critical)
            {
                CriticalsCount++;
            }

            if (logLevel == LogLevel.Error)
            {
                ErrorsCount++;
            }

            if (logLevel == LogLevel.Warning)
            {
                WarningsCount++;
            }

            // Passthrough to underlying log.
            _log.Log<TState>(logLevel, eventId, state, exception, formatter);
        }

        public void PrintEndStatistics()
        {
            Logger.Log.LogInformation("************* CRITICALS COUNT: " + CriticalsCount.ToString() + ", ERRORS: COUNT " + ErrorsCount.ToString() + ", WARNINGS: COUNT " + WarningsCount.ToString() + " *************");
        }
    }
}
