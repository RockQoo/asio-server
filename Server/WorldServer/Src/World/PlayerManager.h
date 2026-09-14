#pragma once

#include "Shared/Protocol/Src/Ids.h"
#include "Shared/Core/Src/Common/RUID.h"
#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Thread/Mutexed.h"

namespace Network
{
    class Session;
}

namespace World
{
    // 우편 한 통. **Zone의 Mail::Info와 필드는 같지만 일부러 따로 선언한다** -- 두 쪽이 하는
    // 일이 다르기 때문이다. Zone의 Mail::Model은 추가/삭제/만료/롤백을 하는 콘텐츠 모델이고,
    // World는 DB에서 읽어 들고 있다가 존에 넘기는 캐시일 뿐이다. 모델을 공유하면 World가
    // 존의 UnitOfWork·만료 스윕까지 떠안는다. 공유하는 것은 클래스가 아니라 와이어 포맷이다.
    struct MailInfo
    {
        // Zone의 Mail::Info.mailId와 같은 RUID다. 와이어 포맷을 공유하므로 폭도 같아야 한다.
        Protocol::MailId mailId{};
        std::string title;
        std::string body;
        int64_t sendUt{};
        int64_t endUt{};
    };

    // 접속 중인 플레이어 한 명에 대해 World가 아는 전부.
    struct PlayerInfo
    {
        // ---- 라우팅 ----
        // 이 클라이언트에게 보낼 때 쓸 게이트웨이 링크. **클라이언트의 소켓이 아니다** --
        // Gateway가 여러 대면 "이 사람은 몇 번 게이트웨이 경유"를 가리키게 된다.
        std::shared_ptr<Network::Session> gatewaySession;
        uint32_t zoneId{};

        // ---- 계정 ----
        // 로그인으로 확정된 DB의 player_id(RUID). 인증 전에는 0이다.
        //
        // **존으로 나가는 PlayerZoneStatePacket::playerId 와 같은 값이다.** 예전에는 그쪽이
        // uint32 라 clientSessionId 를 잘라 넣고 있었고, 재접속할 때마다 값이 바뀌었다.
        Protocol::PlayerId playerId{};
        std::string playerName;

        // C2WLogin을 통과했는가. **false면 게임 패킷을 존으로 넘기지 않는다.**
        bool authenticated{};

        // ---- 콘텐츠 캐시 ----
        // 로그인 때 usp_players_load로 채우고, 존에 그대로 넘겨 동기화한다. 나중에 존이 올린
        // UnitOfWork 태스크를 여기에 대조해 위조를 걸러내는 자리이기도 하다.
        std::unordered_map<Protocol::MailId, MailInfo> mails;
        std::unordered_map<uint8_t, int64_t> currencies;  // 키는 Protocol::ECurrencyType
    };

    // 접속 중인 플레이어 전원. 라우팅 테이블 + 콘텐츠 캐시 + 브로드캐스트 대상 목록을 겸한다.
    //
    // **왜 샤딩이 아니라 락인가**: World는 Zone과 스레드 모델이 다르다. Zone은 모든 일이
    // `clientSessionId % N` 어피니티로 스레드가 고정되지만, World는 기본이 "남는 스레드"이고
    // 순서나 정합성이 필요한 메시지만 ownerId를 지정해 레인을 고정한다. 그래서 이 테이블은
    // 어느 스레드에서든 읽힐 수 있고, 어피니티로는 지킬 수 없다.
    //
    // 예전에는 `clientSessionId % 스레드수`로 샤딩하고 "그 샤드는 그 번호 스레드만 만진다"는
    // 전제로 락을 뺐는데, **그 전제 자체가 World에는 성립하지 않았다.** 실제로 운영툴의 단일
    // 대상 명령(우편 발송/삭제)이 운영툴 레인에서 남의 샤드를 읽고 있었다.
    //
    //   조회 -> `players->Find(...)`        (const = shared_lock)
    //   변경 -> `players.Write()->Add(...)` (비const = unique_lock)
    //
    // 설계 근거: docs/design/locking-strategy.md
    class PlayerManager
    {
    public:
        using Mutexed = Thread::Mutexed<PlayerManager>;

        void Add(const Network::SessionId clientSessionId, const std::shared_ptr<Network::Session>& gatewaySession);
        void Remove(const Network::SessionId clientSessionId);
        void SetZone(const Network::SessionId clientSessionId, const uint32_t zoneId);

        // 로그인 성공을 기록한다. 이 호출 뒤에야 그 세션의 게임 패킷이 존으로 흐른다.
        // 콘텐츠 캐시(mails/currencies)는 DB에서 읽어온 것을 그대로 옮겨 담는다.
        void SetAuthenticated(const Network::SessionId clientSessionId, const Protocol::PlayerId playerId,
                              std::string playerName,
                              std::unordered_map<Protocol::MailId, MailInfo> mails,
                              std::unordered_map<uint8_t, int64_t> currencies);

        // ---- 존이 올린 UnitOfWork 태스크 반영 ----
        // 셋 다 BASIC 레인(owner = clientSessionId)에서만 불린다. 그 레인은 이 클라이언트의
        // 접속 종료(HandleClientDisconnected)와도 같은 주인이라, "이미 지워진 사람에게 우편을
        // 넣는" 순서 역전이 생기지 않는다.
        //
        // **캐시를 갱신하지 않으면 프로세스를 넘는 핸드오프에서 그 변경이 사라진다** -- 존이
        // W2ZEnterZone으로 받는 시작 상태가 곧 이 캐시이기 때문이다.
        void AddMail(const Network::SessionId clientSessionId, MailInfo mailInfo);
        void RemoveMail(const Network::SessionId clientSessionId, const Protocol::MailId mailId);
        void SetCurrency(const Network::SessionId clientSessionId, const uint8_t currencyType,
                         const int64_t amount);

        // DB 작업의 주인이 될 player_id만 꺼낸다. FindRoute와 같은 이유로 우편함까지 복사하지
        // 않는다 -- 태스크 하나마다 불리는 자리다.
        [[nodiscard]] std::optional<Protocol::PlayerId> FindPlayerId(const Network::SessionId clientSessionId) const;

        // **값으로 복사해서 돌려준다.** 참조를 주면 호출부가 락을 벗어난 뒤에도 그걸 들고 있을
        // 수 있고, 그때 다른 스레드가 Remove하면 댕글링이다. 콘텐츠 캐시가 커지면 이 복사가
        // 비싸지므로, 라우팅만 필요한 자리는 아래 FindRoute를 쓴다.
        [[nodiscard]] std::optional<PlayerInfo> Find(const Network::SessionId clientSessionId) const;

        // 릴레이 경로가 쓰는 가벼운 조회 -- 패킷마다 불리므로 우편함까지 복사하면 안 된다.
        struct Route
        {
            std::shared_ptr<Network::Session> gatewaySession;
            uint32_t zoneId{};
            bool authenticated{};
        };
        [[nodiscard]] std::optional<Route> FindRoute(const Network::SessionId clientSessionId) const;

        [[nodiscard]] size_t Count() const noexcept { return players_.size(); }

        // 전체 순회. **읽기 락을 잡은 채로 돈다** -- func 안에서 이 매니저를 다시 변경하면
        // 교착한다(같은 스레드가 unique_lock을 또 잡으려 든다). 순회 중에 바꿔야 하면 대상을
        // 먼저 벡터로 모아 두고 락을 벗어난 뒤에 처리한다.
        void ForEach(const std::function<void(const Network::SessionId, const PlayerInfo&)>& func) const;

    private:
        std::unordered_map<Network::SessionId, PlayerInfo> players_;
    };
}
