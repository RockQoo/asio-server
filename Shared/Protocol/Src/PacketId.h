#pragma once

#include <cstdint>

namespace Protocol
{
    // Packet::PacketHeader::id에 실리는 애플리케이션 수준 패킷 타입 id 전체.
    //
    // 이름 앞 3글자는 예외 없이 "보내는 노드 2 받는 노드"다(C=클라, T=운영툴, W=World,
    // G=Gateway, Z=Zone). 기준은 논리적 주체가 아니라 **실제로 그 패킷을 소켓에 쓰는
    // 프로세스**다 -- G2WClientConnected는 클라이언트 접속을 알리는 내용이지만 보내는 건
    // GatewayServer 프로세스이므로 C2W가 아니라 G2W다.
    //
    // 방향마다 1000 단위로 대역을 잘라 쓴다. PacketHeader에는 id 하나뿐이라 헤더만 봐서는
    // 어느 링크에서 온 값인지 알 수 없는데, 대역이 겹치지 않으면 값 하나로 판정된다 --
    // 잘못 흘러든 패킷이 다른 뜻으로 조용히 해석되는 대신 미등록 id로 즉시 튕긴다.
    // 대역 안에서는 "대역 시작 + 1"부터 세고, 지운 패킷의 번호는 재사용하지 않는다.
    //
    // 자세한 규약은 .claude/rules/packet-naming.md 참고.
    enum class PacketId : uint16_t
    {
        // ---- C2Z : 클라이언트 -> Zone (1 ~ 999) ----
        C2ZEcho = 1,     // 상태 확인용. 공유 상태가 없어 Zone의 LB 스레드가 즉시 되돌려준다
        C2ZChat = 2,     // 길이 접두 문자열
        C2ZMove = 3,     // MovePacket. 담당 TaskWorker에서 처리
        C2ZMailAdd = 4,  // title/body/durationSec, Task::UnitOfWork 경유
        C2ZMailDel = 5,  // mailId, Task::UnitOfWork 경유

        // ---- Z2C : Zone -> 클라이언트 (1000 ~ 1999) ----
        Z2CEchoAck = 1001,          // C2ZEcho의 응답. 본문은 받은 것 그대로
        Z2CChatNotify = 1002,       // sessionId + 메시지. 존 내부 브로드캐스트
        Z2CMoveNotify = 1003,       // sessionId + MovePacket. 존 내부 브로드캐스트
        Z2CEnterZoneNotify = 1004,  // 존 배정(신규 입장 또는 핸드오프 전입) 통지
        Z2CMailAddAck = 1005,       // 실제 배정된 mailId(MailAddAckPacket)
        Z2CMailDelAck = 1006,       // 삭제 요청 결과(MailDelAckPacket)

        // ---- C2W : 클라이언트 -> World (2000 ~ 2999) ----
        // 로그인/인증 자리. 현재 비어 있다.

        // ---- W2C : World -> 클라이언트 (3000 ~ 3999) ----
        W2CNotice = 3001,  // World가 Zone을 거치지 않고 접속 중 전체에게 직접 브로드캐스트

        // ---- G2W : Gateway -> World (4000 ~ 4999) ----
        G2WClientConnected = 4001,     // 클라이언트 accept 직후. payload = clientSessionId(8바이트)
        G2WClientDisconnected = 4002,  // 클라이언트 접속 종료. payload = clientSessionId(8바이트)
        G2WRelay = 4003,               // ClientEnvelopeHeader + 클라이언트 원본 패킷 그대로

        // ---- W2G : World -> Gateway (5000 ~ 5999) ----
        W2GRelay = 5001,  // ClientEnvelopeHeader + 클라이언트에게 보낼 원본 패킷 그대로

        // ---- W2Z : World -> Zone (6000 ~ 6999) ----
        W2ZEnterZoneRequest = 6001,  // 플레이어를 이 존에 입장(신규 배정 또는 핸드오프 전입)시킴
        W2ZLeaveZoneNotify = 6002,   // 접속 종료로 이 존에서 플레이어를 제거하라는 지시
        W2ZRelay = 6003,             // ClientEnvelopeHeader + 클라이언트 원본 패킷 그대로

        // ---- Z2W : Zone -> World (7000 ~ 7999) ----
        Z2WZoneRegister = 7001,         // Zone 접속 직후, 이 Zone이 담당하는 x구간을 알림
        Z2WRelay = 7002,                // ClientEnvelopeHeader + 클라이언트에게 보낼 원본 패킷 그대로
        Z2WZoneTransferRequest = 7003,  // 존 경계를 넘는 이동. Zone이 로컬 상태를 먼저 지우고 요청한다
        Z2WUnitOfWorkStream = 7004,     // Mail 등 변경 이벤트 묶음(Task::UnitOfWork가 직렬화)

        // ---- T2W : 운영툴 -> World (8000 ~ 8999) ----
        // 요청 계열은 페이로드 맨 앞에 항상 requestId(uint32)를 둔다 -- 소켓 하나에 여러
        // 요청이 동시에 흘러다니므로 응답을 어느 요청의 답인지 짝지을 키가 필요하다.
        T2WToolHello = 8001,          // 공유 시크릿 인증. 통과 전에는 다른 패킷을 전부 거부한다
        T2WNoticeRequest = 8002,      // 접속 중 전체에게 공지 브로드캐스트
        T2WMailSendRequest = 8003,    // 특정 클라이언트 또는 전체에게 우편 발송
        T2WMailDeleteRequest = 8004,  // 특정 클라이언트의 우편 1건 삭제
        T2WCouponChunkPush = 8005,    // 운영툴이 로컬에서 생성한 쿠폰 번호 묶음(청크) 적재
        T2WClientListRequest = 8006,  // 지금 접속 중인 클라이언트 목록 조회

        // ---- W2T : World -> 운영툴 (9000 ~ 9999) ----
        W2TToolHelloAck = 9001,     // 인증 결과
        W2TClientListReply = 9002,  // T2WClientListRequest의 응답
        W2TToolCommandAck = 9003,   // 공지/우편/쿠폰 요청의 처리 결과
    };

    // 대역에서 되뽑은 방향. 타입을 방향별로 쪼개지 않는 대신 이 값으로 검증한다.
    enum class EPacketDirection : uint8_t
    {
        Unknown = 0,
        C2Z, Z2C, C2W, W2C, G2W, W2G, W2Z, Z2W, T2W, W2T,
    };

    [[nodiscard]] constexpr EPacketDirection DirectionOf(const PacketId id) noexcept
    {
        switch (static_cast<uint16_t>(id) / 1000)
        {
        case 0: return EPacketDirection::C2Z;
        case 1: return EPacketDirection::Z2C;
        case 2: return EPacketDirection::C2W;
        case 3: return EPacketDirection::W2C;
        case 4: return EPacketDirection::G2W;
        case 5: return EPacketDirection::W2G;
        case 6: return EPacketDirection::W2Z;
        case 7: return EPacketDirection::Z2W;
        case 8: return EPacketDirection::T2W;
        case 9: return EPacketDirection::W2T;
        default: return EPacketDirection::Unknown;
        }
    }

    // ClientEnvelopeHeader::innerPacketId 검증용. 중계 서버(Gateway/World)는 봉투 안을
    // 해석하지 않지만, "클라이언트가 주고받는 패킷이 들어 있다"는 것만은 값 하나로 확인할 수
    // 있다 -- 겉봉투 방향(예: Z2WRelay)과 안쪽 내용물 방향(예: Z2CMoveNotify)은 별개라
    // 안쪽에 서버 내부 링크 id가 실리면 조작이거나 버그다.
    [[nodiscard]] constexpr bool IsClientPacket(const uint16_t rawId) noexcept
    {
        return rawId >= 1 && rawId < 4000;
    }
}

// 어디서든 `Protocol::` 없이 `PacketId::T2WToolHello`처럼 바로 쓰기 위한 전역 노출.
// `ELogCategory`(Shared/Core/Src/Log/LogCategory.h)와 같은 방식이고, 이쪽은 프로젝트를
// 통틀어 이 enum 하나뿐이라 이름이 겹칠 여지도 없다. `enum class`라 `using enum`이
// 아니므로 열거자는 여전히 `PacketId::`로 한정해야 하고, 정수로의 암묵 변환도 그대로
// 막힌다 -- 줄어드는 건 네임스페이스 한 겹뿐이다.
using Protocol::PacketId;
