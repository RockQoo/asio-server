#pragma once

#include <cstdint>

namespace World
{
#pragma pack(push, 1)
    struct ZoneRegisterPacket
    {
        uint32_t zoneId;
        float xMin;
        float xMax;
    };

    // EnterZoneRequest와 ZoneTransferRequest가 공유하는 페이로드 -- 둘 다 "이 플레이어가 이
    // 좌표에서 이 존에 있어야 한다"는 같은 정보를 담기 때문에 구조체를 나누지 않았다.
    // zoneId 의미: EnterZoneRequest(W2Z)에서는 "목표 존"(그 존 서버 프로세스가 여러 존을
    // 호스팅할 수 있으므로 어느 zoneId로 들어가야 하는지 명시 필요), ZoneTransferRequest(Z2W)
    // 에서는 "보내는 쪽(현재) 존"(World 쪽 로그용 -- 실제 라우팅 대상은 x로 결정된다).
    struct PlayerZoneStatePacket
    {
        uint32_t zoneId;
        uint64_t clientSessionId;
        uint32_t playerId;
        float x;
        float y;
    };

    struct LeaveZoneNotifyPacket
    {
        uint64_t clientSessionId;
    };
#pragma pack(pop)

    // UnitOfWorkStream(Z2W)의 바디는 가변 길이(문자열 포함)라 고정 구조체 대신 BinaryWriter/
    // BinaryReader로 직접 쓰고 읽는다. 순서:
    //   playerId(uint32) +
    //   [Core::Task::UnitOfWork가 직렬화한 제너릭 태스크 목록: ownerId(uint64, = clientSessionId)
    //    + taskCount(uint16) + taskCount개의 { kind(uint16) + payloadLen(uint32) + payload }]
    // Core는 이 kind/payload의 실제 의미를 모른다 -- kind는 Shared/Protocol/Src/TaskKind.h가
    // 정의하는 "상위 8비트 카테고리 + 하위 8비트 세부 동작"이다. 지금 유일하게 이 스트림을
    // 쓰는 Mail 태스크는 카테고리 Mail + Added/Removed이고, 둘 다 payload 레이아웃이 같다:
    // mailId(uint32) + WriteString(title) + WriteString(body) + sendUt(int64) + endUt(int64).
    // 삭제 태스크가 지워진 내용을 통째로 싣는 이유는 그게 곧 롤백(되살리기)에 필요한
    // 정보이기 때문이다(Shared/Core/Src/Task/UnitOfWork.h 주석 참고).
    // 쓰는 쪽: Server/ZoneServer/Src/Mail/MailModel.cpp(태스크별 payload 직렬화) +
    // Server/ZoneServer/Src/Task/ZoneUnitOfWork.cpp(playerId 접두 + UnitOfWorkStream 전송).
    // 읽는 쪽: Server/WorldServer/Src/Handler/ZoneLinkHandler.cpp.
}
