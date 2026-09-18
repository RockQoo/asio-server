#pragma once

#include "Unit/MoveModel.h"

#include "Server/Common/Src/Ids.h"
#include "Server/Core/Src/Base/Types.h"

namespace Network
{
    class SessionHolder;
}

class Zone;

class ZoneUnitOfWork;

// 한 틱 동안 모든 유닛에게 같이 넘어가는 값. 유닛마다 다른 것은 여기 넣지 않는다.
struct UnitTickContext final
{
    int64_t nowUt{};
    float deltaSeconds{};

    // 경계 판정에 쓴다. 넘어간 유닛은 여기에 스스로를 신고한다.
    Zone* zone{};
};

// 존 안에서 자리를 갖고 틱을 받는 것 하나. 플레이어와 (앞으로) 몬스터의 공통 부분이다.
//
// **한 컨테이너에 섞여 들어가므로 unitId 공간이 하나다.** 플레이어는 자기 playerId 와 같은
// 값을 쓰고, 몬스터가 붙으면 겹치지 않게 따로 발급한다.
//
// **스레드 규약**
//   move_        BASIC(검증·요청 기록) + TICK(적분) -- 그래서 Mutexed 다
//   zoneId_      BASIC 전용. 입장/퇴장이 전부 BASIC 이라 쓰는 쪽도 읽는 쪽도 한 레인이다
//   unitId_/type_  생성 후 불변이라 어느 레인에서 읽어도 안전하다
class Unit
{
public:
    using SPtr = std::shared_ptr<Unit>;

    enum class EType : uint8_t
    {
        Player,
        Monster,
    };

    Unit(const Common::UnitId unitId, const EType type, const Common::ZoneId zoneId,
         const float x, const float y);

    // 컨테이너가 기반 포인터로 들고 있다가 지운다 -- 가상 소멸자가 없으면 파생의 모델들이
    // 소멸하지 않는다.
    virtual ~Unit() = default;

    Unit(const Unit&) = delete;
    Unit& operator=(const Unit&) = delete;

    [[nodiscard]] Common::UnitId GetUnitId() const noexcept { return unitId_; }
    [[nodiscard]] EType GetType() const noexcept { return type_; }
    [[nodiscard]] bool IsPlayer() const noexcept { return type_ == EType::Player; }

    [[nodiscard]] Common::ZoneId GetZoneId() const noexcept { return zoneId_; }
    void SetZoneId(const Common::ZoneId zoneId) noexcept { zoneId_ = zoneId; }

    [[nodiscard]] MoveModel::Mutexed& Move() noexcept { return move_; }

    // **TICK 레인에서, 다른 유닛들과 동시에 불린다**(UnitContainer::Tick 이 병렬로 돈다).
    // 그래서 이 안에서 남의 유닛을 만지면 안 된다 -- 자기 것만 만진다.
    virtual void Tick(const UnitTickContext& context);

    // ZoneUnitOfWork 가 실패했을 때 부른다. **무엇을 어떻게 되돌릴지는 자기만 안다** --
    // UoW 는 모델 목록을 들고 있지 않고 이 한 줄로 되돌린다. 기본은 "되돌릴 것이 없다".
    virtual void RollbackUoW(const ZoneUnitOfWork& unitOfWork) noexcept;

    // 변경 결과를 돌려줄 클라이언트. 주인이 없는 유닛(몬스터)은 무효 id다.
    [[nodiscard]] virtual Network::SessionId GetClientSessionId() const noexcept;

    // 변경을 World 로 올릴 링크. 존마다 연결이 하나라 그 존에 속한 유닛은 같은 것을 돌려준다.
    // 올릴 곳이 없는 유닛은 nullptr 다.
    [[nodiscard]] virtual Network::SessionHolder* GetWorldLink() const noexcept;

    // 존 이동을 요청해두고 아직 응답을 못 받은 상태. **중복 요청을 막는 자물쇠다** --
    // 존 이동은 존A -> World -> 존B 로 돌아오는 여러 홉이라, 그 사이에도 틱은 계속 돌고
    // 경계 밖 좌표도 그대로 남아 있어서 매 틱 다시 요청하게 된다.
    //
    // TICK 이 세우고(CAS), 퇴장 처리(BASIC)가 내린다 -- 레인이 둘이라 원자적이어야 한다.
    [[nodiscard]] bool TryBeginZoneCrossing() noexcept
    {
        bool expected = false;
        return crossingZone_.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
    }

    void EndZoneCrossing() noexcept { crossingZone_.store(false, std::memory_order_release); }

    [[nodiscard]] bool IsCrossingZone() const noexcept
    {
        return crossingZone_.load(std::memory_order_acquire);
    }

private:
    const Common::UnitId unitId_;
    const EType type_;
    Common::ZoneId zoneId_;

    MoveModel::Mutexed move_;

    std::atomic<bool> crossingZone_{false};
};
