#pragma once

#include <cstdint>
#include <format>
#include <functional>

namespace Protocol
{
    // id 하나를 **자기만의 타입**으로 만드는 래퍼. `int64` 별칭을 여러 종류의 id가 나눠 쓰면
    // 컴파일러가 섞이는 것을 못 막는다 -- 실제로 `enterState.playerId =
    // static_cast<uint32_t>(clientSessionId)` 같은 코드가 조용히 통과하고 있었다.
    //
    //   using MailId = StrongId<struct MailIdTag, int64_t>;
    //   using ZoneId = StrongId<struct ZoneIdTag, uint32_t>;
    //
    // 태그는 정의하지 않는다(선언만). 실체가 필요 없고, 타입을 가르는 역할만 한다.
    //
    // **막는 것**: 다른 종류의 id 대입/비교, raw 정수와의 대입/비교, 암묵 변환.
    // **여는 것**: 같은 종류끼리 ==/!=/<=>, 해시(맵 키), 생성자, Value()(와이어·DB 경계용).
    //
    // `Shared/Core`가 아니라 여기 있는 이유: 이 id들은 Zone/World/클라이언트가 함께 읽고 쓰는
    // 계약이고, Core는 콘텐츠를 몰라야 하는 라이브러리다(PacketId/TaskKind와 같은 근거).
    template <typename TTag, typename TValue, TValue kInvalidValue = TValue{}>
    class StrongId final
    {
    public:
        using ValueType = TValue;

        // "아직 발급되지 않았다/유효하지 않다"를 나타내는 값. 기본 생성이 이 값이라, 멤버로
        // 두면 초기화를 빠뜨려도 유효한 id처럼 보이지 않는다.
        static constexpr TValue kInvalid = kInvalidValue;

        constexpr StrongId() noexcept = default;

        // **explicit이 이 클래스의 전부다.** 암묵 변환을 허용하면 raw 정수가 그대로 흘러들어와
        // 타입을 나눈 의미가 사라진다. 값을 넣을 수 있는 자리는 여기 하나뿐이고, 만든 뒤에는
        // 바꿀 수 없다(대입은 같은 타입끼리의 복사만 된다).
        explicit constexpr StrongId(const TValue value) noexcept
            : value_(value)
        {
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value_ != kInvalidValue; }

        // 와이어에 쓰거나 DB 인자로 넘길 때만 쓴다. **일반 로직에서 부르기 시작하면 이 클래스가
        // 있으나 마나가 된다** -- 직렬화는 BinaryWriter/BinaryReader의 StrongId 오버로드를 쓸 것.
        [[nodiscard]] constexpr TValue Value() const noexcept { return value_; }

        // 기본 구현으로 ==/!= 가 같이 생긴다. 다른 TTag끼리는 애초에 다른 타입이라 후보가
        // 되지 않고, TValue와의 비교도 변환이 없어 컴파일 에러다.
        [[nodiscard]] constexpr bool operator==(const StrongId&) const noexcept = default;

        // 정렬된 컨테이너와 std::sort용. 값 순서가 곧 발급 순서다(RUID는 시간순으로 커진다).
        [[nodiscard]] constexpr auto operator<=>(const StrongId&) const noexcept = default;

    private:
        TValue value_{kInvalidValue};
    };
}

// std::unordered_map/set의 키로 쓰기 위한 해시. 감싼 값의 해시를 그대로 쓴다 -- 태그는
// 타입을 가를 뿐 값에 들어 있지 않으므로 섞일 일이 없다(애초에 다른 타입이라 같은 맵에
// 들어갈 수 없다).
template <typename TTag, typename TValue, TValue kInvalidValue>
struct std::hash<Protocol::StrongId<TTag, TValue, kInvalidValue>>
{
    [[nodiscard]] std::size_t operator()(
        const Protocol::StrongId<TTag, TValue, kInvalidValue>& strongId) const noexcept
    {
        return std::hash<TValue>{}(strongId.Value());
    }
};

// 로그(`LOG.Info(...).KV("MailId", mailId)`)가 std::format을 쓰므로 이게 없으면 호출부마다
// `.Value()`를 붙여야 한다 -- 그러면 "Value()는 경계에서만"이라는 규율이 로그 때문에 무너진다.
// 밑바탕 타입의 formatter를 상속해서 포맷 스펙(`{:>20}` 등)도 그대로 따라온다.
template <typename TTag, typename TValue, TValue kInvalidValue>
struct std::formatter<Protocol::StrongId<TTag, TValue, kInvalidValue>> : std::formatter<TValue>
{
    template <typename TContext>
    auto format(const Protocol::StrongId<TTag, TValue, kInvalidValue>& strongId, TContext& context) const
    {
        return std::formatter<TValue>::format(strongId.Value(), context);
    }
};
