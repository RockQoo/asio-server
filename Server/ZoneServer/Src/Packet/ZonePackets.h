#pragma once

#include "Shared/Common/Src/Ids.h"

namespace Zone
{
    // 고정 레이아웃 페이로드들.
    // Chat은 의도적으로 고정 구조체가 없다 -- 크기가 가변적이라서 BinaryWriter::WriteString /
    // BinaryReader::ReadString으로 길이 접두 문자열을 직접 쓰고 읽는다.
#pragma pack(push, 1)
    struct Position
    {
        float x;
        float y;
    };

    struct Z2CEnterZoneNotify
    {
        // 로그인이 확정한 DB의 player_id(RUID). **재접속해도 같다.**
        Common::PlayerId playerId;

        // 이 클라이언트의 세션 id. **playerId와 용도가 다르다** -- Z2CMoveNotify /
        // Z2CChatNotify 가 "누가" 보냈는지를 이 값으로 싣기 때문에, 클라이언트가 그 통지들
        // 중 자기 것을 가려내려면 이 값이 필요하다.
        //
        // 예전에는 playerId 가 clientSessionId 를 uint32 로 자른 값이라 하나로 둘 다
        // 됐는데, 그건 우연이었고 재접속하면 playerId 가 바뀌는 결함이기도 했다.
        uint64_t clientSessionId;

        Common::ZoneId zoneId;
    };

    // Mail 요청의 결과는 고정 구조체가 아니라 Z2CTaskResult(UnitOfWork 태스크 스트림)로
    // 돌아간다 -- 응답 구조체를 콘텐츠마다 새로 만드는 대신, 클라이언트가 서버와 같은
    // 태스크 목록을 그대로 적용하는 방식이다(Shared/Common/Src/TaskKind.h 참고).
#pragma pack(pop)
}
