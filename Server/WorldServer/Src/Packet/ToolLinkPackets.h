#pragma once

#include "Shared/Protocol/Src/Ids.h"
#include "Shared/Core/Src/Common/RUID.h"

namespace World
{
    // 운영툴 링크의 고정 레이아웃 페이로드들. 가변 길이(문자열이 섞인) 패킷은 구조체를 두지
    // 않고 BinaryWriter/BinaryReader로 직접 쓰고 읽으며, 그 와이어 포맷은 이 파일 아래쪽
    // 주석에 모아 적어둔다(ZoneLinkPackets.h의 UnitOfWorkStream과 같은 방식).
#pragma pack(push, 1)
    struct ToolHelloResultPacket
    {
        uint32_t requestId;
        uint8_t accepted;          // 0 = 거부(시크릿 불일치 또는 프로토콜 버전 불일치)
        uint32_t protocolVersion;  // World가 지원하는 버전 -- 툴이 자기 버전과 비교해 로그를 남긴다
    };

    struct ToolCommandResultPacket
    {
        uint32_t requestId;
        uint16_t resultCode;     // EToolResultCode
        uint32_t affectedCount;  // 실제로 처리된 대상 수(전체 우편/공지에서 몇 명에게 나갔는지)
    };

    struct ToolMailDeletePacket
    {
        uint32_t requestId;
        uint64_t clientSessionId;
        Protocol::MailId mailId;
    };

    struct ToolClientListPacket
    {
        uint32_t requestId;
    };

    // ClientListReply 본문에 반복되는 항목
    struct ToolClientEntry
    {
        uint64_t clientSessionId;
        uint32_t zoneId;
    };
#pragma pack(pop)

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
