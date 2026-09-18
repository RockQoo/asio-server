#pragma once

#include "Player/CurrencyModel.h"
#include "Player/MailModel.h"
#include "Unit/Unit.h"

#include "Server/Core/Src/Network/SessionHolder.h"
#include "Server/Common/Src/PacketId.h"

class Zone;
class ZoneUnitOfWork;

// 권위 있는(authoritative) 플레이어 한 명. `Unit` 에 **주인이 있는 것**(클라이언트 연결,
// 그 사람만의 콘텐츠 모델)을 더한 것이 Player 다.
//
// **unitId 가 곧 playerId 다.** 존에 들어와 있다는 건 로그인을 통과했다는 뜻이라 항상 있다.
//
// **아래 세 줄이 곧 접근 규칙이다** -- 락이 없는 모델(wallet_)을 다른 레인에서 건드리면
// 그 순간 조용히 깨진다. 새 모델을 붙일 때는 여기에 한 줄을 먼저 적을 것.
//
//   wallet_   -- 값 그대로.  BASIC 레인(owner = playerId) 전용
//   mailBox_  -- Mutexed.    BASIC 레인(요청 처리) + TICK 레인(만료 삭제)
//   move_     -- Mutexed.    BASIC 레인(검증·요청 기록) + TICK 레인(적분·경계 판정)
//
// 설계 근거: docs/design/player-and-models.md
class Player final : public Unit
{
public:
    using SPtr = std::shared_ptr<Player>;

    // **모델의 시작 상태는 전부 여기로 들어온다.** 값은 W2ZEnterZone 이 실어 온 것이고
    // (Packet/WorldPackets.h), 각 모델이 자기 생성자에서 그것만 받는다. 빈 모델을 만들어
    // 두고 나중에 채우는 경로는 두지 않는다(그 사이에 읽으면 없는 것으로 보인다).
    Player(Network::SessionHolder& worldLink, const Network::SessionId clientSessionId,
           const Common::PlayerId playerId, const Common::ZoneId zoneId,
           const float x, const float y,
           std::shared_ptr<MailModel::Mutexed> mailBox,
           const std::vector<Common::CurrencyInfo>& currencies);

    [[nodiscard]] Common::PlayerId GetPlayerId() const noexcept
    {
        // 값을 두 군데 들고 있지 않는다 -- 갈리면 DB 키와 레인 주인이 어긋난다.
        return Common::PlayerId{GetUnitId().Value()};
    }

    [[nodiscard]] Network::SessionId GetClientSessionId() const noexcept override
    {
        return clientSessionId_;
    }

    [[nodiscard]] Network::SessionHolder* GetWorldLink() const noexcept override
    {
        return &worldLink_;
    }

    [[nodiscard]] const std::shared_ptr<MailModel::Mutexed>& GetMailBox() const noexcept
    {
        return mailBox_;
    }

    // 가변 참조라 const 를 못 붙인다(cpp-patterns.md "Get 계열은 const 필수"의 예외 항목).
    [[nodiscard]] CurrencyModel& GetWallet() noexcept { return wallet_; }
    [[nodiscard]] const CurrencyModel& GetWallet() const noexcept { return wallet_; }

    // **패킷은 존이 여기로 내려보낸다**(Zone::Handle). 자기 것을 처리하고, 세계를 만지는
    // 일(브로드캐스트, 전송)은 zone 으로 되돌려 올린다 -- 그래서 zone 을 인자로 받는다.
    void Handle(Zone& zone, const PacketId packetId, const std::span<const byte> payload);

    // **TICK 레인에서 다른 플레이어와 동시에 불린다.** 자기 것만 만진다.
    void Tick(const UnitTickContext& context) override;

    // **여기가 역연산을 아는 유일한 자리다.** UoW 는 모델 목록을 들고 있지 않고, 되돌리는
    // 방법은 그 모델을 가진 이 클래스만 안다.
    void RollbackUoW(const ZoneUnitOfWork& unitOfWork) noexcept override;

private:
    // 만료된 우편을 지운다. 지울 것이 있을 때만 불린다.
    void TickMail(const UnitTickContext& context, ZoneUnitOfWork& unitOfWork);

    // 존마다 World 연결이 하나라, 같은 존의 플레이어는 모두 같은 링크를 가리킨다.
    Network::SessionHolder& worldLink_;

    const Network::SessionId clientSessionId_;

    std::shared_ptr<MailModel::Mutexed> mailBox_;
    CurrencyModel wallet_;
};
