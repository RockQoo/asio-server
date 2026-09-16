#pragma once

#include "Currency/Model.h"
#include "Game/MoveModel.h"
#include "Mail/Model.h"

#include "Shared/Core/Src/Base/Types.h"

namespace Zone
{
    // 권위 있는(authoritative) 플레이어 한 명 + 그 사람의 콘텐츠 모델들.
    //
    // **아래 세 줄이 곧 접근 규칙이다** -- 락이 없는 모델(wallet_)을 다른 레인에서 건드리면
    // 그 순간 조용히 깨진다. 새 모델을 붙일 때는 여기에 한 줄을 먼저 적을 것.
    //
    //   wallet_   -- 값 그대로.  플레이어 레인(owner = clientSessionId) 전용
    //   mailBox_  -- Mutexed.    플레이어 레인 + 만료 스윕(별도 유지보수 타이머)
    //   move_     -- Mutexed.    플레이어 레인(검증·요청 기록) + 존 레인(틱 적분·경계 판정)
    //
    // 설계 근거: docs/design/player-and-models.md
    class Player
    {
    public:
        // **모델의 시작 상태는 전부 여기로 들어온다.** 값은 W2ZEnterZone이 실어 온 것이고
        // (Packet/WorldPackets.h), 각 모델이 자기 생성자에서 그것만 받는다.
        // 콘텐츠가 늘면 인자가 하나 늘고 멤버가 하나 는다 -- 빈 모델을 만들어 두고 나중에
        // 채우는 경로는 두지 않는다(그 사이에 읽으면 없는 것으로 보인다).
        Player(const Network::SessionId sessionId, const Common::PlayerId playerId, const Common::ZoneId zoneId,
               const float x, const float y,
               std::shared_ptr<Mail::Model::Mutexed> mailBox,
               const std::vector<Common::CurrencyInfo>& currencies)
            : sessionId_(sessionId)
            , playerId_(playerId)
            , zoneId_(zoneId)
            , move_(x, y)
            , mailBox_(std::move(mailBox))
            , wallet_(currencies)
        {
        }

        Player(const Player&) = delete;
        Player& operator=(const Player&) = delete;

        // 생성 후 불변이라 어느 레인에서 읽어도 안전하다.
        [[nodiscard]] Network::SessionId GetSessionId() const noexcept { return sessionId_; }
        [[nodiscard]] Common::PlayerId GetPlayerId() const noexcept { return playerId_; }

        // 지금 있는 존. **플레이어 레인 전용**이라 락이 없다 -- 핸드오프도 결국 이 레인으로
        // 오는 입장/퇴장 메시지로 처리되므로, 쓰는 쪽도 읽는 쪽도 항상 이 사람의 스레드다.
        [[nodiscard]] Common::ZoneId GetZoneId() const noexcept { return zoneId_; }
        void SetZoneId(const Common::ZoneId zoneId) noexcept { zoneId_ = zoneId; }

        // 플레이어 레인 + 존 레인 공용. `move_->GetX()`(읽기) / `move_.Write()->...`(쓰기).
        [[nodiscard]] MoveModel::Mutexed& Move() noexcept { return move_; }

        // 우편함이 없을 수 있다(입장 처리가 누락된 경우) -- 호출부가 null을 확인하고
        // MailBoxNotFound로 끊는다.
        [[nodiscard]] const std::shared_ptr<Mail::Model::Mutexed>& GetMailBox() const noexcept
        {
            return mailBox_;
        }

        // 가변 참조라 const를 못 붙인다(cpp-patterns.md "Get 계열은 const 필수"의 예외 항목).
        [[nodiscard]] Currency::Model& GetWallet() noexcept { return wallet_; }
        [[nodiscard]] const Currency::Model& GetWallet() const noexcept { return wallet_; }

    private:
        const Network::SessionId sessionId_;
        const Common::PlayerId playerId_;
        Common::ZoneId zoneId_;

        MoveModel::Mutexed move_;
        std::shared_ptr<Mail::Model::Mutexed> mailBox_;
        Currency::Model wallet_;
    };
}
