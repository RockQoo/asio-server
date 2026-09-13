#include "pch.h"
#include "Login/LoginProcessor.h"

#include "Db/AutoDbCommand.h"
#include "Db/DbConnection.h"
#include "Db/PasswordHash.h"
#include "Packet/RelayEnvelope.h"
#include "Packet/ZoneLinkPackets.h"
#include "World/PlayerManager.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryReader.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"

namespace World
{
    LoginProcessor::LoginProcessor(PlayerManager::Mutexed& playerManager, ZoneLinkRegistry::Mutexed& zoneLinkRegistry,
                                   Processor::Group<EProcessorId>& basicGroup,
                                   Processor::Group<EProcessorId>& dbGroup,
                                   DbConnectionPool& dbPool)
        : playerManager_(playerManager)
        , zoneLinkRegistry_(zoneLinkRegistry)
        , basicGroup_(basicGroup)
        , dbGroup_(dbGroup)
        , dbPool_(dbPool)
    {
    }

    void LoginProcessor::HandleClientPacket(const std::shared_ptr<Network::Session>& gatewaySession,
                                            const Network::SessionId clientSessionId, const PacketId packetId,
                                            const std::span<const byte> payload)
    {
        switch (packetId)
        {
        case PacketId::C2WLogin:
            HandleLogin(gatewaySession, clientSessionId, payload);
            break;

        default:
            // 이 빌드가 모르는 C2W id. 조작이거나 클라이언트가 앞서 나간 것이라 버리기만 한다.
            LOG.Warning(ELogCategory::Gateway, "처리하지 않는 C2W 패킷")
                .KV("ClientSessionId", clientSessionId).KV("PacketId", static_cast<uint16_t>(packetId));
            break;
        }
    }

    void LoginProcessor::HandleLogin(const std::shared_ptr<Network::Session>& gatewaySession,
                                     const Network::SessionId clientSessionId, const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        std::string playerName;
        std::string password;
        if (!reader.ReadString(playerName) || !reader.ReadString(password))
        {
            SendResult(gatewaySession, clientSessionId, EErrorCode::InvalidPayload, 0, {});
            return;
        }

        if (playerName.empty() || playerName.size() > kMaxPlayerNameBytes
            || password.empty() || password.size() > kMaxPasswordBytes)
        {
            SendResult(gatewaySession, clientSessionId, EErrorCode::LoginInvalidInput, 0, {});
            return;
        }

        const auto client = playerManager_->Find(clientSessionId);
        if (!client)
        {
            // G2WClientConnected보다 먼저 도착할 수는 없다 -- 둘 다 owner가 clientSessionId라
            // 같은 레인에서 순서대로 처리된다. 그래도 찍히면 그 전제가 깨진 것이니 남긴다.
            LOG.Warning(ELogCategory::Gateway, "등록되지 않은 세션의 로그인 시도")
                .KV("ClientSessionId", clientSessionId);
            return;
        }

        if (client->authenticated)
        {
            SendResult(gatewaySession, clientSessionId, EErrorCode::LoginAlreadyAuthenticated, 0, {});
            return;
        }

        // 스코프를 벗어나는 순간 DB 그룹으로 나간다(AutoDbCommand는 소멸자에서 Post한다).
        AutoDbCommand select(dbPool_, dbGroup_, std::hash<std::string>{}(playerName), false,
            [this, gatewaySession, clientSessionId, playerName, password]
            (const bool succeeded, const DbResult& result)
            {
                OnAccountSelected(gatewaySession, clientSessionId, playerName, password, succeeded, result);
            });

        select.Add(DbCommand{"dbo.usp_players_select", {playerName}});
    }

    void LoginProcessor::OnAccountSelected(const std::shared_ptr<Network::Session>& gatewaySession,
                                           const Network::SessionId clientSessionId, const std::string& playerName,
                                           const std::string& password, const bool succeeded, const DbResult& result)
    {
        if (!succeeded)
        {
            PostFailure(gatewaySession, clientSessionId, EErrorCode::LoginDbFailure);
            return;
        }

        // 계정이 있다 -- 비밀번호를 **서버에서** 비교한다. SP에서 하면 평문이 와이어와 DB 로그에
        // 남는다(Sql/players.sql의 usp_players_select 주석).
        if (!result.empty())
        {
            const auto playerId = GetInt64(result[0], 0);
            const auto storedHash = GetString(result[0], 2);
            if (!playerId || !storedHash)
            {
                LOG.Error(ELogCategory::Db, "usp_players_select의 결과 컬럼 형태가 예상과 다르다")
                    .KV("PlayerName", playerName);
                PostFailure(gatewaySession, clientSessionId, EErrorCode::LoginDbFailure);
                return;
            }

            if (!VerifyPassword(password, *storedHash))
            {
                PostFailure(gatewaySession, clientSessionId, EErrorCode::LoginWrongPassword);
                return;
            }

            PostSuccess(gatewaySession, clientSessionId, playerName, *playerId);
            return;
        }

        // 계정이 없다 -> **자동 가입**. 실패가 아니라 정상 경로다(Sql/players.sql).
        const auto storedHash = HashPassword(password);
        if (storedHash.empty())
        {
            // 빈 해시를 넣으면 그 계정은 영원히 로그인할 수 없게 된다 -- 만들지 않고 끊는다.
            LOG.Error(ELogCategory::Db, "비밀번호 해시 생성 실패, 자동 가입 중단").KV("PlayerName", playerName);
            PostFailure(gatewaySession, clientSessionId, EErrorCode::LoginDbFailure);
            return;
        }

        // playerId는 DB의 IDENTITY가 아니라 여기서 발급한다(cpp-patterns.md의 RUID 절).
        // 같은 이름으로 동시에 첫 로그인이 들어오면 한쪽만 INSERT되고, 그때 실제로 확정된 값은
        // SP가 돌려주므로 이 값은 "제안"일 뿐이다.
        const auto requestedPlayerId = Common::RUIDGenerator::Instance().Next();

        AutoDbCommand upsert(dbPool_, dbGroup_, std::hash<std::string>{}(playerName), true,
            [this, gatewaySession, clientSessionId, playerName, requestedPlayerId]
            (const bool upsertSucceeded, const DbResult& upsertResult)
            {
                OnAccountCreated(gatewaySession, clientSessionId, playerName, requestedPlayerId,
                                 upsertSucceeded, upsertResult);
            });

        upsert.Add(DbCommand{"dbo.usp_players_upsert", {requestedPlayerId, playerName, storedHash}});
    }

    void LoginProcessor::OnAccountCreated(const std::shared_ptr<Network::Session>& gatewaySession,
                                           const Network::SessionId clientSessionId, const std::string& playerName,
                                           const Common::RUID requestedPlayerId, const bool succeeded,
                                           const DbResult& result)
    {
        if (!succeeded)
        {
            PostFailure(gatewaySession, clientSessionId, EErrorCode::LoginDbFailure);
            return;
        }

        // **SP가 돌려준 값을 쓴다.** 동시 첫 로그인에서 진 쪽은 자기가 발급한 id가 들어가지
        // 않았으므로, 제안값을 그대로 쓰면 존재하지 않는 playerId로 게임을 시작하게 된다.
        const auto confirmedPlayerId = result.empty() ? std::nullopt : GetInt64(result[0], 0);
        if (!confirmedPlayerId)
        {
            LOG.Error(ELogCategory::Db, "usp_players_upsert가 player_id를 돌려주지 않았다")
                .KV("PlayerName", playerName);
            PostFailure(gatewaySession, clientSessionId, EErrorCode::LoginDbFailure);
            return;
        }

        LOG.Info(ELogCategory::Db, "자동 가입 완료")
            .KV("PlayerName", playerName).KV("PlayerId", *confirmedPlayerId)
            .KV("RequestedPlayerId", requestedPlayerId);

        PostSuccess(gatewaySession, clientSessionId, playerName, *confirmedPlayerId);
    }

    void LoginProcessor::PostFailure(const std::shared_ptr<Network::Session>& gatewaySession,
                                      const Network::SessionId clientSessionId, const EErrorCode errorCode)
    {
        // 전송뿐이라 BASIC을 거치지 않아도 되지만, 로그인의 모든 결말이 같은 레인에서 나가야
        // 클라이언트가 받는 순서가 흔들리지 않는다(성공 경로는 존 입장 때문에 반드시 BASIC이다).
        basicGroup_.Post(EProcessorId::Login, clientSessionId,
            [this, gatewaySession, clientSessionId, errorCode]
            {
                SendResult(gatewaySession, clientSessionId, errorCode, 0, {});
            });
    }

    void LoginProcessor::PostSuccess(const std::shared_ptr<Network::Session>& gatewaySession,
                                      const Network::SessionId clientSessionId, const std::string& playerName,
                                      const Common::RUID playerId)
    {
        basicGroup_.Post(EProcessorId::Login, clientSessionId,
            [this, gatewaySession, clientSessionId, playerName, playerId]
            {
                CompleteLogin(gatewaySession, clientSessionId, playerName, playerId);
            });
    }

    void LoginProcessor::CompleteLogin(const std::shared_ptr<Network::Session>& gatewaySession,
                                        const Network::SessionId clientSessionId, const std::string& playerName,
                                        const Common::RUID playerId)
    {
        // DB를 다녀오는 동안 접속이 끊겼을 수 있다. 그러면 등록이 이미 지워져 있다.
        if (!playerManager_->Find(clientSessionId))
        {
            LOG.Info(ELogCategory::Gateway, "로그인 처리 중 접속이 끊겼다").KV("ClientSessionId", clientSessionId);
            return;
        }

        // 입장할 존을 **먼저** 확보한다. 존이 없는데 인증만 세워두면, 그 세션은 인증은 됐지만
        // 어느 존에도 없는 상태로 남아 이후 패킷이 조용히 버려진다.
        const auto entry = zoneLinkRegistry_->FindEntryPoint();
        const auto zoneLink = entry ? zoneLinkRegistry_->Find(entry->zoneId) : std::optional<ZoneLinkInfo>{};
        if (!entry || !zoneLink)
        {
            LOG.Warning(ELogCategory::Zone, "입장시킬 존이 아직 연결되지 않음")
                .KV("ClientSessionId", clientSessionId);
            SendResult(gatewaySession, clientSessionId, EErrorCode::LoginNoZoneAvailable, 0, {});
            return;
        }

        // 콘텐츠 캐시(우편/재화)는 아직 비어 있다 -- usp_players_load로 채우는 단계가 다음이다.
        // 쓰기 락을 두 번 잡지 않도록 한 번에 묶는다.
        {
            auto writer = playerManager_.Write();
            writer->SetAuthenticated(clientSessionId, playerId, playerName, {}, {});
            writer->SetZone(clientSessionId, entry->zoneId);
        }

        // 결과를 먼저 보낸다 -- 클라이언트가 로딩 화면으로 넘어간 뒤에 존 입장 통지를 받는
        // 순서가 되어야 "로그인은 됐는데 화면이 안 넘어간다"가 안 생긴다.
        SendResult(gatewaySession, clientSessionId, EErrorCode::Success, playerId, playerName);

        PlayerZoneStatePacket enterState{};
        enterState.zoneId = entry->zoneId;
        enterState.clientSessionId = clientSessionId;
        // 존 쪽 playerId는 아직 uint32라 clientSessionId에서 파생한다(PlayerInfo::playerId 주석).
        enterState.playerId = static_cast<uint32_t>(clientSessionId);
        enterState.x = entry->x;
        enterState.y = entry->y;
        zoneLink->zoneSession->SendPacket(PacketId::W2ZEnterZone,
                                          std::as_bytes(std::span(&enterState, 1)));

        LOG.Info(ELogCategory::Gateway, "로그인 성공, 입장 존 배정")
            .KV("ClientSessionId", clientSessionId).KV("PlayerName", playerName)
            .KV("PlayerId", playerId).KV("ZoneId", entry->zoneId)
            .KV("X", entry->x).KV("Y", entry->y);
    }

    void LoginProcessor::SendResult(const std::shared_ptr<Network::Session>& gatewaySession,
                                     const Network::SessionId clientSessionId, const EErrorCode errorCode,
                                     const Common::RUID playerId, const std::string_view playerName) const
    {
        ClientEnvelopeHeader header{};
        header.clientSessionId = clientSessionId;
        header.innerPacketId = static_cast<uint16_t>(PacketId::W2CLogin);

        Packet::BinaryWriter writer;
        writer.Write(header);
        writer.Write(static_cast<int32_t>(errorCode));
        writer.Write(playerId);
        writer.WriteString(playerName);

        gatewaySession->SendPacket(PacketId::W2GRelay, writer.GetBuffer());
    }
}
