#pragma once

#include <cstdint>

namespace Zone
{
    // 고정 레이아웃 페이로드들.
    // Chat은 의도적으로 고정 구조체가 없다 -- 크기가 가변적이라서 BinaryWriter::WriteString /
    // BinaryReader::ReadString으로 길이 접두 문자열을 직접 쓰고 읽는다.
#pragma pack(push, 1)
    struct MovePacket
    {
        float x;
        float y;
    };

    struct EnterZoneNotifyPacket
    {
        uint32_t playerId;
        uint32_t zoneId;
    };

    // Mail 요청의 결과는 고정 구조체가 아니라 Z2CTaskResult(UnitOfWork 태스크 스트림)로
    // 돌아간다 -- 응답 구조체를 콘텐츠마다 새로 만드는 대신, 클라이언트가 서버와 같은
    // 태스크 목록을 그대로 적용하는 방식이다(Shared/Protocol/Src/TaskKind.h 참고).
#pragma pack(pop)
}
