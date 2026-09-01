#pragma once

#include <cstdint>

namespace World
{
    // World <-> Zone 사이 프로토콜(W2Z/Z2W).
    enum class ZoneLinkPacketId : uint16_t
    {
        ZoneRegister = 1,        // Z2W: Zone 접속 직후, 이 Zone이 담당하는 x구간을 알림
        EnterZoneRequest = 2,    // W2Z: 플레이어를 이 존에 입장(신규 배정 또는 핸드오프 전입)시킴
        LeaveZoneNotify = 3,     // W2Z: 클라이언트 접속 종료로 이 존에서 플레이어를 제거하라는 지시
        ForwardToZone = 4,       // W2Z: ClientEnvelopeHeader + 클라이언트가 보낸 원본 패킷 그대로
        ForwardToWorld = 5,      // Z2W: ClientEnvelopeHeader + 클라이언트에게 보낼 원본 패킷 그대로
        ZoneTransferRequest = 6, // Z2W: 존 경계를 넘어 다른 존으로 이동해야 함을 알림(Zone이 로컬
                                 // 상태를 먼저 지우고 요청하므로 World는 대상 존에 EnterZoneRequest만
                                 // 보내면 된다 -- 실패 시 플레이어가 유실되는 것은 학습용 단순화로
                                 // 감수한다, 존 2개 고정 인접 테이블이라 실제로 실패할 일이 없음)
        UnitOfWorkStream = 7,     // Z2W: Mail 등 변경 이벤트 묶음(Mail::UnitOfWork가 직렬화해서 보냄)
    };
}
