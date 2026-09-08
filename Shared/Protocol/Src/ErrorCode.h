#pragma once

#include <cstdint>

namespace Protocol
{
    // 콘텐츠 처리 결과 코드. Zone이 판정해서 UnitOfWork에 싣고, 클라이언트가 요청 실패를
    // 표시하는 데 쓴다 -- 두 쪽이 공유하는 계약이라 PacketId와 같은 이유로 여기 있다.
    //
    // Core(Shared/Core/Src/Common/CoreErrorCode.h)에 두지 않는 이유: Core는 콘텐츠를 모르는
    // 정적 라이브러리라, 메일/인벤 에러가 하나 늘 때마다 Core.lib과 그걸 참조하는 실행 파일
    // 전부가 다시 빌드된다(카테고리 enum을 프로젝트별로 따로 두는 ELogCategory와 같은 이유).
    // 프레이밍/프로토콜 위반은 여기가 아니라 Common::ECoreErrorCode다.
    //
    // 콘텐츠별로 100 단위 대역을 쓴다 -- 로그에 숫자만 남아도 어느 콘텐츠의 실패인지 바로
    // 알 수 있다. 대역 안에서는 "대역 시작"부터 세고, 지운 값은 재사용하지 않는다.
    enum class EErrorCode : int32_t
    {
        Success = 0,

        // ---- 공통 (1 ~ 99) ----
        InvalidPayload = 1,  // 패킷 본문 파싱 실패 / 필수 필드 누락

        // ---- Mail (100 ~ 199) ----
        MailNotFound = 100,       // 대상 mailId가 우편함에 없음
        MailAlreadyExists = 101,  // 배정하려는 mailId가 이미 우편함에 있음(id 발급 버그 신호)
        MailBoxNotFound = 102,    // 이 세션의 우편함 자체가 없음(입장 처리 누락 신호)

        // ---- Currency (200 ~ 299) ----
        NotEnoughCurrency = 200,      // 잔액 부족. 부분 차감을 하지 않으므로 아무것도 안 바뀐다
        UnknownCurrencyType = 201,    // 알 수 없는 재화 종류(None 포함)
        InvalidCurrencyAmount = 202,  // 증감량이 음수(증가/감소를 한 함수로 섞지 않는다)
    };
}

// PacketId와 같은 이유로 `Protocol::` 없이 바로 쓰기 위한 전역 노출. `enum class`라
// 열거자는 여전히 `EErrorCode::`로 한정해야 하고, 정수로의 암묵 변환도 그대로 막힌다.
using Protocol::EErrorCode;
