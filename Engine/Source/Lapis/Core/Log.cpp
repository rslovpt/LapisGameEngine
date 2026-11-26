#include "Lapis/Core/Log.h"
#include "Lapis/Core/Base.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

#if LAPIS_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace Lapis {

namespace {
    LogLevel   s_Level = LogLevel::Trace;
    std::mutex s_Mutex;

    const char* LevelName(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Info:  return "INFO ";
            case LogLevel::Warn:  return "WARN ";
            case LogLevel::Error: return "ERROR";
            case LogLevel::Fatal: return "FATAL";
            default:              return "?????";
        }
    }

#if LAPIS_PLATFORM_WINDOWS
    WORD LevelColor(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return FOREGROUND_INTENSITY;
            case LogLevel::Info:  return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
            case LogLevel::Warn:  return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
            case LogLevel::Error: return FOREGROUND_RED | FOREGROUND_INTENSITY;
            case LogLevel::Fatal: return BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
            default:              return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        }
    }
#else
    const char* LevelColor(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return "\x1b[90m";
            case LogLevel::Info:  return "\x1b[32m";
            case LogLevel::Warn:  return "\x1b[33m";
            case LogLevel::Error: return "\x1b[31m";
            case LogLevel::Fatal: return "\x1b[41;97m";
            default:              return "\x1b[0m";
        }
    }
#endif
}

void Log::SetLevel(LogLevel level) { s_Level = level; }
LogLevel Log::GetLevel() { return s_Level; }

void Log::Write(LogLevel level, const std::string& message) {
    if (level < s_Level) return;

    const auto now  = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if LAPIS_PLATFORM_WINDOWS
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char timeBuf[16];
    std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tm);

    std::lock_guard<std::mutex> lock(s_Mutex);
    FILE* stream = level >= LogLevel::Error ? stderr : stdout;

#if LAPIS_PLATFORM_WINDOWS
    HANDLE console = GetStdHandle(level >= LogLevel::Error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info{};
    const bool hasConsole = GetConsoleScreenBufferInfo(console, &info) != 0;
    if (hasConsole) SetConsoleTextAttribute(console, LevelColor(level));
    std::fprintf(stream, "[%s] [%s] Lapis: %s\n", timeBuf, LevelName(level), message.c_str());
    if (hasConsole) SetConsoleTextAttribute(console, info.wAttributes);
    // Also mirror to the Visual Studio output window.
    OutputDebugStringA(("[Lapis] " + message + "\n").c_str());
#else
    std::fprintf(stream, "%s[%s] [%s] Lapis: %s\x1b[0m\n", LevelColor(level), timeBuf, LevelName(level), message.c_str());
#endif
    std::fflush(stream);
}

} // namespace Lapis
