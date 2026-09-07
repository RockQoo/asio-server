#pragma once

#include <cstdint>

namespace World
{
    // ToolCommandAckPacket::resultCode 값. 운영툴은 내부 도구라서(공격자에게 노출되는
    // 클라이언트 API가 아니다) 실패 원인을 구체적으로 알려주는 편이 운영에 유리하다 --
    // 반대로 유저용 쿠폰 등록 API는 원인을 뭉뚱그려야 한다(GmTool의 CouponRedeemService 참고).
    enum class EToolResultCode : uint16_t
    {
        Ok = 0,
        NotAuthenticated = 1,  // T2WToolHello를 통과하지 않은 세션
        BadRequest = 2,        // 페이로드 파싱 실패 / 필수 필드 누락
        TargetNotFound = 3,    // 대상 clientSessionId가 접속 중이 아님
        ZoneUnavailable = 4,   // 대상 플레이어가 속한 존 서버와의 연결이 없음
    };
}
