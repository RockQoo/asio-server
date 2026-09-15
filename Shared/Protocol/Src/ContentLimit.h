#pragma once

#include <cstddef>

namespace Protocol
{
    // 가변 길이 본문의 상한. **Zone/World/클라이언트가 함께 지켜야 하는 계약**이라 여기 있다.
    //
    // 왜 필요한가: 프레임 헤더의 bodySize가 uint16이고 받는 쪽 Packet::Buffer는
    // Header::MaxBodySize()(8192)를 넘는 본문을 만나면 **예외를 던지고 연결을 끊는다.**
    // 콘텐츠가 상한 없이 문자열을 이어 붙이면 그 연결이 죽고, 서버 간 링크(Gateway<->World,
    // Zone<->World)는 재연결이 없어서 그대로 끝난다. 즉 **한 사람의 긴 채팅 한 줄이 그 링크에
    // 붙은 전원을 끊을 수 있다** -- 실제로 재현해서 확인했다.
    //
    // 클라이언트 패킷은 홉을 지날 때마다 릴레이 봉투(World::ClientEnvelopeHeader, 10바이트)에
    // 한 번 더 감싸이므로, 콘텐츠가 쓸 수 있는 실질 예산은 8192보다 그만큼 작다.
    inline constexpr size_t kRelayEnvelopeBytes = 10;

    // 채팅 한 줄(UTF-8 바이트). 한글은 글자당 3바이트라 약 85자다.
    inline constexpr size_t kMaxChatBytes = 256;

    // 우편 제목/본문(UTF-8 바이트). **Sql/mails.sql의 NVARCHAR(128)/(1024)와 짝이다.**
    // NVARCHAR는 바이트가 아니라 문자 수라, ASCII만 들어와도 넘지 않도록 **컬럼 크기를 그대로
    // 바이트 상한으로** 쓴다(한글이면 42자/341자가 되지만, 넘겨서 DB가 "String or binary data
    // would be truncated"로 실패하는 것보다 낫다).
    inline constexpr size_t kMaxMailTitleBytes = 128;
    inline constexpr size_t kMaxMailBodyBytes = 1024;

    // 우편함 한 개가 가질 수 있는 통 수. 이것만으로는 위 8192를 보장하지 못한다 --
    // 제목·본문이 길면 몇 통만으로도 넘는다. 그래서 World가 W2ZEnterZone 본문을 만들 때
    // **바이트 예산으로 한 번 더 잘라낸다**(Packet/EnterZoneBody.h).
    inline constexpr size_t kMaxMailCount = 100;

    // 유닛 상태/등장 통지 한 장에 실을 유닛 수. 넘으면 **여러 장으로 쪼개서** 보낸다
    // (자르지 않는다 -- 잘라내면 그 유닛의 HP가 화면에서 영영 갱신되지 않는다).
    //
    // 64로 잡은 근거: 등장 통지 항목이 29바이트라 64개면 약 1.9KB다. 위 8192에 봉투
    // 10바이트를 빼도 한참 남고, 존 하나의 인구가 그보다 많아도 장수만 늘어난다.
    inline constexpr size_t kMaxUnitsPerPacket = 64;
}
