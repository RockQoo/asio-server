#include "pch.h"
#include "World/PlayerManager.h"

namespace World
{
    void PlayerManager::Add(const Network::SessionId clientSessionId,
                            const Network::Session::SPtr& gatewaySession)
    {
        PlayerInfo info{};
        info.gatewaySession = gatewaySession;
        players_[clientSessionId] = std::move(info);
    }

    void PlayerManager::Remove(const Network::SessionId clientSessionId)
    {
        players_.erase(clientSessionId);
    }

    void PlayerManager::SetZone(const Network::SessionId clientSessionId, const Common::ZoneId zoneId)
    {
        if (const auto it = players_.find(clientSessionId); it != players_.end())
        {
            it->second.zoneId = zoneId;
        }
    }

    void PlayerManager::SetAuthenticated(const Network::SessionId clientSessionId, const Common::PlayerId playerId,
                                         std::string playerName,
                                         std::unordered_map<Common::MailId, Common::MailInfo> mails,
                                         std::unordered_map<Common::ECurrencyType, int64_t> currencies)
    {
        const auto it = players_.find(clientSessionId);
        if (it == players_.end())
        {
            return;
        }

        it->second.playerId = playerId;
        it->second.playerName = std::move(playerName);
        it->second.mails = std::move(mails);
        it->second.currencies = std::move(currencies);

        // **마지막에 세운다.** 이 플래그가 곧 "게임 패킷을 존으로 흘려도 된다"는 신호라,
        // 캐시가 다 채워지기 전에 켜지면 안 된다.
        it->second.authenticated = true;
    }

    void PlayerManager::AddMail(const Network::SessionId clientSessionId, Common::MailInfo mailInfo)
    {
        const auto it = players_.find(clientSessionId);
        if (it == players_.end())
        {
            return;
        }

        const auto mailId = mailInfo.mailId;
        it->second.mails.insert_or_assign(mailId, std::move(mailInfo));
    }

    void PlayerManager::RemoveMail(const Network::SessionId clientSessionId, const Common::MailId mailId)
    {
        if (const auto it = players_.find(clientSessionId); it != players_.end())
        {
            it->second.mails.erase(mailId);
        }
    }

    void PlayerManager::SetCurrency(const Network::SessionId clientSessionId, const Common::ECurrencyType currencyType,
                                    const int64_t amount)
    {
        if (const auto it = players_.find(clientSessionId); it != players_.end())
        {
            it->second.currencies.insert_or_assign(currencyType, amount);
        }
    }

    std::optional<Common::PlayerId> PlayerManager::FindPlayerId(const Network::SessionId clientSessionId) const
    {
        const auto it = players_.find(clientSessionId);
        if (it == players_.end())
        {
            return std::nullopt;
        }
        return it->second.playerId;
    }

    std::optional<PlayerInfo> PlayerManager::Find(const Network::SessionId clientSessionId) const
    {
        const auto it = players_.find(clientSessionId);
        if (it == players_.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<PlayerManager::Route> PlayerManager::FindRoute(const Network::SessionId clientSessionId) const
    {
        const auto it = players_.find(clientSessionId);
        if (it == players_.end())
        {
            return std::nullopt;
        }

        return Route{it->second.gatewaySession, it->second.zoneId, it->second.authenticated};
    }

    void PlayerManager::ForEach(const std::function<void(const Network::SessionId, const PlayerInfo&)>& func) const
    {
        for (const auto& [clientSessionId, info] : players_)
        {
            func(clientSessionId, info);
        }
    }
}
