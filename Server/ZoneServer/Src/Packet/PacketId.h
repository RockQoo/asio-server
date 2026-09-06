#pragma once

#include <cstdint>

namespace Zone
{
    // Packet::PacketHeader::id에 실리는 애플리케이션 수준 패킷 타입 id
    enum class PacketId : uint16_t
    {
        Echo = 1,             // 클라이언트 <-> 서버 상태 확인용, ZoneServer의 LB 스레드가 즉시 되돌려줌
        Chat = 2,             // 클라이언트 -> 서버: 길이 접두 문자열, 존 내부로 브로드캐스트
        Move = 3,             // 클라이언트 -> 서버: MovePacket, 담당 TaskWorker에서 처리
        EnterZoneNotify = 4,  // 서버 -> 클라이언트: 존 배정(신규 입장 또는 핸드오프 전입) 통지
        MailAdd = 5,          // 클라이언트 -> 서버: title/body/durationSec, Task::UnitOfWork 경유
        MailDel = 6,          // 클라이언트 -> 서버: mailId, Task::UnitOfWork 경유
        Notice = 7,           // 서버 -> 클라이언트: WorldServer가 Zone을 거치지 않고 직접 브로드캐스트
        MailAddAck = 8,       // 서버 -> 클라이언트: 실제 배정된 mailId(MailAddAckPacket)
        MailDelAck = 9,       // 서버 -> 클라이언트: 삭제 요청 결과(MailDelAckPacket)
    };
}
