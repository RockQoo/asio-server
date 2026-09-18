#pragma once

#include "Server/Core/Src/Network/Connector.h"
#include "Server/Core/Src/Network/IoContextPool.h"
#include "Server/Core/Src/Network/Listener.h"

namespace Network
{
    class IPacketHandler;

    // **이 프로세스의 소켓 계층 전부.** io 스레드와 그 위에서 도는 accept·connect 를 한 덩어리로
    // 소유한다. 여기서 완료를 받아 프레임까지 조립하고, 그다음부터가 파이프라인 레인이다.
    //
    // **왜 App 이 Listener 를 직접 들지 않는가**: 포트가 늘 때마다 App 에 멤버가 하나씩 늘고,
    // 그때마다 Run()/Stop() 두 곳을 같이 고쳐야 했다. 하나를 빠뜨려도 컴파일은 되므로
    // "종료 때 그 포트만 안 닫히는" 종류의 실수가 난다. 목록으로 들고 있으면 그 실수가 없다.
    //
    // 이 단계를 파이프라인 레인(MessageProducer)으로 만들지 않은 근거:
    // docs/design/network-lane.md
    //
    // **호출 순서**: AddListener/AddConnector 를 다 부른 뒤 Start() -> Join().
    // Stop() 은 시그널 핸들러처럼 **다른 스레드에서** 불린다.
    class Service final
    {
    public:
        explicit Service(const size_t ioThreadCount);
        ~Service();

        Service(const Service&) = delete;
        Service& operator=(const Service&) = delete;

        // **Start() 전에만 부른다.** 여기서 포트를 잡으므로, 이미 쓰는 포트면 이 자리에서
        // 예외가 난다(기동 실패가 드러나는 지점이다).
        void AddListener(const uint16_t port, IPacketHandler& handler);

        // 바깥으로 나가는 링크. 상대가 아직 안 떠 있어도 되고, 붙을 때까지 재시도한다.
        void AddConnector(std::string host, const uint16_t port, IPacketHandler& handler);

        // 등록된 것을 전부 시작하고 io 스레드를 띄운다. **블로킹하지 않는다.**
        void Start();

        // io 스레드가 끝날 때까지 기다린다. Stop() 이 불려야 끝난다.
        void Join();

        // accept/connect 를 끊고 io_context 를 세운다. 여러 번 불려도 안전하다.
        void Stop();

        // 소켓 컨텍스트를 빌려 쓰는 자리(시그널 대기, 주기 타이머)가 있어 열어둔다.
        // **세션을 직접 만들지 말 것** -- 그건 Listener/Connector 의 일이다.
        [[nodiscard]] asio::io_context& Next() noexcept { return ioPool_.Next(); }
        [[nodiscard]] asio::io_context& At(const size_t index) noexcept { return ioPool_.At(index); }

        [[nodiscard]] size_t ThreadCount() const noexcept { return ioPool_.Size(); }

    private:
        IoContextPool ioPool_;

        // 둘 다 enable_shared_from_this 라 shared_ptr 로만 소유된다(비동기 완료 핸들러가
        // 도는 동안 자기 생존을 보장해야 한다).
        std::vector<std::shared_ptr<Listener>> listeners_;
        std::vector<std::shared_ptr<Connector>> connectors_;
    };
}
