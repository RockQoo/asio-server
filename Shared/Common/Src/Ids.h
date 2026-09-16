#pragma once

#include <cstdint>

#include "Shared/Common/Src/StrongId.h"

namespace Common
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
}
