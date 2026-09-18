#pragma once

#include "Server/Core/Src/Pipeline/Types.h"
#include "Server/Common/Src/Ids.h"

// 존 하나를 레인에서 가리키는 좌표 묶음. **담당 존마다 처리기가 네 벌 있다.**
struct ZoneLaneTarget final
{
    Pipeline::ProcessorId lb{};         // World 링크 수신구. 봉투만 까고 넘긴다
    Pipeline::ProcessorId basic{};      // 판단·예약 + 입·퇴장
    Pipeline::ProcessorId tick{};       // 적분·경계 판정·만기
    Pipeline::ProcessorId broadcast{};  // 팬아웃 전송

    // ── TICK 레인의 주인 둘 ───────────────────────────────────────────────────
    // **틱 박자와 그 외를 다른 레인으로 가른다.** 같은 주인으로 묶으면 "버프 정리"나
    // "인스턴스 점검" 같은 잡일이 밀릴 때 그만큼 틱이 늦게 돈다 -- 틱은 프레임이라
    // 한 번 밀리면 그 존 전체가 느려진 것으로 보인다.
    Pipeline::OwnerId tickOwner{};
    Pipeline::OwnerId otherOwner{};

    // ── BROADCAST 레인의 주인 ─────────────────────────────────────────────────
    // **zoneId 를 그대로 쓰고 해시를 켠다.** 존 번호는 값이 드문드문해서(연속이 아니다)
    // 해시를 끄면 나머지 연산이 한쪽 레인으로 몰린다 -- TICK 과 정반대다.
    Pipeline::OwnerId broadcastOwner{};
};

// TICK 레인 수 = 존 수 x 2 + 1.
//
// **레인 수를 주인 값의 최대치보다 크게 잡아서 나머지 연산을 항등으로 만든다.**
// 주인이 `1 .. 2N` 이고 레인이 `2N+1` 개면 `owner % (2N+1) == owner` 라 충돌이 0이다
// (그래서 TICK 은 해시를 끈다 -- 켜면 이 1:1이 깨진다).
// `+1` 은 0번 자리이고, **존에 매이지 않은 전역 메시지**가 거기로 간다.
[[nodiscard]] constexpr size_t TickLaneCount(const size_t zoneCount) noexcept
{
    return zoneCount * 2 + 1;
}

// 존에 안 매인 TICK 메시지의 주인(예: World 등록 성공 통지).
inline constexpr Pipeline::OwnerId kGlobalTickOwner{0};

// **서수는 어디에도 저장하지 않고 담당 목록 순서에서 파생시킨다** -- 값을 두 군데 적으면
// 갈린다. 담당 존이 `1, 3` 처럼 띄엄띄엄해도 서수는 `0, 1` 이라 그대로 성립한다.
[[nodiscard]] constexpr Pipeline::OwnerId ZoneTickOwner(const size_t ordinal) noexcept
{
    return Pipeline::OwnerId{static_cast<int64_t>(ordinal * 2 + 1)};
}

[[nodiscard]] constexpr Pipeline::OwnerId ZoneOtherOwner(const size_t ordinal) noexcept
{
    return Pipeline::OwnerId{static_cast<int64_t>(ordinal * 2 + 2)};
}

// AddProcessor()가 돌려준 ProcessorId를 보관하는 자리.
// **"누구한테 보낼지"를 알아야 PushMsg를 할 수 있다.**
//
// 프로세서 id가 컴파일 타임 상수가 아니라 **등록 순서로 정해지는 런타임 인덱스**라
// (MessageProducer::AddProcessor가 돌려준다) 어딘가 보관해야 하고, 보내는 쪽은 이 구조체만
// 본다. 보내는 쪽이 받는 쪽 객체를 몰라도 되는 것이 이 구조의 요점이다.
//
// **스레드 규약**: 기동 때 ZoneApp이 한 번 채우고, 이후 레인 스레드들이 읽기만 한다.
// 채우기 전에 PushMsg를 하면 id가 무효(-1)라 MessageProducer가 경고를 남기고 버린다.
struct ZoneProcessorIds final
{
    // TIMER -- 만기만 판정하고 실제 일은 해당 레인으로 되돌린다. 프로세스에 하나다.
    Pipeline::ProcessorId timer{};

    // zoneId -> 그 존의 레인 좌표.
    std::unordered_map<Common::ZoneId, ZoneLaneTarget> zones;

    [[nodiscard]] bool HasZone(const Common::ZoneId zoneId) const { return zones.contains(zoneId); }

    // 없는 존이면 무효 id가 담긴 값을 돌려준다 -- 그대로 PushMsg하면 MessageProducer가
    // 경고를 남기고 버리므로, 호출부가 검사를 빠뜨려도 조용히 사라지지는 않는다.
    [[nodiscard]] ZoneLaneTarget ZoneTarget(const Common::ZoneId zoneId) const
    {
        const auto found = zones.find(zoneId);
        return found == zones.end() ? ZoneLaneTarget{} : found->second;
    }
};

// 이 프로세스의 프로세서 id 묶음. 전역이지만 값만 들고 있고 아무것도 소유하지 않는다.
[[nodiscard]] ZoneProcessorIds& Ids();
