#include "Tool/LoadTestClient/Src/pch.h"
#include "Tool/LoadTestClient/Src/Session/StressSession.h"

#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Server/ZoneServer/Src/Packet/PacketId.h"
#include "Server/ZoneServer/Src/Packet/ZonePackets.h"

#include <utility>

namespace Load
{
    StressSession::StressSession(const size_t index, asio::io_context& ioContext, std::string host, const uint16_t port,
                                  const uint32_t cyclesTarget, const bool isBroadcaster, StressStats& stats)
        : index_(index)
        , ioContext_(ioContext)
        , host_(std::move(host))
        , port_(port)
        , cyclesTarget_(cyclesTarget)
        , isBroadcaster_(isBroadcaster)
        , stats_(stats)
    {
        // 접속 전부터 "진행이 없었다"고 취급되지 않도록(워치독이 now()-lastProgressAt으로
        // 정체를 판단하므로) 생성 시점을 기준선으로 잡아둔다.
        MarkProgress();
    }

    void StressSession::Start()
    {
        stats_.RecordAttempted();
        connector_ = std::make_shared<Network::Connector>(ioContext_, host_, port_, *this);
        connector_->Start();
    }

    void StressSession::Stop()
    {
        if (broadcastTimer_)
        {
            broadcastTimer_->Stop();
        }
        if (connector_)
        {
            connector_->Stop();
        }
        if (session_)
        {
            session_->Close();
        }
    }

    void StressSession::OnSessionOpened(const std::shared_ptr<Network::Session>& session)
    {
        session_ = session;
        stats_.RecordConnected();
        MarkProgress();
    }

    void StressSession::OnPacket(const std::shared_ptr<Network::Session>& /*session*/,
                                 const Packet::PacketHeader& header,
                                 const std::span<const byte> payload)
    {
        switch (static_cast<Zone::PacketId>(header.id))
        {
        case Zone::PacketId::EnterZoneNotify:
            HandleEnterZoneNotify(payload);
            break;
        case Zone::PacketId::MailAddAck:
            HandleMailAddAck(payload);
            break;
        case Zone::PacketId::MailDelAck:
            HandleMailDelAck(payload);
            break;
        case Zone::PacketId::Move:
        case Zone::PacketId::Chat:
            HandleBroadcastPacket();
            break;
        default:
            break;
        }
    }

    void StressSession::OnClosed(const std::shared_ptr<Network::Session>& /*session*/, const std::error_code& /*reason*/)
    {
        stats_.RecordSessionEnded();
    }

    void StressSession::HandleEnterZoneNotify(const std::span<const byte> /*payload*/)
    {
        MarkProgress();
        if (isBroadcaster_)
        {
            StartBroadcastTimer();
        }
        SendMailAdd();
    }

    void StressSession::HandleMailAddAck(const std::span<const byte> payload)
    {
        Zone::MailAddAckPacket ack{};
        if (payload.size() < sizeof(ack))
        {
            return;
        }
        std::memcpy(&ack, payload.data(), sizeof(ack));

        stats_.RecordMailAddAcked();
        if (mailAddSentAt_.time_since_epoch().count() != 0)
        {
            stats_.RecordMailAddLatency(ElapsedUs(mailAddSentAt_));
        }
        lastAddedMailId_ = ack.mailId;
        MarkProgress();

        SendMailDel(lastAddedMailId_);
    }

    void StressSession::HandleMailDelAck(const std::span<const byte> payload)
    {
        Zone::MailDelAckPacket ack{};
        if (payload.size() < sizeof(ack))
        {
            return;
        }
        std::memcpy(&ack, payload.data(), sizeof(ack));

        stats_.RecordMailDelAcked();
        if (mailDelSentAt_.time_since_epoch().count() != 0)
        {
            stats_.RecordMailDelLatency(ElapsedUs(mailDelSentAt_));
        }
        if (cycleStartedAt_.time_since_epoch().count() != 0)
        {
            stats_.RecordCycleLatency(ElapsedUs(cycleStartedAt_));
        }
        MarkProgress();

        // 리포트용 식별자 -- 실제 TCP 세션 id가 아니라 이 도구가 부여한 인덱스다(Connector가
        // 자체적으로 매기는 SessionId는 세션마다 1부터 다시 시작해 전역 식별에 못 쓴다).
        const auto testSessionId = static_cast<Network::SessionId>(index_);
        if (ack.mailId != lastAddedMailId_ || !ack.success)
        {
            stats_.RecordMismatch(testSessionId, cycleIndex_, lastAddedMailId_, ack.mailId,
                                   !ack.success ? "delete reported failure" : "mailId mismatch");
        }
        else
        {
            stats_.RecordCycleCompleted();
        }

        ++cycleIndex_;
        if (cycleIndex_ < cyclesTarget_)
        {
            SendMailAdd();
        }
        else
        {
            done_.store(true, std::memory_order_release);
            stats_.RecordSessionDone();
        }
    }

    void StressSession::HandleBroadcastPacket()
    {
        stats_.RecordBroadcastReceived();
        // 여기서 MarkProgress()를 호출하지 않는다 -- 워치독은 "이 세션의 Mail 사이클이
        // 멈췄는가"를 보려는 것인데, 브로드캐스트 수신은 그와 무관한 신호다. 한 번이라도
        // 호출하면 다른 세션(브로드캐스터)이 주기적으로 쏘는 Move를 자기도 받을 때마다 진행
        // 시각이 계속 갱신되어, 정작 자기 Mail 사이클은 응답을 못 받고 멈춰 있어도 스톨로
        // 잡히지 않는 오탐(false negative)이 생긴다.
    }

    void StressSession::SendMailAdd()
    {
        if (!session_)
        {
            return;
        }

        Packet::BinaryWriter writer;
        writer.WriteString("lt-mail");
        writer.WriteString("stress-body");
        writer.Write(static_cast<int64_t>(3600));  // 테스트 도중 자동 만료로 뒤섞이지 않게 충분히 길게
        session_->SendPacket(static_cast<uint16_t>(Zone::PacketId::MailAdd), writer.GetBuffer());

        // 사이클(Add->AddAck->Del->DelAck)의 시작점도 여기다 -- Add 송신이 곧 사이클 시작.
        mailAddSentAt_ = std::chrono::steady_clock::now();
        cycleStartedAt_ = mailAddSentAt_;

        stats_.RecordMailAddSent();
        MarkProgress();
    }

    void StressSession::SendMailDel(const uint32_t mailId)
    {
        if (!session_)
        {
            return;
        }

        session_->SendPacket(static_cast<uint16_t>(Zone::PacketId::MailDel), std::as_bytes(std::span(&mailId, 1)));

        mailDelSentAt_ = std::chrono::steady_clock::now();

        stats_.RecordMailDelSent();
        MarkProgress();
    }

    uint64_t StressSession::ElapsedUs(const std::chrono::steady_clock::time_point since) noexcept
    {
        const auto delta = std::chrono::steady_clock::now() - since;
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(delta).count();
        return us > 0 ? static_cast<uint64_t>(us) : 0;
    }

    void StressSession::StartBroadcastTimer()
    {
        broadcastTimer_ = std::make_unique<Timer::RepeatingTimer>(ioContext_);
        broadcastTimer_->Start(std::chrono::milliseconds(2500), [this]
        {
            if (!session_)
            {
                return;
            }

            // zone 0의 담당 구간 [0,10) 안에서만 움직여서 핸드오프가 안 생기게 한다(핸드오프
            // 자체는 이번 부하 테스트 범위 밖).
            Zone::MovePacket move{};
            move.x = 1.0f + static_cast<float>(index_ % 8);
            move.y = 0.0f;
            session_->SendPacket(static_cast<uint16_t>(Zone::PacketId::Move), std::as_bytes(std::span(&move, 1)));

            stats_.RecordBroadcastSent(stats_.ActiveSessionCount());
        });
    }

    void StressSession::MarkProgress() noexcept
    {
        lastProgressAtTicks_.store(std::chrono::steady_clock::now().time_since_epoch().count(),
                                    std::memory_order_relaxed);
    }

    std::chrono::steady_clock::time_point StressSession::LastProgressAt() const noexcept
    {
        return std::chrono::steady_clock::time_point(
            std::chrono::steady_clock::duration(lastProgressAtTicks_.load(std::memory_order_relaxed)));
    }
}
