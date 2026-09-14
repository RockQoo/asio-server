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
        MailBoxFull = 103,        // 우편함이 상한(Protocol::kMaxMailCount)에 도달함
        MailTextTooLong = 104,    // 제목/본문이 상한을 넘음(ContentLimit.h -- DB 컬럼과 짝)

        // ---- Currency (200 ~ 299) ----
        NotEnoughCurrency = 200,      // 잔액 부족. 부분 차감을 하지 않으므로 아무것도 안 바뀐다
        UnknownCurrencyType = 201,    // 알 수 없는 재화 종류(None 포함)
        InvalidCurrencyAmount = 202,  // 증감량이 음수(증가/감소를 한 함수로 섞지 않는다)

        // ---- Login (300 ~ 399) ----
        //
        // **계정이 없는 것은 실패가 아니다** -- 그 자리에서 만들고 성공으로 돌려준다
        // (Sql/players.sql의 usp_players_upsert가 자동 가입 경로다). 그래서 "없는 계정"에
        // 해당하는 코드가 여기 없고, 있어서도 안 된다.
        LoginInvalidInput = 300,          // 아이디/비밀번호가 비었거나 길이 제한을 넘음
        LoginWrongPassword = 301,         // 계정은 있는데 비밀번호가 다름
        LoginDbFailure = 302,             // DB 조회/생성이 실패했다(재시도하면 될 수 있다)
        LoginAlreadyAuthenticated = 303,  // 이미 로그인한 연결이 또 보냄
        LoginNoZoneAvailable = 304,       // 인증은 됐는데 입장시킬 존이 아직 World에 붙지 않음
    };
}

// PacketId와 같은 이유로 `Protocol::` 없이 바로 쓰기 위한 전역 노출. `enum class`라
// 열거자는 여전히 `EErrorCode::`로 한정해야 하고, 정수로의 암묵 변환도 그대로 막힌다.
using Protocol::EErrorCode;
