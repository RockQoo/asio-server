#include "pch.h"
#include "Processor/LoginProcessor.h"

#include "Db/AutoSpCommands.h"
#include "Db/DbConnection.h"
#include "Db/PasswordHash.h"
#include "Packet/EnterZoneBody.h"
#include "Packet/RelayEnvelope.h"
#include "Packet/ZoneLinkPackets.h"
#include "World/PlayerManager.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryReader.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"

namespace World
{
    LoginProcessor::LoginProcessor(PlayerManager::Mutexed& playerManager, ZoneLinkRegistry::Mutexed& zoneLinkRegistry,
                                   Processor::Group<EProcessorId>& basicGroup, DbProcessor& dbProcessor)
        : playerManager_(playerManager)
        , zoneLinkRegistry_(zoneLinkRegistry)
        , basicGroup_(basicGroup)
        , dbProcessor_(dbProcessor)
    {
    }

    void LoginProcessor::HandleClientPacket(const Network::Session::SPtr& gatewaySession,
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

    void LoginProcessor::HandleLogin(const Network::Session::SPtr& gatewaySession,
                                     const Network::SessionId clientSessionId, const std::span<const byte> payload)
    {
        Packet::BinaryReader binaryReader(payload);
        std::string playerName;
        std::string password;
        if (!binaryReader.ReadString(playerName) || !binaryReader.ReadString(password))
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

        // 스코프를 벗어나는 순간 DB 그룹으로 나간다(AutoSpCommands는 소멸자에서 넘긴다).
        //
        // **로그인 경로의 주인은 처음부터 끝까지 clientSessionId다.** 계정을 조회하는 지금은
        // 아직 playerId를 모르고, 알게 된 뒤에도 바꾸지 않는다 -- 중간에 갈아타면 그 지점부터
        // 앞 구간과 직렬화가 끊겨서, 같은 세션의 로그인 단계들이 서로 다른 strand에서 겹친다.
        AutoSpCommands autoSpCommands(dbProcessor_, clientSessionId, false,
            [this, gatewaySession, clientSessionId, playerName, password]
            (const bool succeeded, const DbResult& dbResult)
            {
                OnAccountSelected(gatewaySession, clientSessionId, playerName, password, succeeded, dbResult);
            });

        autoSpCommands.Add(DbCommand{"dbo.usp_players_select", {playerName}});
    }

    void LoginProcessor::OnAccountSelected(const Network::Session::SPtr& gatewaySession,
                                           const Network::SessionId clientSessionId, const std::string& playerName,
                                           const std::string& password, const bool succeeded, const DbResult& dbResult)
    {
        if (!succeeded)
        {
            PostFailure(gatewaySession, clientSessionId, EErrorCode::LoginDbFailure);
            return;
        }

        // 계정이 있다 -- 비밀번호를 **서버에서** 비교한다. SP에서 하면 평문이 와이어와 DB 로그에
        // 남는다(Sql/players.sql의 usp_players_select 주석).
        if (const auto* const row = FirstRow(dbResult); row != nullptr)
        {
            const auto playerId = GetInt64(*row, 0);
            const auto storedHash = GetString(*row, 2);
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

            LoadPlayerContent(gatewaySession, clientSessionId, playerName, *playerId);
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
        const auto requestedPlayerId = Base::Ruid::Create();

        AutoSpCommands autoSpCommands(dbProcessor_, clientSessionId, true,
            [this, gatewaySession, clientSessionId, playerName, requestedPlayerId]
            (const bool upsertSucceeded, const DbResult& upsertResult)
            {
                OnAccountCreated(gatewaySession, clientSessionId, playerName, requestedPlayerId,
                                 upsertSucceeded, upsertResult);
            });

        autoSpCommands.Add(DbCommand{"dbo.usp_players_upsert", {requestedPlayerId, playerName, storedHash}});
    }

    void LoginProcessor::OnAccountCreated(const Network::Session::SPtr& gatewaySession,
                                           const Network::SessionId clientSessionId, const std::string& playerName,
                                           const Base::RUID requestedPlayerId, const bool succeeded,
                                           const DbResult& dbResult)
    {
        if (!succeeded)
        {
            PostFailure(gatewaySession, clientSessionId, EErrorCode::LoginDbFailure);
            return;
        }

        // **SP가 돌려준 값을 쓴다.** 동시 첫 로그인에서 진 쪽은 자기가 발급한 id가 들어가지
        // 않았으므로, 제안값을 그대로 쓰면 존재하지 않는 playerId로 게임을 시작하게 된다.
        const auto* const row = FirstRow(dbResult);
        const auto confirmedPlayerId = row != nullptr ? GetInt64(*row, 0) : std::nullopt;
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

        // 방금 만든 계정이라 우편도 재화도 없다. 그래도 같은 경로를 타게 두는 이유는, 시드로
        // 미리 넣어둔 계정이나 나중에 지급 로직이 생겼을 때 여기서만 예외가 생기지 않게 하려는
        // 것이다 -- "새 계정은 적재를 건너뛴다"가 한 번 들어가면 그 가정이 언젠가 깨진다.
        LoadPlayerContent(gatewaySession, clientSessionId, playerName, *confirmedPlayerId);
    }

    void LoginProcessor::LoadPlayerContent(const Network::Session::SPtr& gatewaySession,
                                            const Network::SessionId clientSessionId,
                                            const std::string& playerName, const Base::RUID playerId)
    {
        // **여기서도 주인은 clientSessionId다.** playerId를 알게 됐다고 갈아타지 않는다 --
        // 앞의 조회/가입과 같은 strand에 남아야 로그인 한 건이 한 줄로 처리된다.
        //
        // 존 입장 뒤의 DB 작업(UnitOfWork)은 반대로 playerId가 주인이다(MainProcessor).
        // 세션이 아니라 계정에 묶이는 일이라 재접속해도 같은 레인을 유지해야 하기 때문이고,
        // 로그인은 그 세션 안에서만 의미가 있어 기준이 다르다.
        AutoSpCommands autoSpCommands(dbProcessor_, clientSessionId, false,
            [this, gatewaySession, clientSessionId, playerName, playerId]
            (const bool succeeded, const DbResult& dbResult)
            {
                OnPlayerLoaded(gatewaySession, clientSessionId, playerName, playerId, succeeded, dbResult);
            });

        autoSpCommands.Add(DbCommand{"dbo.usp_players_load", {playerId}});
    }

    void LoginProcessor::OnPlayerLoaded(const Network::Session::SPtr& gatewaySession,
                                         const Network::SessionId clientSessionId,
                                         const std::string& playerName, const Base::RUID playerId,
                                         const bool succeeded, const DbResult& dbResult)
    {
        if (!succeeded)
        {
            // **적재 실패는 로그인 실패로 끊는다.** 빈 캐시로 들여보내면 그 사람의 우편과 재화가
            // 없는 것으로 보이고, 그 상태에서 뭔가를 쓰면(재화는 절대값 UPDATE다) DB의 실제
            // 값을 덮어써 잔액이 사라진다.
            PostFailure(gatewaySession, clientSessionId, EErrorCode::LoginDbFailure);
            return;
        }

        // 결과 집합 순서는 usp_players_load의 SELECT 순서와 같은 계약이다(그 SP 주석 참고).
        std::unordered_map<Common::MailId, Common::MailInfo> mails;
        for (const auto& row : SetAt(dbResult, 0))
        {
            const auto mailIdValue = GetInt64(row, 0);
            const auto title = GetString(row, 1);
            const auto body = GetString(row, 2);
            const auto sendUt = GetInt64(row, 3);
            const auto endUt = GetInt64(row, 4);
            if (!mailIdValue || !title || !body || !sendUt || !endUt)
            {
                LOG.Error(ELogCategory::Db, "usp_players_load의 우편 컬럼 형태가 예상과 다르다")
                    .KV("PlayerId", playerId);
                PostFailure(gatewaySession, clientSessionId, EErrorCode::LoginDbFailure);
                return;
            }

            // DB에서 읽은 정수를 타입 있는 id로 바꾸는 자리다 -- StrongId 생성자가 explicit인
            // 덕분에 이 경계가 코드에 드러난다(그냥 흘러들어오지 않는다).
            const Common::MailId mailId{*mailIdValue};
            mails.emplace(mailId, Common::MailInfo{mailId, *title, *body, *sendUt, *endUt});
        }

        std::unordered_map<uint8_t, int64_t> currencies;
        for (const auto& row : SetAt(dbResult, 1))
        {
            const auto type = GetInt64(row, 0);
            const auto amount = GetInt64(row, 1);
            if (!type || !amount)
            {
                LOG.Error(ELogCategory::Db, "usp_players_load의 재화 컬럼 형태가 예상과 다르다")
                    .KV("PlayerId", playerId);
                PostFailure(gatewaySession, clientSessionId, EErrorCode::LoginDbFailure);
                return;
            }

            currencies.emplace(static_cast<uint8_t>(*type), *amount);
        }

        LOG.Info(ELogCategory::Db, "플레이어 콘텐츠 적재")
            .KV("PlayerName", playerName).KV("PlayerId", playerId)
            .KV("Mails", mails.size()).KV("Currencies", currencies.size());

        PostSuccess(gatewaySession, clientSessionId, playerName, playerId,
                    std::move(mails), std::move(currencies));
    }

    void LoginProcessor::PostFailure(const Network::Session::SPtr& gatewaySession,
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

    void LoginProcessor::PostSuccess(const Network::Session::SPtr& gatewaySession,
                                      const Network::SessionId clientSessionId, const std::string& playerName,
                                      const Base::RUID playerId,
                                      std::unordered_map<Common::MailId, Common::MailInfo> mails,
                                      std::unordered_map<uint8_t, int64_t> currencies)
    {
        basicGroup_.Post(EProcessorId::Login, clientSessionId,
            [this, gatewaySession, clientSessionId, playerName, playerId,
             mails = std::move(mails), currencies = std::move(currencies)]() mutable
            {
                CompleteLogin(gatewaySession, clientSessionId, playerName, playerId,
                              std::move(mails), std::move(currencies));
            });
    }

    void LoginProcessor::CompleteLogin(const Network::Session::SPtr& gatewaySession,
                                        const Network::SessionId clientSessionId, const std::string& playerName,
                                        const Base::RUID playerId,
                                        std::unordered_map<Common::MailId, Common::MailInfo> mails,
                                        std::unordered_map<uint8_t, int64_t> currencies)
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

        PlayerZoneStatePacket enterState{};
        enterState.zoneId = entry->zoneId;
        enterState.clientSessionId = clientSessionId;
        // **로그인이 확정한 값을 그대로 싣는다.** 예전에는 clientSessionId 를 uint32 로 잘라
        // 넣고 있어서, 존과 클라이언트가 보는 playerId 가 재접속할 때마다 바뀌었다.
        enterState.playerId = Common::PlayerId{playerId};
        enterState.x = entry->x;
        enterState.y = entry->y;

        // **캐시에 넣기 전에 보낼 바이트를 먼저 만든다** -- 아래에서 맵을 move로 넘겨버리므로,
        // 순서를 바꾸면 빈 맵을 실어 보내게 된다.
        const auto enterBody = BuildEnterZoneBody(enterState, mails, currencies);

        // 캐시를 채우고 인증을 확정한다. 쓰기 락을 두 번 잡지 않도록 한 번에 묶는다.
        {
            auto writeProxy = playerManager_.Write();
            writeProxy->SetAuthenticated(clientSessionId, Common::PlayerId{playerId}, playerName,
                                     std::move(mails), std::move(currencies));
            writeProxy->SetZone(clientSessionId, entry->zoneId);
        }

        // 결과를 먼저 보낸다 -- 클라이언트가 로딩 화면으로 넘어간 뒤에 존 입장 통지를 받는
        // 순서가 되어야 "로그인은 됐는데 화면이 안 넘어간다"가 안 생긴다.
        SendResult(gatewaySession, clientSessionId, EErrorCode::Success, playerId, playerName);

        zoneLink->zoneSession->SendPacket(PacketId::W2ZEnterZone, enterBody);

        LOG.Info(ELogCategory::Gateway, "로그인 성공, 입장 존 배정")
            .KV("ClientSessionId", clientSessionId).KV("PlayerName", playerName)
            .KV("PlayerId", playerId).KV("ZoneId", entry->zoneId)
            .KV("X", entry->x).KV("Y", entry->y).KV("BodyBytes", enterBody.size());
    }

    void LoginProcessor::SendResult(const Network::Session::SPtr& gatewaySession,
                                     const Network::SessionId clientSessionId, const EErrorCode errorCode,
                                     const Base::RUID playerId, const std::string_view playerName) const
    {
        ClientEnvelopeHeader header{};
        header.clientSessionId = clientSessionId;
        header.innerPacketId = static_cast<uint16_t>(PacketId::W2CLogin);

        Packet::BinaryWriter binaryWriter;
        binaryWriter.Write(header);
        binaryWriter.Write(static_cast<int32_t>(errorCode));
        binaryWriter.Write(playerId);
        binaryWriter.WriteString(playerName);

        gatewaySession->SendPacket(PacketId::W2GRelay, binaryWriter.GetBuffer());
    }
}
