#pragma once

#include <cstdint>

namespace World
{
    // 운영툴(Tool/GmTool) <-> World 사이 프로토콜(T2W/W2T). Gateway/Zone 링크와 마찬가지로
    // 독립된 id 공간이며, 전용 accept 포트(WorldServerConfig::toolPort)로만 들어온다 --
    // 클라이언트 트래픽이 들어오는 게이트웨이 포트와 물리적으로 분리해두면 "운영 권한 패킷은
    // 이 포트에서만 온다"는 것 자체가 1차 방어선이 된다.
    //
    // 요청(T2W) 계열은 페이로드 맨 앞에 항상 requestId(uint32)를 둔다 -- 운영툴은 HTTP 요청
    // 하나에 대해 World의 처리 결과를 기다려야 하는데(웹 UI에 성공/실패를 보여줘야 한다),
    // 소켓 하나에 여러 요청이 동시에 흘러다니므로 응답(ToolCommandAck/ClientListReply)을
    // 어느 요청의 답인지 짝지을 키가 필요하다.
    enum class ToolLinkPacketId : uint16_t
    {
        ToolHello = 1,           // T2W: 운영툴 인증(공유 시크릿). 통과 전에는 다른 패킷을 전부 거부한다
        ToolHelloAck = 2,        // W2T: 인증 결과
        NoticeRequest = 3,       // T2W: 접속 중 전체 클라이언트에게 공지 브로드캐스트
        MailSendRequest = 4,     // T2W: 특정 클라이언트 또는 전체에게 우편 발송
        MailDeleteRequest = 5,   // T2W: 특정 클라이언트의 우편 1건 삭제
        CouponChunkPush = 6,     // T2W: 운영툴이 로컬에서 생성한 쿠폰 번호 묶음(청크) 적재 요청
        ClientListRequest = 7,   // T2W: 지금 접속 중인 클라이언트 목록 조회(우편 대상 선택용)
        ClientListReply = 8,     // W2T: ClientListRequest의 응답
        ToolCommandAck = 9,      // W2T: Notice/Mail/Coupon 요청의 처리 결과
    };

    // ToolCommandAckPacket::resultCode 값. 운영툴은 내부 도구라서(공격자에게 노출되는
    // 클라이언트 API가 아니다) 실패 원인을 구체적으로 알려주는 편이 운영에 유리하다 --
    // 반대로 유저용 쿠폰 등록 API는 원인을 뭉뚱그려야 한다(GmTool의 CouponRedeemService 참고).
    enum class EToolResultCode : uint16_t
    {
        Ok = 0,
        NotAuthenticated = 1,  // ToolHello를 통과하지 않은 세션
        BadRequest = 2,        // 페이로드 파싱 실패 / 필수 필드 누락
        TargetNotFound = 3,    // 대상 clientSessionId가 접속 중이 아님
        ZoneUnavailable = 4,   // 대상 플레이어가 속한 존 서버와의 연결이 없음
    };
}
