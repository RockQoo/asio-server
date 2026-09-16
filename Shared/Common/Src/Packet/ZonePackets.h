#pragma once

#include "Shared/Common/Src/PacketId.h"

#include "Shared/Core/Src/Packet/BinaryWriter.h"

#include "Shared/Common/Src/Ids.h"

namespace Common
{
    // 고정 레이아웃 페이로드들.
    // Chat은 의도적으로 고정 구조체가 없다 -- 크기가 가변적이라서 BinaryWriter::WriteString /
    // BinaryReader::ReadString으로 길이 접두 문자열을 직접 쓰고 읽는다.
#pragma pack(push, 1)
    struct Position
    {
        float x;
        float y;
    };

    struct Z2CEnterZoneNotify
    {
        static constexpr PacketId kPacketId = PacketId::Z2CEnterZoneNotify;
        // 로그인이 확정한 DB의 player_id(RUID). **재접속해도 같다.**
        PlayerId playerId;

        // 이 클라이언트의 세션 id. **playerId와 용도가 다르다** -- Z2CMoveNotify /
        // Z2CChatNotify 가 "누가" 보냈는지를 이 값으로 싣기 때문에, 클라이언트가 그 통지들
        // 중 자기 것을 가려내려면 이 값이 필요하다.
        //
        // 예전에는 playerId 가 clientSessionId 를 uint32 로 자른 값이라 하나로 둘 다
        // 됐는데, 그건 우연이었고 재접속하면 playerId 가 바뀌는 결함이기도 했다.
        uint64_t clientSessionId;

        ZoneId zoneId;
    };

    // Mail 요청의 결과는 고정 구조체가 아니라 Z2CTaskResult(UnitOfWork 태스크 스트림)로
    // 돌아간다 -- 응답 구조체를 콘텐츠마다 새로 만드는 대신, 클라이언트가 서버와 같은
    // 태스크 목록을 그대로 적용하는 방식이다(Shared/Common/Src/TaskKind.h 참고).

    // 이동 통지. **요청(C2ZMove)과 본문이 다르다** -- 받는 쪽은 "누가" 움직였는지 알아야 해서
    // 발신자가 앞에 붙는다. 고정 레이아웃이라 Serialize() 가 필요 없다.
    struct Z2CMoveNotify
    {
        static constexpr PacketId kPacketId = PacketId::Z2CMoveNotify;

        uint32_t senderSessionId;
        Position position;

        void Set(const uint32_t sender, const Position& value)
        {
            senderSessionId = sender;
            position = value;
        }
    };
#pragma pack(pop)

    // 채팅 통지. 문자열이 있어 고정 레이아웃이 아니므로 **자기 바이트를 직접 만든다**
    // (Common::ToBytes 가 Serialize() 를 보고 갈라준다).
    struct Z2CChatNotify
    {
        static constexpr PacketId kPacketId = PacketId::Z2CChatNotify;

        uint32_t senderSessionId{};
        std::string message;

        void Set(const uint32_t sender, std::string text)
        {
            senderSessionId = sender;
            message = std::move(text);
        }

        [[nodiscard]] std::vector<byte> Serialize() const
        {
            Packet::BinaryWriter binaryWriter;
            binaryWriter.Write(senderSessionId);
            binaryWriter.WriteString(message);
            return binaryWriter.MoveBuffer();
        }
    };
}
