#pragma once

#include "Server/ZoneServer/Src/Currency/CurrencyModel.h"
#include "Server/ZoneServer/Src/Game/MoveModel.h"
#include "Server/ZoneServer/Src/Mail/MailModel.h"

#include "Shared/Core/Src/Common/Types.h"

#include <cstdint>
#include <memory>
#include <utility>

namespace Zone
{
    // 권위 있는(authoritative) 플레이어 한 명 + 그 사람의 콘텐츠 모델들.
    //
    // **모델마다 보호 방식이 다르고, 그 기준은 "실제로 몇 개의 레인이 건드리는가"다.**
    //
    //   wallet_   -- 값 그대로.  플레이어 레인(owner = clientSessionId) 전용
    //   mailBox_  -- Mutexed.    플레이어 레인 + 만료 스윕(별도 유지보수 타이머)
    //   move_     -- Mutexed.    플레이어 레인(검증·요청 기록) + 존 레인(틱 적분·경계 판정)
    //
    // 이 객체 자체는 **두 레인이 shared_ptr로 함께 들고 있다** -- 플레이어 레인은
    // PlayerRegistry(샤딩된 맵)로, 존 레인은 ZoneInstance의 로스터로 도달한다. 객체를 둘로
    // 쪼개지 않은 이유는, 쪼개면 "이 사람의 것"이 두 군데로 흩어져 콘텐츠가 늘 때마다
    // 어느 쪽에 넣을지를 매번 다시 정해야 하기 때문이다. 대신 **모델 단위로 소유 레인을
    // 명시**한다.
    //
    // 위 세 줄이 곧 접근 규칙이다: 락이 없는 모델(wallet_)을 다른 레인에서 건드리기
    // 시작하면 그 순간 조용히 깨진다. 새 모델을 붙일 때는 여기에 한 줄을 먼저 적을 것.
    class Player
    {
    public:
        Player(const Network::SessionId sessionId, const uint32_t playerId, const uint32_t zoneId,
               const float x, const float y,
               std::shared_ptr<Mail::MailModel::Mutexed> mailBox)
            : sessionId_(sessionId)
            , playerId_(playerId)
            , zoneId_(zoneId)
            , move_(x, y)
            , mailBox_(std::move(mailBox))
        {
        }

        Player(const Player&) = delete;
        Player& operator=(const Player&) = delete;

        // 생성 후 불변이라 어느 레인에서 읽어도 안전하다.
        [[nodiscard]] Network::SessionId GetSessionId() const noexcept { return sessionId_; }
        [[nodiscard]] uint32_t GetPlayerId() const noexcept { return playerId_; }

        // 지금 있는 존. **플레이어 레인 전용**이라 락이 없다 -- 핸드오프도 결국 이 레인으로
        // 오는 입장/퇴장 메시지로 처리되므로, 쓰는 쪽도 읽는 쪽도 항상 이 사람의 스레드다.
        // 이 값을 여기 둔 덕에 예전의 "clientSessionId -> zoneId" 공유 맵(+ shared_mutex)이
        // 통째로 없어졌다.
        [[nodiscard]] uint32_t GetZoneId() const noexcept { return zoneId_; }
        void SetZoneId(const uint32_t zoneId) noexcept { zoneId_ = zoneId; }

        // 플레이어 레인 + 존 레인 공용. `move_->GetX()`(읽기) / `move_.Write()->...`(쓰기).
        [[nodiscard]] MoveModel::Mutexed& Move() noexcept { return move_; }

        // 우편함이 없을 수 있다(입장 처리가 누락된 경우) -- 호출부가 null을 확인하고
        // MailBoxNotFound로 끊는다.
        [[nodiscard]] const std::shared_ptr<Mail::MailModel::Mutexed>& GetMailBox() const noexcept
        {
            return mailBox_;
        }

        // 가변 참조를 반환하는 접근자라 const를 붙일 수 없다(cpp-patterns.md "Get 계열은 const
        // 필수"의 예외 항목). 호출부가 재화를 실제로 바꿔야 하기 때문이다.
        [[nodiscard]] Currency::CurrencyModel& GetWallet() noexcept { return wallet_; }
        [[nodiscard]] const Currency::CurrencyModel& GetWallet() const noexcept { return wallet_; }

    private:
        const Network::SessionId sessionId_;
        const uint32_t playerId_;
        uint32_t zoneId_;

        MoveModel::Mutexed move_;
        std::shared_ptr<Mail::MailModel::Mutexed> mailBox_;
        Currency::CurrencyModel wallet_;
    };
}
