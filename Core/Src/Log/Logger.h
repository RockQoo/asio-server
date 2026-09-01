#pragma once

#include "Core/Src/Log/LogLevel.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>

namespace Log
{
    // 프로세스 전역 싱글턴. 콘솔(레벨별 색상)과 파일에 동시에 기록한다. I/O 스레드/존 워커
    // 스레드 등 여러 스레드에서 동시에 호출되므로 mutex_로 출력을 직렬화한다 (Zone::ZoneWorld
    // 존별 상태(한 스레드 전용이라 락이 없다)와 달리, 로그는 스레드 어피니티가 없는 전역 자원이라 락이 필요하다).
    class Logger
    {
    public:
        [[nodiscard]] static Logger& Instance();

        // 콘솔 코드페이지를 UTF-8로 맞추고(한글 로그가 CP949로 오인식되어 깨지는 문제 방지),
        // 로그 파일을 연다. 프로세스 시작 시 한 번만 호출하면 된다.
        void Initialize(const std::filesystem::path& logFilePath, const ELogLevel minLevel = ELogLevel::Debug);

        void Write(const ELogLevel level, const std::string_view message);

    private:
        Logger() = default;
        ~Logger() = default;

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        std::mutex mutex_;
        std::ofstream file_;
        ELogLevel minLevel_{ELogLevel::Debug};
        uint16_t defaultConsoleAttributes_{0};
    };
}
