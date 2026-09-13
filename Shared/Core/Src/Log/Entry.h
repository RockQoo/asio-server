#pragma once

#include "Shared/Core/Src/Log/LogLevel.h"
#include "Shared/Core/Src/Log/Logger.h"

namespace Log
{
    // TCategory는 각 프로젝트(Core 자신, ZoneServer 등)가 스스로 정의하는 scoped enum이다.
    // Log는 콘텐츠(존 등)를 몰라야 하므로 구체적인 카테고리 값 목록을 갖지 않고, ToString(TCategory)
    // 프리 함수를 ADL로 찾아 문자열로 바꾼다 -- 그 프리 함수를 카테고리 enum과 같은 네임스페이스에
    // 두면(예: Zone::ToString(Zone::ELogCategory)) 별도 등록 없이 자동으로 발견된다.
    // C++20에는 std::is_scoped_enum이 없어(C++23 추가) is_enum_v로 대체한다 -- 프로젝트 컨벤션상
    // enum은 항상 enum class이므로 실질적으로는 문제되지 않는다.
    template <typename TCategory>
    concept LogCategoryType = std::is_enum_v<TCategory> &&
        requires(const TCategory category) { { ToString(category) } -> std::convertible_to<std::string_view>; };

    // LOG.Error(category, "메시지").KV("Key", value); 처럼 체이닝하고, 문장이 끝나 이 임시
    // 객체가 파괴되는 시점(소멸자)에 한 줄을 커밋한다. 체이닝 없이 LOG.Info(...)만 써도
    // 유효하므로 Proxy 쪽에는 일부러 [[nodiscard]]를 붙이지 않았다.
    //
    // **KV/V는 반드시 `*this` 참조를 반환해야 한다** -- 값으로 반환하면 호출마다 임시객체가
    // 생겨 그때마다 소멸자가 돌면서 줄이 여러 번, 그것도 불완전한 상태로 찍힌다.
    //
    // message를 string_view로 받는 것은 생성자에서 즉시 line_에 구워 넣기 때문이다
    // (값으로 받으면 SSO를 넘는 한글 메시지가 매 호출 힙 할당된다).
    template <LogCategoryType TCategory>
    class Entry
    {
    public:
        Entry(const ELogLevel level, const TCategory category, const std::string_view message)
            : level_(level)
            , line_(std::format("[{}] {}", ToString(category), message))
        {
        }

        ~Entry()
        {
            Logger::Instance().Write(level_, line_);
        }

        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;
        Entry(Entry&&) = delete;
        Entry& operator=(Entry&&) = delete;

        template <typename T>
        Entry& KV(const std::string_view key, const T& value)
        {
            AppendField(std::format("{} : {}", key, value));
            return *this;
        }

        template <typename T>
        Entry& V(const T& value)
        {
            AppendField(std::format("{}", value));
            return *this;
        }

    private:
        void AppendField(const std::string_view field)
        {
            line_ += hasFields_ ? ", " : " . ";
            line_ += field;
            hasFields_ = true;
        }

        ELogLevel level_;
        std::string line_;
        bool hasFields_{false};
    };
}
