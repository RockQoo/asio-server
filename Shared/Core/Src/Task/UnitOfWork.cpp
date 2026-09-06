#include "Shared/Core/Src/pch.h"
#include "Shared/Core/Src/Task/UnitOfWork.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"

#include <utility>

namespace Task
{
    UnitOfWork::UnitOfWork(const uint64_t ownerId, FlushFunc flush)
        : ownerId_(ownerId)
        , flush_(std::move(flush))
    {
    }

    UnitOfWork::~UnitOfWork()
    {
        if (tasks_.empty())
        {
            return;
        }

        // 와이어: ownerId(8) + taskCount(2) + taskCount개의 {kind(2) + payloadLen(4) + payload}.
        // 길이 프리픽스를 두는 이유: 읽는 쪽이 이 kind를 아직 모르더라도(예: 새 콘텐츠 태스크가
        // 추가되기 전 빌드) 페이로드를 통째로 건너뛰고 다음 태스크로 넘어갈 수 있게 하기 위함.
        Packet::BinaryWriter writer;
        writer.Write(ownerId_);
        writer.Write(static_cast<uint16_t>(tasks_.size()));

        for (const auto& task : tasks_)
        {
            writer.Write(task.kind);
            writer.Write(static_cast<uint32_t>(task.payload.size()));
            writer.WriteBytes(task.payload);
        }

        if (flush_)
        {
            flush_(writer.GetBuffer());
        }
    }

    void UnitOfWork::RecordTask(const uint16_t taskKind, const std::span<const byte> payload)
    {
        tasks_.push_back(TaskRecord{taskKind, std::vector<byte>(payload.begin(), payload.end())});
    }
}
