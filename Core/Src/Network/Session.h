#pragma once

#include "Core/Src/Common/Types.h"
#include "Core/Src/Packet/PacketBuffer.h"

#include <asio.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Network
{
    class IPacketHandler;

    // TCP 연결 하나. 소켓을 건드리는 모든 코드는 strand_를 거치므로, I/O 스레드가 동시에 읽고
    // 있는 중에도 어느 스레드에서든(특히 TaskWorker 로직 스레드에서) Send()를 안전하게 호출할
    // 수 있다. shared_ptr(enable_shared_from_this)로만 소유되어, 비동기 완료 핸들러가 진행되는
    // 동안 자기 자신의 생존을 보장한다.
    class Session final : public std::enable_shared_from_this<Session>
    {
    public:
        Session(asio::io_context& ioContext, const SessionId id, IPacketHandler& handler);
        ~Session();

        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;

        void Start();
        void Close();

        // [헤더 + 페이로드] 프레임을 만들어 비동기 전송 큐에 넣는다. 스레드 세이프하다.
        void SendPacket(const uint16_t packetId, const std::span<const byte> payload);

        [[nodiscard]] asio::ip::tcp::socket& Socket() noexcept { return socket_; }
        [[nodiscard]] SessionId Id() const noexcept { return id_; }
        [[nodiscard]] bool IsOpen() const noexcept { return socket_.is_open(); }
        [[nodiscard]] std::string RemoteAddress() const;

    private:
        void DoRead();
        void DoWrite();
        void HandleClose(const std::error_code& ec);

        asio::ip::tcp::socket socket_;
        asio::strand<asio::io_context::executor_type> strand_;

        const SessionId id_;
        IPacketHandler& handler_;

        static constexpr size_t kReceiveBufferSize = 4096;
        std::array<byte, kReceiveBufferSize> receiveBuffer_{};
        Packet::PacketBuffer packetBuffer_;

        std::deque<std::vector<byte>> sendQueue_;
        bool writing_{false};

        std::atomic<bool> closed_{false};
    };
}
