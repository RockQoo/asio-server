#pragma once

#include <cstdint>

namespace Zone
{
    // 고정 레이아웃 페이로드들.
    // Chat은 의도적으로 고정 구조체가 없다 -- 크기가 가변적이라서 BinaryWriter::WriteString /
    // BinaryReader::ReadString으로 길이 접두 문자열을 직접 쓰고 읽는다.
#pragma pack(push, 1)
    struct MovePacket
    {
        float x;
        float y;
    };

    struct EnterZoneNotifyPacket
    {
        uint32_t playerId;
        uint32_t zoneId;
    };

    // MailAdd 요청 하나당 정확히 하나씩 돌아온다 -- 클라이언트는 이 mailId로만 MailDel을
    // 보낼 수 있다(서버가 실제로 배정한 값을 몰라서는 자기가 만든 메일을 못 지운다).
    struct MailAddAckPacket
    {
        uint32_t mailId;
    };

    // MailDel 요청 하나당 정확히 하나씩 돌아온다. success=0은 그 mailId가 이미 없어졌거나
    // (만료 자동삭제와 겹침 등) 애초에 존재한 적이 없었다는 뜻이다.
    struct MailDelAckPacket
    {
        uint32_t mailId;
        uint8_t success;
    };
#pragma pack(pop)
}
