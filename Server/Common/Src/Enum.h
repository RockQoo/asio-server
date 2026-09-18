#pragma once

#include <cstdint>

// 콘텐츠 enum 모음. 게임 규칙상의 "값 목록"(재화 종류, 앞으로 아이템/우편 종류 등)은
// 하나씩 파일을 만들지 않고 여기 모은다 -- 하나하나는 열 줄 남짓이고, 새 콘텐츠를 붙일 때
// "이 서버가 아는 값이 무엇인지"를 한 파일에서 훑는 편이 낫다.
//
// **여기 두지 않는 것**(각자 파일이 따로 있는 이유가 있다):
//   - Common::PacketId    방향별 번호 대역 규약이 붙어 있다(.claude/rules/packet-naming.md)
//   - Common::EErrorCode  콘텐츠별 100 단위 대역 규약이 붙어 있다
//   - TaskKind.h 의 enum들  MakeTaskKind/CategoryOf/SubTaskOf 와 한 세트라 떼면 규약이 갈린다
//
// 값을 지워도 번호를 재사용하지 않는다 -- DB/로그에 남은 옛 값이 다른 뜻이 된다.
namespace Common
{
    // 재화 종류. Zone이 판정하고 World가 DB에 반영하고 클라이언트가 화면에 표시하는, 세 쪽이
    // 공유하는 계약이다.
    // 0은 "종류 없음/미지정" 예약값이다 -- 기본값으로 초기화된 값이 실수로 골드를 가리키는
    // 것보다, 알 수 없는 종류로 즉시 거부되는 게 낫다(zoneId를 1부터 세는 것과 같은 판단).
    enum class ECurrencyType : uint8_t
    {
        None = 0,
        Gold = 1,
    };
}

// PacketId/ErrorCode와 같은 이유로 `Common::` 없이 바로 쓰기 위한 전역 노출.
using Common::ECurrencyType;
