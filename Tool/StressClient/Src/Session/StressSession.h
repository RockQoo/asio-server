#pragma once

#include "Shared/Core/Src/Network/Connector.h"
#include "Shared/Core/Src/Network/IPacketHandler.h"
#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Timer/RepeatingTimer.h"
#include "Tool/StressClient/Src/Stats/StressStats.h"

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Stress
{
    // 시뮬레이션 클라이언트 1개. Network::IPacketHandler를 구현해 자기 전용 Connector로
    // Gateway에 접속하고, EnterZoneNotify를 받으면 MailAdd->MailAddAck->MailDel->MailDelAck
    // 사이클을 목표 횟수만큼 반복한다. 상태 전이는 전부 OnPacket 콜백에서 논블로킹으로
    // 일어난다 -- 이 세션이 물린 io_context 스레드 위에서는 자기 콜백들이 항상 순서대로만
    // 호출되므로(Shared/Core/Src/Network/IoContextPool: io_context 1개당 전용 스레드 1개) 사이클 진행
    // 관련 멤버(cycleIndex_/lastAddedMailId_ 등)는 원자적일 필요가 없다. 워치독이 "다른"
    // 스레드에서 읽는 진행 시각/완료 플래그만 atomic으로 둔다.
    class StressSession final : public Network::IPacketHandler
    {
    public:
        StressSession(const size_t index, asio::io_context& ioContext, std::string host, const uint16_t port,
                      const uint32_t cyclesTarget, const bool isBroadcaster, StressStats& stats);

        void Start();
        void Stop();

        void OnSessionOpened(const std::shared_ptr<Network::Session>& session) override;
        void OnPacket(const std::shared_ptr<Network::Session>& session,
                      const Packet::PacketHeader& header,
                      const std::span<const byte> payload) override;
        void OnClosed(const std::shared_ptr<Network::Session>& session, const std::error_code& reason) override;

        [[nodiscard]] bool IsDone() const noexcept { return done_.load(std::memory_order_acquire); }
        [[nodiscard]] std::chrono::steady_clock::time_point LastProgressAt() const noexcept;
        [[nodiscard]] size_t Index() const noexcept { return index_; }

    private:
        void HandleEnterZoneNotify(const std::span<const byte> payload);
        void HandleMailAddAck(const std::span<const byte> payload);
        void HandleMailDelAck(const std::span<const byte> payload);
        void HandleBroadcastPacket();

        void SendMailAdd();
        void SendMailDel(const uint32_t mailId);
        void StartBroadcastTimer();
        void MarkProgress() noexcept;

        // 왜 여기서 재는가: 클라이언트가 실제로 체감하는 지연은 "요청을 소켓에 얹은 순간부터
        // 응답이 파싱된 순간까지"다. 서버 내부 처리 시간만 재면 큐에서 밀린 시간이 통째로
        // 빠져 실제보다 낙관적인 수치가 나온다.
        [[nodiscard]] static uint64_t ElapsedUs(const std::chrono::steady_clock::time_point since) noexcept;

        size_t index_;
        asio::io_context& ioContext_;
        std::string host_;
        uint16_t port_;
        uint32_t cyclesTarget_;
        bool isBroadcaster_;
        StressStats& stats_;

        std::shared_ptr<Network::Connector> connector_;
        std::shared_ptr<Network::Session> session_;
        std::unique_ptr<Timer::RepeatingTimer> broadcastTimer_;

        // 이 세션의 io_context 스레드 안에서만 접근된다(OnPacket/OnSessionOpened/OnClosed는 전부
        // 그 스레드에서 순서대로 호출됨) -- 락/atomic 불필요.
        uint32_t cycleIndex_{0};
        uint32_t lastAddedMailId_{0};

        // 지연 측정용 송신 시각. 위 멤버들과 같은 이유로(자기 io 스레드에서만 읽고 쓴다)
        // atomic이 아니다 -- 집계는 이 스레드에서 값을 만들어 StressStats의 atomic에만 넘긴다.
        std::chrono::steady_clock::time_point mailAddSentAt_{};
        std::chrono::steady_clock::time_point mailDelSentAt_{};
        std::chrono::steady_clock::time_point cycleStartedAt_{};

        // 워치독(다른 스레드)이 읽으므로 atomic.
        std::atomic<bool> done_{false};
        std::atomic<int64_t> lastProgressAtTicks_{0};
    };
}
