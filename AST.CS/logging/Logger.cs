using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Console;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace SquashC.Compiler
{
    public class Logger
    {
        public static ILogger Log { get; set; }

        public static StatisticsLogger _log;

        private static void CreateBuilder(ILoggingBuilder builder)
        {
            builder.AddConsole();
            builder.SetMinimumLevel(LogLevel.Information); // Set to Information level
        }

        public static void InitLogging()
        {
            using ILoggerFactory factory = LoggerFactory.Create(builder => CreateBuilder(builder));
            _log = new StatisticsLogger(factory.CreateLogger("SquashC"));
            Log = _log;
        }
    }
}
