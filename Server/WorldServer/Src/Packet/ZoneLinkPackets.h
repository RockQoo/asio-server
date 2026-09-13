#pragma once

namespace World
{
#pragma pack(push, 1)
    // 존이 자기 담당 사각형을 알려온다. World는 이 사각형만으로 라우팅하므로 존 배치 규칙
    // (격자든 CSV든)을 알 필요가 없다 -- 배치를 바꿔도 World 코드는 그대로다.
    struct ZoneRegisterPacket
    {
        uint32_t zoneId;
        float xMin;
        float xMax;
        float yMin;
        float yMax;
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

    // UnitOfWorkStream(Z2W)의 바디는 가변 길이라 고정 구조체가 없다 -- BinaryWriter/Reader로
    // 직접 쓰고 읽는다. 바이트 순서와 taskKind 해석은 docs/design/wire-format.md.
    //
    // Core는 kind/payload의 의미를 모른다. 쓰는 쪽은 Model.cpp + UnitOfWork.cpp,
    // 읽는 쪽은 ZoneLinkHandler.cpp 다.
}
