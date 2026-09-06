#pragma once

#include "Shared/Core/Src/Common/Types.h"

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace Network
{
    class IPacketHandler;

    // 아웃바운드 TCP 연결(다른 서버에 "클라이언트"로 접속). Listener와 대칭 구조지만 accept
    // 대신 connect라는 점만 다르다 -- 성공하면 똑같이 Session을 만들어
    // IPacketHandler::OnSessionOpened를 호출한다. accept와 connect 둘 다 "세션이 막 시작됐다"는
    // 의미는 동일해서 인터페이스에 별도 OnConnected를 추가하지 않고 재사용했다. **최초 연결에
    // 실패하는 동안만** 재시도 타이머로 계속 시도한다 -- Zone/Gateway가 World보다 먼저 뜨는
    // 기동 순서를 신경 쓰지 않아도 되게 하기 위함이다. 한 번 맺어진 연결이 나중에 끊기는 경우의
    // 재연결은 구현하지 않았다(학습 범위 밖 -- README "알려진 한계" 참고).
    class Connector final : public std::enable_shared_from_this<Connector>
    {
    public:
        Connector(asio::io_context& ioContext, std::string host, const uint16_t port, IPacketHandler& handler);

        void Start();
        void Stop();

    private:
        void DoConnect();
        void ScheduleRetry();

        asio::io_context& ioContext_;
        std::string host_;
        uint16_t port_;
        IPacketHandler& handler_;
        asio::steady_timer retryTimer_;
        std::atomic<bool> stopped_{false};
        std::atomic<SessionId> nextSessionId_{1};

        static constexpr std::chrono::milliseconds kRetryInterval{2000};
    };
}
