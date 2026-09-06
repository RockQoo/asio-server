#include "Shared/Core/Src/pch.h"
#include "Shared/Core/Src/Network/Listener.h"
#include "Shared/Core/Src/Network/IPacketHandler.h"
#include "Shared/Core/Src/Network/IoContextPool.h"
#include "Shared/Core/Src/Network/Session.h"

namespace Network
{
    Listener::Listener(asio::io_context& acceptorContext, IoContextPool& ioPool, const uint16_t port, IPacketHandler& handler)
        : acceptor_(acceptorContext, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
        , ioPool_(ioPool)
        , handler_(handler)
    {
        acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    }

    void Listener::Start()
    {
        DoAccept();
    }

    void Listener::Stop()
    {
        std::error_code ec;
        acceptor_.close(ec);
    }

    void Listener::DoAccept()
    {
        auto& targetContext = ioPool_.Next();
        auto session = std::make_shared<Session>(targetContext, nextSessionId_.fetch_add(1), handler_);

        acceptor_.async_accept(session->Socket(),
            [this, self = shared_from_this(), session](const std::error_code ec)
            {
                if (!ec)
                {
                    handler_.OnSessionOpened(session);
                    session->Start();
                }
                else
                {
                    LOG.Error(ELogCategory::Network, "accept 오류").KV("Message", ec.message());
                }

                if (acceptor_.is_open())
                {
                    DoAccept();
                }
            });
    }
}
