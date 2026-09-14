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

    // W2ZEnterZone과 Z2WZoneTransfer가 공유하는 **고정 머리** -- 둘 다 "이 플레이어가 이
    // 좌표에서 이 존에 있어야 한다"는 같은 정보를 담기 때문에 구조체를 나누지 않았다.
    // zoneId 의미: W2ZEnterZone에서는 "목표 존"(그 존 서버 프로세스가 여러 존을 호스팅할 수
    // 있으므로 어느 zoneId로 들어가야 하는지 명시 필요), Z2WZoneTransfer에서는 "보내는 쪽
    // (현재) 존"(World 쪽 로그용 -- 실제 라우팅 대상은 x로 결정된다).
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

    // ---- W2ZEnterZone 의 가변 길이 꼬리 ----
    //
    // 고정 머리(PlayerZoneStatePacket) 뒤에 **World 가 캐시하고 있는 그 플레이어의 콘텐츠**가
    // 이어진다. 존은 이걸 받아 Mail/Currency 모델을 채운 상태로 Player 를 만든다.
    //
    //   uint16  mailCount
    //     반복: int64 mailId(RUID), string title, string body, int64 sendUt, int64 endUt
    //   uint16  currencyCount
    //     반복: uint8 currencyType, int64 amount
    //
    // (string = uint16 길이 + UTF-8 바이트. Packet::BinaryWriter::WriteString 형식.)
    //
    // **왜 존이 DB 를 직접 안 읽고 World 가 실어 보내는가**: DB 로 나가는 관문을 World 하나로
    // 유지하기 위해서다. 존이 직접 읽기 시작하면 커넥션 관리가 두 프로세스로 갈리고, 나중에
    // 존이 올린 UnitOfWork 를 World 캐시와 대조해 위조를 거르는 자리가 사라진다.
    //
    // **핸드오프에도 같은 포맷이 쓰인다** -- 존을 넘을 때 World 가 캐시에서 다시 실어 보낸다.
    //
    // 다만 지금은 **캐시가 로그인 시점의 DB 스냅샷에서 멈춰 있다.** 존이 올린 UnitOfWork 를
    // World 캐시에 반영하는 단계가 아직 없어서(ZoneLinkHandler 의 ApplyMailTask 가 로그만
    // 남긴다), 존에서 새로 만든 우편은 프로세스를 넘는 핸드오프에서 여전히 사라진다.
    // 같은 프로세스 안의 가로 이동은 Player 객체가 그대로 살아 있어 영향이 없다.
    //
    // 쓰는 쪽은 Packet/EnterZoneBody.h, 읽는 쪽은 ZoneServer 의 WorldLinkHandler 다.
    // 한쪽만 고치면 조용히 어긋나므로 이 표가 유일한 계약이다.

    // UnitOfWorkStream(Z2W)의 바디도 가변 길이라 고정 구조체가 없다 -- BinaryWriter/Reader로
    // 직접 쓰고 읽는다. 바이트 순서와 taskKind 해석은 docs/design/wire-format.md.
    //
    // Core는 kind/payload의 의미를 모른다. 쓰는 쪽은 Model.cpp + UnitOfWork.cpp,
    // 읽는 쪽은 ZoneLinkHandler.cpp 다.
}
