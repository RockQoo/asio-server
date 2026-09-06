#include "Shared/Core/Src/pch.h"
#include "Shared/Core/Src/Network/Connector.h"
#include "Shared/Core/Src/Network/IPacketHandler.h"
#include "Shared/Core/Src/Network/Session.h"

#include <exception>
#include <utility>

namespace Network
{
    Connector::Connector(asio::io_context& ioContext, std::string host, const uint16_t port, IPacketHandler& handler)
        : ioContext_(ioContext)
        , host_(std::move(host))
        , port_(port)
        , handler_(handler)
        , retryTimer_(ioContext)
    {
    }

    void Connector::Start()
    {
        DoConnect();
    }

    void Connector::Stop()
    {
        stopped_.store(true, std::memory_order_release);
        retryTimer_.cancel();
    }

    void Connector::DoConnect()
    {
        // resolve()는 실패 시 예외를 던진다 -- 이 함수 자체가 io_context 스레드에서 직접
        // 호출되므로(Start() 최초 호출 및 재시도 타이머 콜백) 여기서 못 잡으면 스레드가
        // 처리되지 않은 예외로 죽는다.
        try
        {
            auto session = std::make_shared<Session>(ioContext_, nextSessionId_.fetch_add(1), handler_);
            asio::ip::tcp::resolver resolver(ioContext_);
            const auto endpoints = resolver.resolve(host_, std::to_string(port_));

            asio::async_connect(session->Socket(), endpoints,
                [this, self = shared_from_this(), session](const std::error_code ec, const asio::ip::tcp::endpoint&)
                {
                    if (stopped_.load(std::memory_order_acquire))
                    {
                        return;
                    }

                    if (ec)
                    {
                        LOG.Warning(ELogCategory::Network, "연결 실패, 재시도 예정")
                            .KV("Host", host_).KV("Port", port_).KV("Message", ec.message());
                        ScheduleRetry();
                        return;
                    }

                    LOG.Info(ELogCategory::Network, "연결 성공").KV("Host", host_).KV("Port", port_);
                    handler_.OnSessionOpened(session);
                    session->Start();
                });
        }
        catch (const std::exception& ex)
        {
            LOG.Warning(ELogCategory::Network, "주소 확인 실패, 재시도 예정")
                .KV("Host", host_).KV("Port", port_).KV("Message", ex.what());
            ScheduleRetry();
        }
    }

    void Connector::ScheduleRetry()
    {
        if (stopped_.load(std::memory_order_acquire))
        {
            return;
        }

        retryTimer_.expires_after(kRetryInterval);
        retryTimer_.async_wait([this, self = shared_from_this()](const std::error_code ec)
        {
            if (!ec && !stopped_.load(std::memory_order_acquire))
            {
                DoConnect();
            }
        });
    }
}
