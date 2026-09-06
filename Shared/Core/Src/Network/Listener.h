#pragma once

#include "Shared/Core/Src/Common/Types.h"

#include <asio.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

namespace Network
{
    class IPacketHandler;
    class IoContextPool;

    // 들어오는 TCP 연결을 계속 accept하는 루프. 새로 생기는 Session마다 풀에서 라운드로빈으로
    // 고른 io_context를 붙여주므로, 접속이 많아져도 여러 I/O 스레드로 고르게 퍼진다.
    class Listener final : public std::enable_shared_from_this<Listener>
    {
    public:
        Listener(asio::io_context& acceptorContext, IoContextPool& ioPool, const uint16_t port, IPacketHandler& handler);

        void Start();
        void Stop();

    private:
        void DoAccept();

        asio::ip::tcp::acceptor acceptor_;
        IoContextPool& ioPool_;
        IPacketHandler& handler_;
        std::atomic<SessionId> nextSessionId_{1};
    };
}
