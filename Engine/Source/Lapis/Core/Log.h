#pragma once
// Simple leveled logger with "{}" placeholder formatting:
//     LAPIS_INFO("Loaded {} meshes in {} ms", count, ms);

#include <functional>
#include <sstream>
#include <string>
#include <string_view>

namespace Lapis {

enum class LogLevel { Trace = 0, Info, Warn, Error, Fatal, Off };

namespace Detail {
    inline void FormatImpl(std::ostringstream& out, std::string_view fmt) { out << fmt; }

    template <typename T, typename... Rest>
    void FormatImpl(std::ostringstream& out, std::string_view fmt, const T& value, const Rest&... rest) {
        const size_t pos = fmt.find("{}");
        if (pos == std::string_view::npos) { out << fmt; return; }
        out << fmt.substr(0, pos) << value;
        FormatImpl(out, fmt.substr(pos + 2), rest...);
    }
}

// Formats "{}" placeholders in order using operator<<.
template <typename... Args>
std::string Format(std::string_view fmt, const Args&... args) {
    std::ostringstream out;
    Detail::FormatImpl(out, fmt, args...);
    return out.str();
}

class Log {
public:
    static void SetLevel(LogLevel level);
    static LogLevel GetLevel();
    static void Write(LogLevel level, const std::string& message);

    // Mirrors every message to a text file (flushed per line so the log
    // survives crashes). Creates parent directories. Returns false on failure.
    static bool OpenFile(const std::string& path);
    static void CloseFile();
    static const std::string& GetFilePath();

    // Extra sink (e.g. diagnostics capturing errors). Returns an id for removal.
    using Listener = std::function<void(LogLevel, const std::string&)>;
    static int  AddListener(Listener listener);
    static void RemoveListener(int id);

    template <typename... Args>
    static void Print(LogLevel level, std::string_view fmt, const Args&... args) {
        if (level < GetLevel()) return;
        Write(level, Format(fmt, args...));
    }
};

} // namespace Lapis

#define LAPIS_TRACE(...) ::Lapis::Log::Print(::Lapis::LogLevel::Trace, __VA_ARGS__)
#define LAPIS_INFO(...)  ::Lapis::Log::Print(::Lapis::LogLevel::Info,  __VA_ARGS__)
#define LAPIS_WARN(...)  ::Lapis::Log::Print(::Lapis::LogLevel::Warn,  __VA_ARGS__)
#define LAPIS_ERROR(...) ::Lapis::Log::Print(::Lapis::LogLevel::Error, __VA_ARGS__)
#define LAPIS_FATAL(...) ::Lapis::Log::Print(::Lapis::LogLevel::Fatal, __VA_ARGS__)
