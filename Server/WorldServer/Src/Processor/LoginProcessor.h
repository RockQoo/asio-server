#pragma once

#include "Shared/Core/Src/Common/RUID.h"
#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Processor/Group.h"
#include "Shared/Protocol/Src/ErrorCode.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Db/DbCommand.h"
#include "Processor/DbProcessor.h"
#include "World/PlayerManager.h"
#include "World/ZoneLinkRegistry.h"
#include "Worker/ProcessorId.h"

namespace World
{

    // C2W 대역(로그인)의 처리기. **IPacketHandler가 아니다** -- 운영툴처럼 자기 포트를 갖는 게
    // 아니라, 클라이언트 패킷이 Gateway 릴레이 봉투에 실려 들어오므로 MainProcessor가
    // 봉투를 열어 이쪽으로 넘긴다(그래서 등록도 Listener가 아니라 그 핸들러가 한다).
    //
    // **스레드 규약이 이 클래스의 전부다.** 한 번의 로그인이 레인을 세 번 갈아타지만,
    // **주인은 처음부터 끝까지 clientSessionId 하나다**:
    //
    //   BASIC(Login, owner=clientSessionId)   패킷 파싱 / 중복 로그인 거절
    //     -> DB(Db,    owner=clientSessionId) usp_players_select + 비밀번호 검증
    //                                         없으면 RUID 발급 + usp_players_upsert (자동 가입)
    //     -> DB(Db,    owner=clientSessionId) usp_players_load -- 우편/재화 적재
    //     -> BASIC(Login, owner=clientSessionId)  PlayerManager 갱신 + 존 입장 + 결과 전송
    //
    // **playerId를 알게 된 뒤에도 주인을 바꾸지 않는 이유**: 중간에 갈아타면 그 지점부터 앞
    // 구간과 직렬화가 끊겨, 같은 세션의 로그인 단계들이 서로 다른 strand에서 겹친다. 존 입장
    // 뒤의 UnitOfWork만 playerId가 주인이다(MainProcessor) -- 그건 세션이 아니라 계정에 묶이는
    // 일이라 재접속해도 같은 레인을 유지해야 하고, 로그인은 그 세션 안에서만 의미가 있다.
    //
    // DB 왕복이 **반드시 DB 그룹**이어야 하는 이유: 쿼리 한 번이 BASIC 레인에 걸리면 그 레인에
    // 배정된 모든 플레이어의 패킷이 그동안 멈춘다(config/world.cfg의 db_threads 주석).
    class LoginProcessor
    {
    public:
        LoginProcessor(PlayerManager::Mutexed& playerManager, ZoneLinkRegistry::Mutexed& zoneLinkRegistry,
                       Processor::Group<EProcessorId>& basicGroup, DbProcessor& dbProcessor);

        // MainProcessor가 봉투 안 id의 방향이 C2W일 때 부른다.
        // **BASIC 레인(owner=clientSessionId)에서 불린다** -- I/O 스레드가 아니다.
        void HandleClientPacket(const Network::Session::SPtr& gatewaySession,
                                const Network::SessionId clientSessionId, const PacketId packetId,
                                const std::span<const byte> payload);

    private:
        void HandleLogin(const Network::Session::SPtr& gatewaySession,
                         const Network::SessionId clientSessionId, const std::span<const byte> payload);

        // 아래 넷은 **DB 레인**에서 불린다(DbProcessor의 콜백). BASIC 상태를 만지지 않고,
        // 결과만 PostFailure/PostSuccess로 BASIC에 되던진다.
        void OnAccountSelected(const Network::Session::SPtr& gatewaySession,
                               const Network::SessionId clientSessionId, const std::string& playerName,
                               const std::string& password, const bool succeeded, const DbResult& dbResult);
        void OnAccountCreated(const Network::Session::SPtr& gatewaySession,
                              const Network::SessionId clientSessionId, const std::string& playerName,
                              const Common::RUID requestedPlayerId, const bool succeeded, const DbResult& dbResult);

        // 계정이 확정된 뒤 우편/재화를 적재한다. 여기서부터 owner가 playerId로 바뀐다.
        void LoadPlayerContent(const Network::Session::SPtr& gatewaySession,
                               const Network::SessionId clientSessionId, const std::string& playerName,
                               const Common::RUID playerId);
        void OnPlayerLoaded(const Network::Session::SPtr& gatewaySession,
                            const Network::SessionId clientSessionId, const std::string& playerName,
                            const Common::RUID playerId, const bool succeeded, const DbResult& dbResult);

        // DB 레인 -> BASIC 레인으로 결말을 넘긴다.
        void PostFailure(const Network::Session::SPtr& gatewaySession,
                         const Network::SessionId clientSessionId, const EErrorCode errorCode);
        void PostSuccess(const Network::Session::SPtr& gatewaySession,
                         const Network::SessionId clientSessionId, const std::string& playerName,
                         const Common::RUID playerId,
                         std::unordered_map<Protocol::MailId, MailInfo> mails,
                         std::unordered_map<uint8_t, int64_t> currencies);

        // BASIC 레인. 캐시를 채우고 인증을 확정한 뒤, 콘텐츠를 실어 존에 입장시킨다.
        void CompleteLogin(const Network::Session::SPtr& gatewaySession,
                           const Network::SessionId clientSessionId, const std::string& playerName,
                           const Common::RUID playerId,
                           std::unordered_map<Protocol::MailId, MailInfo> mails,
                           std::unordered_map<uint8_t, int64_t> currencies);

        void SendResult(const Network::Session::SPtr& gatewaySession,
                        const Network::SessionId clientSessionId, const EErrorCode errorCode,
                        const Common::RUID playerId, const std::string_view playerName) const;

        // players.player_name이 NVARCHAR(32)다. 바이트가 아니라 글자 수 제한이라 한글이면
        // UTF-8 3바이트씩 늘어나므로, 여기서는 바이트 상한을 넉넉히 잡고 최종 판정은 DB가 한다.
        static constexpr size_t kMaxPlayerNameBytes = 96;
        static constexpr size_t kMaxPasswordBytes = 128;

        PlayerManager::Mutexed& playerManager_;
        ZoneLinkRegistry::Mutexed& zoneLinkRegistry_;
        Processor::Group<EProcessorId>& basicGroup_;
        DbProcessor& dbProcessor_;
    };
}
