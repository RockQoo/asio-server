#pragma once

#include <cstdint>

namespace World
{
    // 운영툴 링크의 고정 레이아웃 페이로드들. 가변 길이(문자열이 섞인) 패킷은 구조체를 두지
    // 않고 BinaryWriter/BinaryReader로 직접 쓰고 읽으며, 그 와이어 포맷은 이 파일 아래쪽
    // 주석에 모아 적어둔다(ZoneLinkPackets.h의 UnitOfWorkStream과 같은 방식).
#pragma pack(push, 1)
    struct ToolHelloAckPacket
    {
        uint32_t requestId;
        uint8_t accepted;          // 0 = 거부(시크릿 불일치 또는 프로토콜 버전 불일치)
        uint32_t protocolVersion;  // World가 지원하는 버전 -- 툴이 자기 버전과 비교해 로그를 남긴다
    };

    struct ToolCommandAckPacket
    {
        uint32_t requestId;
        uint16_t resultCode;     // EToolResultCode
        uint32_t affectedCount;  // 실제로 처리된 대상 수(전체 우편/공지에서 몇 명에게 나갔는지)
    };

    struct ToolMailDeleteRequestPacket
    {
        uint32_t requestId;
        uint64_t clientSessionId;
        uint32_t mailId;
    };

    struct ToolClientListRequestPacket
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

    // 가변 길이 패킷 와이어 포맷(전부 리틀엔디언, 문자열은 BinaryWriter::WriteString =
    // 길이(uint16) + UTF-8 바이트):
    //
    // ToolHello(T2W)
    //   requestId(uint32) + protocolVersion(uint32) + String(sharedSecret) + String(operatorName)
    //
    // NoticeRequest(T2W)
    //   requestId(uint32) + String(message)
    //   -> World가 Zone을 거치지 않고 접속 중 전체 클라이언트에게 PacketId::W2CNotice를 직접 보낸다.
    //
    // MailSendRequest(T2W)
    //   requestId(uint32) + targetKind(uint8) + clientSessionId(uint64)
    //     + String(title) + String(body) + durationSec(int64)
    //   targetKind: 0 = 접속 중 전체(clientSessionId 무시), 1 = clientSessionId 한 명
    //   -> 존으로 ForwardToZone(innerPacketId = PacketId::C2ZMailAdd) 형태로 "그 클라이언트가
    //      직접 보낸 것처럼" 주입한다. 그래서 Zone/Mail 쪽은 코드를 한 줄도 고치지 않아도 되고,
    //      MailAddAck / UnitOfWork(DB 반영) 경로도 평소와 완전히 동일하게 흐른다.
    //
    // CouponChunkPush(T2W)
    //   requestId(uint32) + String(campaignCode) + chunkSeq(uint32) + couponCount(uint32)
    //     + couponCount개의 String(couponCode)
    //   주의: Packet::PacketHeader::MaxBodySize()가 8192바이트라, 코드 하나가 27바이트
    //   (길이 2 + 25자)인 것을 감안하면 한 패킷에 250개 정도가 상한이다. 그래서 운영툴은
    //   "DB 벌크 인서트 청크"(수천~수만 건)와 "World 전송 청크"(200건)를 서로 다른 크기로
    //   나눠서 쓴다 -- GmTool의 CouponIssueService 주석 참고.
    //
    // ClientListReply(W2T)
    //   requestId(uint32) + count(uint32) + count개의 ToolClientEntry
    //   count는 MaxBodySize에 맞춰 잘라서 보낸다(현재 상한 kMaxClientListEntries).
}
