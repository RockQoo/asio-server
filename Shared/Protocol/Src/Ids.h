#pragma once

#include <cstdint>

#include "Shared/Protocol/Src/StrongId.h"

namespace Protocol
{
    // 이 저장소가 쓰는 id 종류 전부. **세 프로세스와 클라이언트가 공유하는 계약**이라
    // Shared/Protocol에 둔다(PacketId/TaskKind와 같은 근거).
    //
    // 밑바탕 타입이 종류마다 다른 것에 주의:
    //
    //   int64  : RUID로 발급하는 것(전역 유일 + 시간순). DB에서 BIGINT다
    //   uint32 : RUID가 아닌 것. 존 번호처럼 작은 범위를 사람이 정해 쓴다
    //
    // **유효하지 않은 값은 전부 0이다.** 기본 생성이 0이라 초기화를 빠뜨려도 유효한 id로
    // 보이지 않고, zoneId가 예전부터 "0 = 존 없음/미배정"이던 규약과도 맞는다.
    //
    // RUID가 0이 될 수 없는 이유: 조립식이 `(경과ms << 22) | (노드 << 12) | 시퀀스`인데
    // 노드 0은 예약값이라 `Ruid::Init`이 거부한다. 그래서 발급된 값은 항상 4096 이상이다.

    // 플레이어 계정. DB players.player_id
    using PlayerId = StrongId<struct PlayerIdTag, int64_t>;

    // 우편 한 통. DB mails.mail_id (단독 PK)
    using MailId = StrongId<struct MailIdTag, int64_t>;

    // 존 번호. 1부터 시작하고 0은 "존 없음/미배정" 예약값이다.
    // **RUID가 아니다** -- 2x2 격자의 고정 번호라 사람이 정한다(ParseZoneList).
    using ZoneId = StrongId<struct ZoneIdTag, uint32_t>;

    // 전투 유닛 하나. **존 메모리 안에서만 유효하고 DB에 남지 않는다** -- 그래서 RUID가 아니다.
    //
    // uint32인 것이 의도다: 플레이어 유닛의 id는 **clientSessionId를 uint32로 자른 값**이고,
    // 그건 Z2CMoveNotify/Z2CChatNotify가 발신자를 싣는 방식과 정확히 같다(ZonePackets.h).
    // 그래서 클라이언트는 키 하나로 "움직이는 원"과 "HP 바"를 이을 수 있다 -- 폭을 넓히면
    // 그 대응이 깨져서 클라이언트에 변환 표가 하나 생긴다.
    // 몬스터는 최상위 비트를 세운 대역(kMonsterUnitIdBase)이라 세션 id와 겹치지 않는다.
    using UnitId = StrongId<struct UnitIdTag, uint32_t>;

    // 몬스터 유닛 id의 시작. 세션 id는 접속 순서대로 1씩 증가하므로 이 값에 닿으려면 한
    // 프로세스가 21억 번 accept해야 한다.
    inline constexpr uint32_t kMonsterUnitIdBase = 0x8000'0000u;
}
