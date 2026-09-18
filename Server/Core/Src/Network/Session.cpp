#include "pch.h"
#include "Server/Core/Src/Network/Session.h"
#include "Server/Core/Src/Network/IPacketHandler.h"
#include "Server/Core/Src/Network/SendStats.h"
#include "Server/Core/Src/Packet/PacketFramer.h"
#include "Server/Core/Src/Base/CoreException.h"

namespace Network
{
    Session::Session(asio::io_context& ioContext, const SessionId id, IPacketHandler& handler)
        : socket_(ioContext)
        , strand_(asio::make_strand(ioContext))
        , id_(id)
        , handler_(handler)
    {
    }

    Session::~Session()
    {
        // 연결이 죽어 못 보낸 채로 남은 몫을 계측에서 뺀다. **안 빼면 전체 적체 카운터가
        // 영영 안 내려간다.** 여기 올 때는 비동기 작업이 전부 끝난 뒤라 큐가 더 안 바뀐다.
        size_t droppedBytes = 0;
        for (const auto& frame : sendQueue_)
        {
            droppedBytes += frame.size();
        }
        SendStats::Instance().OnDropped(sendQueue_.size(), droppedBytes);
    }

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

        // BuildFrame이 상한 초과를 예외로 알린다. **여기서 잡아야 한다** -- SendPacket은
        // I/O 스레드에서도 불리고, 새어나간 예외는 그 스레드를 잡아줄 곳 없이 죽여서
        // 프로세스가 std::terminate로 끝난다(cpp-patterns.md "asio 비동기 핸들러의 예외 안전").
        // 그 패킷 하나만 버리고 연결은 살린다 -- 어차피 보냈어도 받는 쪽이 끊었을 것이고,
        // 이렇게 하면 **보낸 쪽 로그에 원인이 남는다.**
        std::vector<byte> frame;
        try
        {
            frame = Packet::BuildFrame(packetId, payload);
        }
        catch (const Base::CoreException& ex)
        {
            LOG.Error(ELogCategory::Packet, "패킷이 너무 커서 보내지 않는다")
                .KV("SessionId", id_).KV("PacketId", packetId)
                .KV("PayloadBytes", payload.size()).KV("Message", ex.what());
            return;
        }

        // 큐에 넣는 작업(그리고 sendQueue_/writing_ 접근)은 항상 strand 위에서 일어난다.
        // SendPacket을 어느 스레드에서 호출했든 상관없이, 이 덕분에 크로스 스레드 전송이 안전해진다.
        asio::post(strand_, [self = shared_from_this(), frame = std::move(frame)]() mutable
        {
            const auto alreadyWriting = self->writing_;
            const auto frameBytes = frame.size();
            self->sendQueue_.push_back(std::move(frame));

            // strand 안이라 이 세션의 큐 길이는 여기서 정확하다. 합산만 프로세스 전역이다.
            SendStats::Instance().OnEnqueue(self->id_, self->sendQueue_.size(), frameBytes);

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

                        Packet::Header header{};
                        std::vector<byte> payload;
                        while (self->packetBuffer_.TryExtract(header, payload))
                        {
                            self->handler_.OnPacket(self, header, payload);
                        }
                    }
                    catch (const Base::CoreException& ex)
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

                    SendStats::Instance().OnSent(self->sendQueue_.front().size());
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
