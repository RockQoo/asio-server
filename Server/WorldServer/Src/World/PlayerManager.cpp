#include "pch.h"
#include "World/PlayerManager.h"

void PlayerManager::Add(const Network::SessionId clientSessionId,
                        const Network::Session::SPtr& gatewaySession)
{
    PlayerInfo info{};
    info.gatewaySession = gatewaySession;
    players_[clientSessionId] = std::move(info);
}

void PlayerManager::Remove(const Network::SessionId clientSessionId)
{
    const auto it = players_.find(clientSessionId);
    if (it == players_.end())
    {
        return;
    }

    // 두 색인을 같이 지운다. 한쪽만 지우면 그 playerId 로 들어온 변경이 이미 사라진
    // 세션을 가리키게 된다.
    if (it->second.playerId.IsValid())
    {
        sessionByPlayerId_.erase(it->second.playerId);
    }

    players_.erase(it);
}

void PlayerManager::SetZoneId(const Network::SessionId clientSessionId, const Common::ZoneId zoneId)
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

    sessionByPlayerId_[playerId] = clientSessionId;

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

std::optional<Network::SessionId> PlayerManager::FindSessionByPlayerId(const Common::PlayerId playerId) const
{
    const auto it = sessionByPlayerId_.find(playerId);
    if (it == sessionByPlayerId_.end())
    {
        return std::nullopt;
    }
    return it->second;
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
