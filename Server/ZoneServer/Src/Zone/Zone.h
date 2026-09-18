#pragma once

#include "App/ZoneDef.h"
#include "Unit/UnitContainer.h"

#include "Server/Core/Src/Network/SessionHolder.h"
#include "Server/Common/Src/PacketId.h"

// 존 하나의 **게임 세계**. 로스터와 공간 상태가 여기 있고, 그 위에서 도는 처리기
// (`ZoneProcessor` · `TickProcessor` · `BroadcastProcessor`)는 **데이터를 갖지 않는다.**
//
// **왜 처리기가 아니라 여기에 두는가**: 같은 존을 여러 레인이 본다.
//
//   ZoneProcessor(BASIC, owner = playerId)  판단하고 예약한다 -- 이동 검증, 콘텐츠, 입·퇴장
//   TickProcessor(TICK,  owner = 존 틱 주인) 예약된 것을 실행한다 -- 적분, 경계 판정, 만기
//
// 처리기마다 상태를 들면 같은 세계가 여러 벌이 된다. 그래서 세계는 이 객체 하나뿐이고,
// 처리기들이 `shared_ptr` 로 같은 것을 가리킨다.
//
// 존 경계를 넘은 유닛 하나.
struct ZoneCrossing final
{
    Common::UnitId unitId{};
    float x{};
    float y{};
};

// **World 링크를 존이 소유한다.** 연결이 zoneId 하나당 하나라, "이 유닛의 변경을 어디로
// 올리나"의 답이 곧 그 유닛이 속한 존이다.
class Zone final
{
public:
    using SPtr = std::shared_ptr<Zone>;

    Zone(const ZoneDef& def, Network::SessionHolder& worldLink);

    Zone(const Zone&) = delete;
    Zone& operator=(const Zone&) = delete;

    [[nodiscard]] Common::ZoneId GetZoneId() const noexcept { return def_.zoneId; }
    [[nodiscard]] const ZoneDef& GetDef() const noexcept { return def_; }

    // 이 존의 변경을 World 로 올리는 링크. Player 가 생성될 때 이걸 받아 든다.
    [[nodiscard]] Network::SessionHolder& GetWorldLink() const noexcept { return worldLink_; }

    [[nodiscard]] UnitContainer& Units() noexcept { return unitContainer_; }
    [[nodiscard]] const UnitContainer& Units() const noexcept { return unitContainer_; }

    // **TICK 레인 전용.** 유닛 전원을 (병렬로) 돌린 뒤, 그 사이에 예약된 입·퇴장을 확정한다.
    // 순서를 바꾸면 순회 중에 목록이 바뀐다.
    void Tick(const UnitTickContext& context);

    // ── BASIC 레인 ────────────────────────────────────────────────────────────
    //
    // 패킷 하나를 그 사람에게 내려보낸다. **처리기는 여기까지만 안다** -- 누가 받는지는
    // 컨테이너가 찾고, 무엇을 하는지는 그 유닛이 정한다.
    // 그 사람이 이 존에 없으면 false (아직 입장 전이거나 이미 퇴장했다).
    [[nodiscard]] bool Handle(const Common::PlayerId playerId, const PacketId packetId,
                              const std::span<const byte> payload);

    // 이 존 전원에게. **대상 목록을 여기서 만들어 BROADCAST 레인으로 넘긴다** -- 받는
    // 쪽이 컨테이너를 다시 보면 그 레인이 이 존의 락에 묶인다.
    void Fanout(const PacketId innerPacketId, const std::span<const byte> payload,
                const Network::SessionId excludeClientSessionId = 0) const;

    // 이 존의 한 명에게. Zone 은 클라이언트와 직접 연결되지 않으므로 World 를 거치는
    // 봉투에 담긴다 -- 부르는 쪽은 그걸 몰라도 된다.
    void SendToClient(const Network::SessionId clientSessionId, const PacketId innerPacketId,
                      const std::span<const byte> payload) const;

    // ── TICK 레인 ─────────────────────────────────────────────────────────────

        // **병렬 틱 중에 여러 스레드가 부른다.** 경계를 넘는 일은 드물어서(대부분의 틱에
    // 한 건도 없다) 작은 뮤텍스로 충분하다 -- 여기를 락 없이 만들려고 유닛마다 칸을
    // 잡아두면 접속자 수만큼 빈 칸이 생긴다.
    void ReportCrossing(const Common::UnitId unitId, const float x, const float y);

    // 틱이 끝난 뒤 TICK 레인이 한 번에 걷어간다. 걷어가면 통이 빈다.
    [[nodiscard]] std::vector<ZoneCrossing> TakeCrossings();

private:
    // 담당 구간을 필드로 흩지 않고 정의 그대로 들고 있는다 -- 경계 검사(ZoneDef::Contains)를
    // 한 곳에만 두면 x/y 중 한쪽만 빠뜨리는 실수가 안 생긴다.
    const ZoneDef def_;

    Network::SessionHolder& worldLink_;

    UnitContainer unitContainer_;

    std::mutex crossingsMutex_;
    std::vector<ZoneCrossing> crossings_;
};
