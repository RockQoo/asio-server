#pragma once

#include "Shared/Core/Src/Common/RUID.h"
#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Processor/Group.h"
#include "Shared/Protocol/Src/ErrorCode.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Db/DbCommand.h"
#include "World/PlayerManager.h"
#include "World/ZoneLinkRegistry.h"
#include "Worker/ProcessorId.h"

namespace Network
{
    class Session;
}

namespace World
{
    // PlayerManager::Mutexed 를 쓰므로 전방 선언으로는 부족하다.
    class DbConnectionPool;

    // C2W 대역(로그인)의 처리기. **IPacketHandler가 아니다** -- 운영툴처럼 자기 포트를 갖는 게
    // 아니라, 클라이언트 패킷이 Gateway 릴레이 봉투에 실려 들어오므로 GatewayLinkHandler가
    // 봉투를 열어 이쪽으로 넘긴다(그래서 등록도 Listener가 아니라 그 핸들러가 한다).
    //
    // **스레드 규약이 이 클래스의 전부다.** 한 번의 로그인이 레인을 세 번 갈아탄다:
    //
    //   BASIC(Login, owner=clientSessionId)   패킷 파싱 / 중복 로그인 거절
    //     -> DB(Db, owner=hash(playerName))   usp_players_select + 비밀번호 검증
    //                                         없으면 RUID 발급 + usp_players_upsert (자동 가입)
    //     -> BASIC(Login, owner=clientSessionId)  PlayerManager 갱신 + 존 입장 + 결과 전송
    //
    // DB 왕복이 **반드시 DB 그룹**이어야 하는 이유: 쿼리 한 번이 BASIC 레인에 걸리면 그 레인에
    // 배정된 모든 플레이어의 패킷이 그동안 멈춘다(config/world.cfg의 db_threads 주석).
    //
    // **DB 레인의 owner가 playerId가 아니라 이름 해시인 이유**: 로그인이 끝나기 전에는
    // playerId를 모른다. 이름으로 해시하면 같은 이름의 동시 첫 로그인이 한 DB 스레드로
    // 직렬화되어, usp_players_upsert의 HOLDLOCK과 이중으로 안전망이 된다.
    class LoginProcessor
    {
    public:
        LoginProcessor(PlayerManager::Mutexed& playerManager, ZoneLinkRegistry::Mutexed& zoneLinkRegistry,
                       Processor::Group<EProcessorId>& basicGroup,
                       Processor::Group<EProcessorId>& dbGroup,
                       DbConnectionPool& dbPool);

        // GatewayLinkHandler가 봉투 안 id의 방향이 C2W일 때 부른다.
        // **BASIC 레인(owner=clientSessionId)에서 불린다** -- I/O 스레드가 아니다.
        void HandleClientPacket(const std::shared_ptr<Network::Session>& gatewaySession,
                                const Network::SessionId clientSessionId, const PacketId packetId,
                                const std::span<const byte> payload);

    private:
        void HandleLogin(const std::shared_ptr<Network::Session>& gatewaySession,
                         const Network::SessionId clientSessionId, const std::span<const byte> payload);

        // 아래 둘은 **DB 레인**에서 불린다(AutoDbCommand의 콜백). BASIC 상태를 만지지 않고,
        // 결과만 PostResult/PostSuccess로 BASIC에 되던진다.
        void OnAccountSelected(const std::shared_ptr<Network::Session>& gatewaySession,
                               const Network::SessionId clientSessionId, const std::string& playerName,
                               const std::string& password, const bool succeeded, const DbResult& result);
        void OnAccountCreated(const std::shared_ptr<Network::Session>& gatewaySession,
                              const Network::SessionId clientSessionId, const std::string& playerName,
                              const Common::RUID requestedPlayerId, const bool succeeded, const DbResult& result);

        // DB 레인 -> BASIC 레인으로 결말을 넘긴다.
        void PostFailure(const std::shared_ptr<Network::Session>& gatewaySession,
                         const Network::SessionId clientSessionId, const EErrorCode errorCode);
        void PostSuccess(const std::shared_ptr<Network::Session>& gatewaySession,
                         const Network::SessionId clientSessionId, const std::string& playerName,
                         const Common::RUID playerId);

        // BASIC 레인. 인증을 확정하고 존에 입장시킨 뒤 결과를 보낸다.
        void CompleteLogin(const std::shared_ptr<Network::Session>& gatewaySession,
                           const Network::SessionId clientSessionId, const std::string& playerName,
                           const Common::RUID playerId);

        void SendResult(const std::shared_ptr<Network::Session>& gatewaySession,
                        const Network::SessionId clientSessionId, const EErrorCode errorCode,
                        const Common::RUID playerId, const std::string_view playerName) const;

        // players.player_name이 NVARCHAR(32)다. 바이트가 아니라 글자 수 제한이라 한글이면
        // UTF-8 3바이트씩 늘어나므로, 여기서는 바이트 상한을 넉넉히 잡고 최종 판정은 DB가 한다.
        static constexpr size_t kMaxPlayerNameBytes = 96;
        static constexpr size_t kMaxPasswordBytes = 128;

        PlayerManager::Mutexed& playerManager_;
        ZoneLinkRegistry::Mutexed& zoneLinkRegistry_;
        Processor::Group<EProcessorId>& basicGroup_;
        Processor::Group<EProcessorId>& dbGroup_;
        DbConnectionPool& dbPool_;
    };
}
