#pragma once

#include "Shared/Core/Src/Log/LogLevel.h"
#include "Shared/Core/Src/Log/Logger.h"

#include <format>
#include <string>
#include <string_view>
#include <type_traits>

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

    // LOG.Error(category, "메시지").KV("Key", value).V(value2); 처럼 체이닝해서 값을 붙이고,
    // 문장이 끝나 이 임시 객체가 파괴되는 시점(소멸자)에 실제로 한 줄을 로그로 커밋한다.
    //
    // 카테고리+메시지는 생성자에서 즉시 std::format으로 line_(소유 문자열)에 구워 넣는다.
    // 그래서 message를 std::string으로 값 복사/이동해 들고 있을 필요가 없다 -- 생성자 호출이
    // 끝나는 순간 원본 문자열은 더 이상 필요 없으므로, 비-소유 std::string_view로 받아도
    // 안전하고 그 편이 매 호출마다 힙 할당을 하나 줄인다(문자열 리터럴을 std::string으로
    // 값 매개변수로 받으면 SSO 버퍼를 넘는 한글 메시지는 매번 새로 힙 할당된다).
    //
    // KV/V가 `*this`에 대한 참조를 반환해야 체이닝 도중 생기는 임시 사본 없이, 문장 끝에서
    // 딱 한 번만 커밋된다 -- 값으로 반환하면 매 호출마다 별도 임시객체가 생겨 그때마다
    // 소멸자가 돌면서 줄이 여러 번(불완전한 상태로) 찍히게 된다.
    //
    // KV/V 체이닝 없이 `LOG.Info(category, "메시지");`만 쓰는 것도 유효한 사용법이다 --
    // 반환값을 변수로 받지 않고 버려도(discard) 소멸자가 정상적으로 한 줄을 커밋하기
    // 때문에, LogProxy::Debug/Info/Warning/Error에는 일부러 [[nodiscard]]를 붙이지 않는다.
    template <LogCategoryType TCategory>
    class LogEntry
    {
    public:
        LogEntry(const ELogLevel level, const TCategory category, const std::string_view message)
            : level_(level)
            , line_(std::format("[{}] {}", ToString(category), message))
        {
        }

        ~LogEntry()
        {
            Logger::Instance().Write(level_, line_);
        }

        LogEntry(const LogEntry&) = delete;
        LogEntry& operator=(const LogEntry&) = delete;
        LogEntry(LogEntry&&) = delete;
        LogEntry& operator=(LogEntry&&) = delete;

        template <typename T>
        LogEntry& KV(const std::string_view key, const T& value)
        {
            AppendField(std::format("{} : {}", key, value));
            return *this;
        }

        template <typename T>
        LogEntry& V(const T& value)
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
