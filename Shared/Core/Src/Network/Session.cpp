#include "Shared/Core/Src/pch.h"
#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Network/IPacketHandler.h"
#include "Shared/Core/Src/Packet/PacketFramer.h"
#include "Shared/Core/Src/Common/CoreException.h"

#include <cstring>

namespace Network
{
    Session::Session(asio::io_context& ioContext, const SessionId id, IPacketHandler& handler)
        : socket_(ioContext)
        , strand_(asio::make_strand(ioContext))
        , id_(id)
        , handler_(handler)
    {
    }

    Session::~Session() = default;

    void Session::Start()
    {
        DoRead();
    }

    void Session::Close()
    {
        if (closed_.exchange(true))
        {
            return;
        }

        asio::post(strand_, [self = shared_from_this()]
        {
            std::error_code ec;
            self->socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
            self->socket_.close(ec);
        });
    }

    void Session::SendPacket(const uint16_t packetId, const std::span<const byte> payload)
    {
        if (closed_.load(std::memory_order_acquire))
        {
            return;
        }

        auto frame = Packet::BuildFrame(packetId, payload);

        // 큐에 넣는 작업(그리고 sendQueue_/writing_ 접근)은 항상 strand 위에서 일어난다.
        // SendPacket을 어느 스레드에서 호출했든 상관없이, 이 덕분에 크로스 스레드 전송이 안전해진다.
        asio::post(strand_, [self = shared_from_this(), frame = std::move(frame)]() mutable
        {
            const auto alreadyWriting = self->writing_;
            self->sendQueue_.push_back(std::move(frame));
            if (!alreadyWriting)
            {
                self->DoWrite();
            }
        });
    }

    void Session::DoRead()
    {
        socket_.async_read_some(
            asio::buffer(receiveBuffer_),
            asio::bind_executor(strand_,
                [self = shared_from_this()](const std::error_code ec, const size_t bytesTransferred)
                {
                    if (ec)
                    {
                        self->HandleClose(ec);
                        return;
                    }

                    try
                    {
                        self->packetBuffer_.Append(std::span(self->receiveBuffer_.data(), bytesTransferred));

                        Packet::PacketHeader header{};
                        std::vector<byte> payload;
                        while (self->packetBuffer_.TryExtract(header, payload))
                        {
                            self->handler_.OnPacket(self, header, payload);
                        }
                    }
                    catch (const Common::CoreException& ex)
                    {
                        LOG.Error(ELogCategory::Packet, "패킷 프레이밍 오류")
                            .KV("SessionId", self->id_)
                            .KV("Code", static_cast<int32_t>(ex.Code()))
                            .KV("Message", ex.what());
                        self->HandleClose(std::make_error_code(std::errc::protocol_error));
                        return;
                    }
                    catch (const std::exception& ex)
                    {
                        LOG.Error(ELogCategory::Network, "알 수 없는 오류")
                            .KV("SessionId", self->id_)
                            .KV("Message", ex.what());
                        self->HandleClose(std::make_error_code(std::errc::protocol_error));
                        return;
                    }

                    self->DoRead();
                }));
    }

    void Session::DoWrite()
    {
        writing_ = true;

        asio::async_write(socket_, asio::buffer(sendQueue_.front()),
            asio::bind_executor(strand_,
                [self = shared_from_this()](const std::error_code ec, size_t /*bytesTransferred*/)
                {
                    if (ec)
                    {
                        self->HandleClose(ec);
                        return;
                    }

                    self->sendQueue_.pop_front();
                    if (!self->sendQueue_.empty())
                    {
                        self->DoWrite();
                    }
                    else
                    {
                        self->writing_ = false;
                    }
                }));
    }

    void Session::HandleClose(const std::error_code& ec)
    {
        const auto wasAlreadyClosed = closed_.exchange(true);

        std::error_code ignored;
        socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
        socket_.close(ignored);

        if (!wasAlreadyClosed)
        {
            handler_.OnClosed(shared_from_this(), ec);
        }
    }

    std::string Session::RemoteAddress() const
    {
        std::error_code ec;
        const auto endpoint = socket_.remote_endpoint(ec);
        if (ec)
        {
            return "unknown";
        }
        return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
    }
}
