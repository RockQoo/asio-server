#pragma once

#include "Server/Core/Src/Thread/Mutexed.h"

// 플레이어 한 명의 위치 + 아직 반영되지 않은 이동 요청.
//
// **두 레인이 함께 보는 유일한 플레이어 모델이라 Mutexed다.**
//   플레이어 레인 -- 이동 패킷 검증 + 요청 기록(속도 클램프에 직전 위치가 필요하다)
//   존 레인       -- 틱마다 요청을 실제 위치로 적분하고 경계를 판정
//
// 위치를 존 레인 전용으로 몰면 락은 없앨 수 있지만 속도 클램프가 틱으로 들어가고,
// 이동이 폭주할 때 그게 곧 틱 지연이 된다. 검증은 플레이어 레인이 흡수하는 쪽이 낫다.
//
// 설계 근거: docs/design/player-and-models.md
class MoveModel
{
public:
    using Mutexed = Thread::Mutexed<MoveModel>;

    MoveModel() = default;

    MoveModel(const float x, const float y)
        : x_(x)
        , y_(y)
        , requestedX_(x)
        , requestedY_(y)
    {
    }

    [[nodiscard]] float GetX() const noexcept { return x_; }
    [[nodiscard]] float GetY() const noexcept { return y_; }

    // 플레이어 레인이 검증을 마친 뒤 부른다. 실제 위치는 아직 바꾸지 않는다 --
    // 적분은 존 레인이 다음 틱에 한다.
    void RequestMove(const float x, const float y) noexcept
    {
        requestedX_ = x;
        requestedY_ = y;
        hasRequest_ = true;
    }

    [[nodiscard]] bool HasRequest() const noexcept { return hasRequest_; }
    [[nodiscard]] float GetRequestedX() const noexcept { return requestedX_; }
    [[nodiscard]] float GetRequestedY() const noexcept { return requestedY_; }

    // 존 레인(틱)이 부른다. 요청을 위치로 확정하고 요청 플래그를 내린다.
    void ApplyRequest() noexcept
    {
        x_ = requestedX_;
        y_ = requestedY_;
        hasRequest_ = false;
    }

    // 존 경계 밖으로 나가려던 요청을 취소한다(World가 되돌려 보낼 때 등).
    void CancelRequest() noexcept
    {
        requestedX_ = x_;
        requestedY_ = y_;
        hasRequest_ = false;
    }

    // 핸드오프로 다른 존에 들어갈 때처럼 위치를 통째로 덮어쓴다.
    void Teleport(const float x, const float y) noexcept
    {
        x_ = x;
        y_ = y;
        CancelRequest();
    }

private:
    float x_{};
    float y_{};
    float requestedX_{};
    float requestedY_{};
    bool hasRequest_{false};
};
