#pragma once

#include "Shared/Core/Src/Log/Entry.h"

namespace Log
{
    // LOG.Error(category, "메시지").KV("Key", value); 형태의 진입점. 상태 없는
    // 얇은 래퍼라 전역 인스턴스 하나(LOG)를 어디서든 공유해도 안전하다 -- 실제 동기화는
    // Logger::Write 안의 mutex_가 담당한다. category는 어떤 프로젝트가 정의한 scoped enum이든
    // (LogCategoryType을 만족하기만 하면) 그대로 받아, 호출부마다 알맞은 Entry<TCategory>를
    // 반환한다.
    class Proxy
    {
    public:
        template <LogCategoryType TCategory>
        Entry<TCategory> Debug(const TCategory category, const std::string_view message) const
        {
            return Entry<TCategory>(ELogLevel::Debug, category, message);
        }

        template <LogCategoryType TCategory>
        Entry<TCategory> Info(const TCategory category, const std::string_view message) const
        {
            return Entry<TCategory>(ELogLevel::Info, category, message);
        }

        template <LogCategoryType TCategory>
        Entry<TCategory> Warning(const TCategory category, const std::string_view message) const
        {
            return Entry<TCategory>(ELogLevel::Warning, category, message);
        }

        template <LogCategoryType TCategory>
        Entry<TCategory> Error(const TCategory category, const std::string_view message) const
        {
            return Entry<TCategory>(ELogLevel::Error, category, message);
        }
    };

    inline Proxy LOG;
}

// 어디서든 `Log::` 없이 `LOG.Error(...)`로 바로 쓰기 위한 전역 노출.
using Log::LOG;
