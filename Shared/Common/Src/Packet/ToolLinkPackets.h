#pragma once

#include "Shared/Common/Src/PacketId.h"

#include "Shared/Common/Src/Ids.h"
#include "Shared/Core/Src/Base/RUID.h"
#include "Shared/Core/Src/Packet/BinaryReader.h"

namespace Common
{
    // 운영툴 링크의 고정 레이아웃 페이로드들. 가변 길이(문자열이 섞인) 패킷은 구조체를 두지
    // 않고 BinaryWriter/BinaryReader로 직접 쓰고 읽으며, 그 와이어 포맷은 이 파일 아래쪽
    // 주석에 모아 적어둔다(ZoneLinkPackets.h의 UnitOfWorkStream과 같은 방식).
#pragma pack(push, 1)
    struct W2THelloResult
    {
        static constexpr PacketId kPacketId = PacketId::W2THelloResult;
        uint32_t requestId;
        uint8_t accepted;          // 0 = 거부(시크릿 불일치 또는 프로토콜 버전 불일치)
        uint32_t protocolVersion;  // World가 지원하는 버전 -- 툴이 자기 버전과 비교해 로그를 남긴다
    };

    struct W2TCommandResult
    {
        static constexpr PacketId kPacketId = PacketId::W2TCommandResult;
        uint32_t requestId;
        uint16_t resultCode;     // EToolResultCode
        uint32_t affectedCount;  // 실제로 처리된 대상 수(전체 우편/공지에서 몇 명에게 나갔는지)
    };

    struct T2WMailDelete
    {
        static constexpr PacketId kPacketId = PacketId::T2WMailDelete;
        uint32_t requestId;
        uint64_t clientSessionId;
        MailId mailId;
    };

    struct T2WClientList
    {
        static constexpr PacketId kPacketId = PacketId::T2WClientList;
        uint32_t requestId;
    };

    // ClientListReply 본문에 반복되는 항목
    struct W2TClientListEntry
    {
        uint64_t clientSessionId;
        uint32_t zoneId;
    };
#pragma pack(pop)

    // ---- 가변 길이 T2W 요청 ----
    //
    // **와이어 바이트는 예전과 한 바이트도 같다.** 예전에는 핸들러가 BinaryReader 로 직접
    // 읽었고, 그 읽는 순서를 그대로 Parse 로 옮겼을 뿐이다. GmTool(C#)이 이 순서를
    // 미러링하므로 필드 순서를 바꾸면 조용히 어긋난다(docs/design/wire-format.md 의 표가 계약).

    struct T2WHello
    {
        static constexpr PacketId kPacketId = PacketId::T2WHello;
        uint32_t requestId{};
        uint32_t protocolVersion{};
        std::string sharedSecret;
        std::string operatorName;

        [[nodiscard]] bool Parse(const std::span<const byte> payload)
        {
            Packet::BinaryReader binaryReader(payload);
            return binaryReader.Read(requestId) && binaryReader.Read(protocolVersion)
                && binaryReader.ReadString(sharedSecret) && binaryReader.ReadString(operatorName);
        }
    };

    struct T2WNotice
    {
        static constexpr PacketId kPacketId = PacketId::T2WNotice;
        uint32_t requestId{};
        std::string message;

        [[nodiscard]] bool Parse(const std::span<const byte> payload)
        {
            Packet::BinaryReader binaryReader(payload);
            return binaryReader.Read(requestId) && binaryReader.ReadString(message);
        }
    };

    struct T2WMailSend
    {
        static constexpr PacketId kPacketId = PacketId::T2WMailSend;
        uint32_t requestId{};
        uint8_t targetKind{};
        uint64_t clientSessionId{};
        std::string title;
        std::string body;
        int64_t durationSec{};

        [[nodiscard]] bool Parse(const std::span<const byte> payload)
        {
            Packet::BinaryReader binaryReader(payload);
            return binaryReader.Read(requestId) && binaryReader.Read(targetKind)
                && binaryReader.Read(clientSessionId)
                && binaryReader.ReadString(title) && binaryReader.ReadString(body)
                && binaryReader.Read(durationSec);
        }
    };

    struct T2WCouponChunkPush
    {
        static constexpr PacketId kPacketId = PacketId::T2WCouponChunkPush;
        uint32_t requestId{};
        std::string campaignCode;
        uint32_t chunkSeq{};
        std::vector<std::string> couponCodes;

        [[nodiscard]] bool Parse(const std::span<const byte> payload)
        {
            Packet::BinaryReader binaryReader(payload);
            uint32_t couponCount{};
            if (!binaryReader.Read(requestId) || !binaryReader.ReadString(campaignCode)
                || !binaryReader.Read(chunkSeq) || !binaryReader.Read(couponCount))
            {
                return false;
            }

            couponCodes.reserve(couponCount);
            for (uint32_t i = 0; i < couponCount; ++i)
            {
                std::string code;
                if (!binaryReader.ReadString(code))
                {
                    return false;
                }
                couponCodes.push_back(std::move(code));
            }

            return true;
        }
    };

    // 운영툴 링크의 프로토콜 버전. 와이어 포맷을 바꿀 때마다 올리고, GmTool 쪽
    // ToolLinkProtocol.ProtocolVersion과 값이 같아야 ToolHello가 통과한다 -- 서버만 고쳐놓고
    // 툴을 안 고쳤을 때 "패킷이 이상하게 파싱되는" 대신 접속 단계에서 바로 걸러내기 위함이다.
    inline constexpr uint32_t kToolLinkProtocolVersion = 1;

    // 가변 길이 패킷(ToolHello / NoticeRequest / MailSendRequest / CouponChunkPush /
    // ClientListReply)의 바디 구조는 docs/design/wire-format.md 에 있다.
    //
    // 주의: CouponChunkPush는 MaxBodySize(8192) 때문에 한 패킷에 250개 정도가 상한이라,
    // 운영툴이 "DB 벌크 청크"와 "World 전송 청크"를 다른 크기로 나눠 쓴다.
}
