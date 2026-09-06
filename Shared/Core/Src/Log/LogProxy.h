#pragma once

#include "Shared/Core/Src/Log/LogEntry.h"

namespace Log
{
    // LOG.Error(category, "메시지").KV("Key", value).V(value2); 형태의 진입점. 상태 없는
    // 얇은 래퍼라 전역 인스턴스 하나(LOG)를 어디서든 공유해도 안전하다 -- 실제 동기화는
    // Logger::Write 안의 mutex_가 담당한다. category는 어떤 프로젝트가 정의한 scoped enum이든
    // (LogCategoryType을 만족하기만 하면) 그대로 받아, 호출부마다 알맞은 LogEntry<TCategory>를
    // 반환한다.
    class LogProxy
    {
    public:
        template <LogCategoryType TCategory>
        LogEntry<TCategory> Debug(const TCategory category, const std::string_view message) const
        {
            return LogEntry<TCategory>(ELogLevel::Debug, category, message);
        }

        template <LogCategoryType TCategory>
        LogEntry<TCategory> Info(const TCategory category, const std::string_view message) const
        {
            return LogEntry<TCategory>(ELogLevel::Info, category, message);
        }

        template <LogCategoryType TCategory>
        LogEntry<TCategory> Warning(const TCategory category, const std::string_view message) const
        {
            return LogEntry<TCategory>(ELogLevel::Warning, category, message);
        }

        template <LogCategoryType TCategory>
        LogEntry<TCategory> Error(const TCategory category, const std::string_view message) const
        {
            return LogEntry<TCategory>(ELogLevel::Error, category, message);
        }
    };

    inline LogProxy LOG;
}

// 어디서든 `Log::` 없이 `LOG.Error(...)`로 바로 쓰기 위한 전역 노출.
using Log::LOG;
