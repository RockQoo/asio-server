#pragma once

#include "Shared/Protocol/Src/Ids.h"

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
        // 로그인이 확정한 DB의 player_id(RUID). **재접속해도 같다.**
        Protocol::PlayerId playerId;

        // 이 클라이언트의 세션 id. **playerId와 용도가 다르다** -- Z2CMoveNotify /
        // Z2CChatNotify 가 "누가" 보냈는지를 이 값으로 싣기 때문에, 클라이언트가 그 통지들
        // 중 자기 것을 가려내려면 이 값이 필요하다.
        //
        // 예전에는 playerId 가 clientSessionId 를 uint32 로 자른 값이라 하나로 둘 다
        // 됐는데, 그건 우연이었고 재접속하면 playerId 가 바뀌는 결함이기도 했다.
        uint64_t clientSessionId;

        Protocol::ZoneId zoneId;
    };

    // Mail 요청의 결과는 고정 구조체가 아니라 Z2CTaskResult(UnitOfWork 태스크 스트림)로
    // 돌아간다 -- 응답 구조체를 콘텐츠마다 새로 만드는 대신, 클라이언트가 서버와 같은
    // 태스크 목록을 그대로 적용하는 방식이다(Shared/Protocol/Src/TaskKind.h 참고).

    // --- 전투 ---
    //
    // **전투는 Z2CTaskResult를 쓰지 않는다.** 그 통로는 UnitOfWork(= DB로 나가는 변경)의
    // 결말을 나르는 것인데, HP/MP는 영속 대상이 아니라 UnitOfWork를 애초에 열지 않는다.
    // 그래서 결과도 고정 구조체로 돌려준다 -- 전투 결과가 DB 경로를 타지 않는다는 사실이
    // 와이어 포맷에도 그대로 드러난다.

    struct AttackPacket
    {
        uint32_t targetUnitId;
        uint8_t attackKind;  // Combat::EAttackKind
    };

    // 시전자에게만. **실패해도 반드시 보낸다** -- 무응답으로 멈추는 경로를 만들지 않는다.
    // 실패면 damage/targetHp는 0이다.
    struct AttackResultPacket
    {
        int32_t errorCode;  // Protocol::EErrorCode
        uint32_t targetUnitId;
        uint8_t attackKind;
        int32_t damage;
        int32_t targetHp;
    };

    // 시전자를 뺀 주변에게. 시전자는 자기 AttackResult로 같은 연출을 그린다.
    struct UnitAttackNotifyPacket
    {
        uint32_t attackerUnitId;
        uint32_t targetUnitId;
        uint8_t attackKind;
        int32_t damage;
    };

    struct UnitDeadPacket
    {
        uint32_t unitId;
        uint32_t killerUnitId;
    };

    // 존을 떠난 것이라 **사망과 다르다** -- 사망한 유닛은 자리에 남아 리스폰을 기다린다.
    struct UnitDespawnPacket
    {
        uint32_t unitId;
    };

    // Z2CUnitSpawn / Z2CUnitStateSync는 항목 수(uint16)를 먼저 쓰고 아래 구조체를 그 수만큼
    // 이어 붙인다. 한 장에 담는 수는 Protocol::kMaxUnitsPerPacket으로 끊고 넘치면 장을 나눈다.
    struct UnitSpawnEntry
    {
        uint32_t unitId;
        uint8_t kind;  // Combat::EUnitKind
        float x;
        float y;
        int32_t hp;
        int32_t maxHp;
        int32_t mp;
        int32_t maxMp;
    };

    struct UnitStateEntry
    {
        uint32_t unitId;
        int32_t hp;
        int32_t mp;
    };
#pragma pack(pop)
}
