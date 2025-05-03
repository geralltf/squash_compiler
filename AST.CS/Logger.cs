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

        public static void InitLogging()
        {
            using ILoggerFactory factory = LoggerFactory.Create(builder => builder.AddConsole().SetMinimumLevel(LogLevel.Information)); // Set to Information level
            Log = factory.CreateLogger("SquashC");
        }
    }
}
