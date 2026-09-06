#include "Shared/Core/Src/pch.h"
#include "Shared/Core/Src/Log/Logger.h"

#include <ctime>
#include <format>
#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
    std::string CurrentTimestamp()
    {
        const auto now = std::chrono::system_clock::now();
        const auto nowTimeT = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::tm tm{};
        localtime_s(&tm, &nowTimeT);

        return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}",
                            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                            tm.tm_hour, tm.tm_min, tm.tm_sec, ms.count());
    }

    std::string_view LevelToTag(const Log::ELogLevel level)
    {
        switch (level)
        {
        case Log::ELogLevel::Debug:   return "DEBUG";
        case Log::ELogLevel::Info:    return "INFO ";
        case Log::ELogLevel::Warning: return "WARN ";
        case Log::ELogLevel::Error:   return "ERROR";
        }
        return "?????";
    }

#ifdef _WIN32
    // 콘솔에서 레벨을 눈에 띄게 구분하기 위한 색상: Error=빨강, Warning=노랑(레거시 콘솔 16색
    // API(SetConsoleTextAttribute)에는 주황이 없어 노랑으로 대체), Info=초록. Debug는 콘솔
    // 기본색을 그대로 쓴다.
    WORD LevelToConsoleAttributes(const Log::ELogLevel level, const WORD defaultAttributes)
    {
        switch (level)
        {
        case Log::ELogLevel::Info:    return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case Log::ELogLevel::Warning: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case Log::ELogLevel::Error:   return FOREGROUND_RED | FOREGROUND_INTENSITY;
        default:                            return defaultAttributes;
        }
    }
#endif
}

namespace Log
{
    Logger& Logger::Instance()
    {
        static Logger instance;
        return instance;
    }

    void Logger::Initialize(const std::filesystem::path& logFilePath, const ELogLevel minLevel)
    {
        std::lock_guard lock(mutex_);

        minLevel_ = minLevel;

#ifdef _WIN32
        // MSVC 콘솔은 기본적으로 시스템 ANSI 코드페이지(한글 로케일에서는 CP949)로 출력을
        // 해석한다. 소스 파일은 /utf-8로 컴파일되어 문자열 리터럴이 UTF-8 바이트로 남기 때문에,
        // 콘솔 코드페이지를 맞춰주지 않으면 한글 로그가 깨져 보인다.
        ::SetConsoleOutputCP(CP_UTF8);
        ::SetConsoleCP(CP_UTF8);

        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &info))
        {
            defaultConsoleAttributes_ = info.wAttributes;
        }
#endif

        if (!logFilePath.parent_path().empty())
        {
            std::filesystem::create_directories(logFilePath.parent_path());
        }
        file_.open(logFilePath, std::ios::app);
    }

    void Logger::Write(const ELogLevel level, const std::string_view message)
    {
        if (level < minLevel_)
        {
            return;
        }

        const auto line = std::format("[{}] [{}] {}", CurrentTimestamp(), LevelToTag(level), message);

        std::lock_guard lock(mutex_);

#ifdef _WIN32
        const auto handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
        ::SetConsoleTextAttribute(handle, LevelToConsoleAttributes(level, static_cast<WORD>(defaultConsoleAttributes_)));
        std::cout << line << '\n';
        ::SetConsoleTextAttribute(handle, static_cast<WORD>(defaultConsoleAttributes_));
#else
        std::cout << line << '\n';
#endif

        if (file_.is_open())
        {
            file_ << line << '\n';
            file_.flush();
        }
    }
}
