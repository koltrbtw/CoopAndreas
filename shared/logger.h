#pragma once

class logger
{
public:
    // set by the code that wants the lines logged on this thread (a stand command run for the dev tool), nullptr
    // otherwise
    static inline thread_local std::string* ms_pCapture = nullptr;

private:
    static void log(const char* text, const char* level, const char* color)
    {
        std::cout << color << "[" << level << "]" << "\033[0m" << ": " << text << std::endl;
        if (ms_pCapture)
        {
            ms_pCapture->append("[").append(level).append("]: ").append(text).append("\n");
        }
    }

public:
    static void info(const char* format, ...)
    {
        char buffer[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        log(buffer, "info", "\033[34m");
    }

    static void warn(const char* format, ...)
    {
        char buffer[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        log(buffer, "warn", "\033[33m");
    }

    static void error(const char* format, ...)
    {
        char buffer[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        log(buffer, "error", "\033[31m");
    }
};