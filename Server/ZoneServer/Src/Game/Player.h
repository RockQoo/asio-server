#pragma once

#include "Server/ZoneServer/Src/Currency/CurrencyModel.h"
#include "Server/ZoneServer/Src/Mail/MailModel.h"

#include "Shared/Core/Src/Common/Types.h"

#include <cstdint>
#include <memory>
#include <utility>

namespace Zone
{
    // 권위 있는(authoritative) 플레이어 한 명의 상태 + 그 플레이어가 가진 콘텐츠 모델들.
    // 그 플레이어의 존을 담당하는 BASIC 스레드에서만 건드려야 하며, I/O 스레드에서 직접
    // 접근해서는 안 된다.
    //
    // 모델을 여기 모아두는 이유: 콘텐츠마다 "세션 -> 모델" 맵을 따로 두면 콘텐츠가 늘 때마다
    // 맵이 하나씩 늘고 조회도 그만큼 늘어난다. 플레이어를 한 번 찾으면 그 사람의 모델이 전부
    // 손에 들어오는 쪽이 낫다.
    //
    // 우편함만 Mutexed 핸들로 들고 있는 게 눈에 띌 수 있는데, **우편 만료 스윕이 BASIC이 아닌
    // 별도 유지보수 스레드에서 돌기 때문**이다(Mail::MailExpiryService). 재화처럼 BASIC 전용
    // 모델은 락이 필요 없어서 값으로 직접 들고 있는다 -- "전부 락"도 "전부 무락"도 아니고
    // 모델마다 실제 접근 스레드 수에 맞춘다.
    class Player
    {
    public:
        Player() = default;

        // mailBox는 sink 매개변수라 const를 붙이지 않는다(본문에서 멤버로 move한다).
        Player(const Network::SessionId sessionId, const uint32_t playerId, const float x, const float y,
               std::shared_ptr<Mail::MailModel::Mutexed> mailBox)
            : sessionId_(sessionId)
            , playerId_(playerId)
            , x_(x)
            , y_(y)
            , mailBox_(std::move(mailBox))
        {
        }

        [[nodiscard]] Network::SessionId GetSessionId() const noexcept { return sessionId_; }
        [[nodiscard]] uint32_t GetPlayerId() const noexcept { return playerId_; }
        [[nodiscard]] float GetX() const noexcept { return x_; }
        [[nodiscard]] float GetY() const noexcept { return y_; }

        void SetPosition(const float x, const float y) noexcept
        {
            x_ = x;
            y_ = y;
        }

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
        Network::SessionId sessionId_{};
        uint32_t playerId_{};
        float x_{};
        float y_{};

        std::shared_ptr<Mail::MailModel::Mutexed> mailBox_;
        Currency::CurrencyModel wallet_;
    };
}
