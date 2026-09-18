#include "pch.h"
#include "Server/Core/Src/Network/Service.h"

namespace Network
{
    Service::Service(const size_t ioThreadCount)
        : ioPool_(ioThreadCount)
    {
    }

    Service::~Service()
    {
        // IoContextPool 소멸자가 Stop/Join 을 한 번 더 하지만, Listener/Connector 를 먼저
        // 끊어야 이미 세워지는 컨텍스트로 accept 완료가 들어가지 않는다.
        Stop();
    }

    void Service::AddListener(const uint16_t port, IPacketHandler& handler)
    {
        // **acceptor 는 항상 0번 컨텍스트에 둔다.** accept 자체는 가벼워 한 스레드로 충분하고,
        // 실제 부하가 걸리는 세션은 Listener 가 풀에서 라운드로빈으로 나눠 붙인다.
        listeners_.push_back(std::make_shared<Listener>(ioPool_.At(0), ioPool_, port, handler));
    }

    void Service::AddConnector(std::string host, const uint16_t port, IPacketHandler& handler)
    {
        connectors_.push_back(std::make_shared<Connector>(ioPool_.At(0), std::move(host), port, handler));
    }

    void Service::Start()
    {
        for (const auto& listener : listeners_)
        {
            listener->Start();
        }
        for (const auto& connector : connectors_)
        {
            connector->Start();
        }

        ioPool_.Run();
    }

    void Service::Join()
    {
        ioPool_.Join();
    }

    void Service::Stop()
    {
        // **받는 쪽부터 끊는다.** 컨텍스트를 먼저 세우면 아직 열려 있는 acceptor 의 완료가
        // 갈 곳을 잃는다.
        for (const auto& listener : listeners_)
        {
            listener->Stop();
        }
        for (const auto& connector : connectors_)
        {
            connector->Stop();
        }

        // Start() 전에 불려도(기동 실패 경로) 아직 돌지 않는 풀을 세우는 것뿐이라 무해하다.
        ioPool_.Stop();
    }
}
