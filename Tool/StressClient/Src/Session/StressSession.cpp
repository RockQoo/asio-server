#include "Tool/StressClient/Src/pch.h"
#include "Tool/StressClient/Src/Session/StressSession.h"

#include "Shared/Core/Src/Common/RequestId.h"
#include "Shared/Core/Src/Packet/BinaryReader.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Shared/Protocol/Src/TaskKind.h"
#include "Server/ZoneServer/Src/Packet/ZonePackets.h"

#include <optional>
#include <utility>

namespace Stress
{
    namespace
    {
        // Z2CTaskResult에 실려 온 UnitOfWork 태스크 스트림에서 원하는 Mail 태스크의 mailId를
        // 꺼낸다. 스트림 포맷은 Shared/Core/Src/Task/UnitOfWork.h 주석 참고 --
        // 부하 도구라 첫 번째로 맞는 태스크 하나만 보면 충분하다.
        [[nodiscard]] std::optional<uint32_t> FindMailTaskId(const std::span<const byte> stream,
                                                             const Protocol::EMailTask subTask)
        {
            Packet::BinaryReader reader(stream);
            uint64_t ownerId{};
            uint16_t taskCount{};
            if (!reader.Read(ownerId) || !reader.Read(taskCount))
            {
                return std::nullopt;
            }

            for (uint16_t i = 0; i < taskCount; ++i)
            {
                uint16_t kind{};
                uint32_t payloadLen{};
                if (!reader.Read(kind) || !reader.Read(payloadLen))
                {
                    return std::nullopt;
                }

                const auto taskPayload = reader.ReadBytes(payloadLen);
                if (!taskPayload)
                {
                    return std::nullopt;
                }

                if (Protocol::CategoryOf(kind) != Protocol::ETaskCategory::Mail
                    || Protocol::SubTaskOf(kind) != static_cast<uint8_t>(subTask))
                {
                    continue;
                }

                Packet::BinaryReader mailReader(*taskPayload);
                uint32_t mailId{};
                if (!mailReader.Read(mailId))
                {
                    return std::nullopt;
                }
                return mailId;
            }

            return std::nullopt;
        }
    }

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
        switch (static_cast<PacketId>(header.id))
        {
        case PacketId::Z2CEnterZoneNotify:
            HandleEnterZoneNotify(payload);
            break;
        case PacketId::Z2CTaskResult:
            HandleTaskResult(payload);
            break;
        case PacketId::Z2CMoveNotify:
        case PacketId::Z2CChatNotify:
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

    void StressSession::HandleTaskResult(const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        int32_t errorCode{};
        uint16_t requestPacketId{};

        // requestId는 성공/실패 어느 쪽이든 실린다 -- 태스크 스트림 앞에 있으므로 여기서
        // 읽어서 넘겨야 그 뒤 스트림 오프셋이 맞는다. 부하 도구는 값 자체를 쓰지 않지만,
        // 건너뛰지 않으면 스트림을 8바이트 밀려서 파싱해 mailId를 못 찾는다.
        Common::RequestId requestId{};
        if (!reader.Read(errorCode) || !reader.Read(requestPacketId) || !reader.Read(requestId))
        {
            return;
        }

        // 남은 바이트가 UnitOfWork 태스크 스트림이다(실패면 비어 있다).
        const auto stream = reader.RemainingBytes();
        switch (static_cast<PacketId>(requestPacketId))
        {
        case PacketId::C2ZMailAdd:
            HandleMailAddResult(errorCode, stream);
            break;
        case PacketId::C2ZMailDel:
            HandleMailDelResult(errorCode, stream);
            break;
        default:
            // requestPacketId=0(서버가 스스로 만든 변경, 예: 메일 만료)은 이 도구의 사이클과
            // 무관하다 -- 부하 테스트는 만료 시간을 길게 잡아 만료가 끼어들지 않게 한다.
            break;
        }
    }

    void StressSession::HandleMailAddResult(const int32_t errorCode, const std::span<const byte> stream)
    {
        const auto testSessionId = static_cast<Network::SessionId>(index_);
        const auto addedMailId = FindMailTaskId(stream, Protocol::EMailTask::Added);
        if (errorCode != 0 || !addedMailId)
        {
            stats_.RecordMismatch(testSessionId, cycleIndex_, 0, 0, "mail add failed");
            MarkProgress();
            return;
        }

        stats_.RecordMailAddAcked();
        if (mailAddSentAt_.time_since_epoch().count() != 0)
        {
            stats_.RecordMailAddLatency(ElapsedUs(mailAddSentAt_));
        }
        lastAddedMailId_ = *addedMailId;
        MarkProgress();

        SendMailDel(lastAddedMailId_);
    }

    void StressSession::HandleMailDelResult(const int32_t errorCode, const std::span<const byte> stream)
    {
        const auto removedMailId = FindMailTaskId(stream, Protocol::EMailTask::Removed);

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
        if (errorCode != 0 || !removedMailId)
        {
            stats_.RecordMismatch(testSessionId, cycleIndex_, lastAddedMailId_, 0,
                                   "delete reported failure");
        }
        else if (*removedMailId != lastAddedMailId_)
        {
            stats_.RecordMismatch(testSessionId, cycleIndex_, lastAddedMailId_, *removedMailId,
                                   "mailId mismatch");
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
        session_->SendPacket(PacketId::C2ZMailAdd, writer.GetBuffer());

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

        session_->SendPacket(PacketId::C2ZMailDel, std::as_bytes(std::span(&mailId, 1)));

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
            session_->SendPacket(PacketId::C2ZMove, std::as_bytes(std::span(&move, 1)));

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
