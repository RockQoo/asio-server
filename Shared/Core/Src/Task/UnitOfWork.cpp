#include "Shared/Core/Src/pch.h"
#include "Shared/Core/Src/Task/UnitOfWork.h"

#include "Shared/Core/Src/Common/EnumFlags.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"

#include <cstdlib>
#include <exception>

namespace Task
{
    UnitOfWork::UnitOfWork(const uint64_t ownerId)
        : ownerId_(ownerId)
    {
    }

    UnitOfWork::~UnitOfWork()
    {
        if (committed_ || tasks_.empty())
        {
            return;
        }

        // 여기까지 왔다는 건 기록된 변경이 DB로도 클라이언트로도 나가지 못했다는 뜻이다.
        // 조용히 넘기면 "메모리만 바뀌고 DB에는 없는" 상태가 되어 나중에 원인을 찾기가
        // 극도로 어려워지므로 바로 드러낸다(Thread::Mutexed의 승급 금지와 같은 판단).
        LOG.Error(ELogCategory::General, "Commit 없이 소멸한 UnitOfWork -- 기록된 변경이 유실된다")
            .KV("OwnerId", ownerId_).KV("TaskCount", tasks_.size());

        // 단, 예외가 전파되는 중이라면 죽이지 않는다 -- 여기서 abort하면 진짜 원인인 그 예외가
        // 묻힌다. 이 경로에서는 파생 구현을 부를 수 없어(소멸 중) 메모리 롤백도 못 하므로,
        // 예외를 던질 수 있는 구간은 UnitOfWork 스코프 밖에 두는 것이 원칙이다.
        if (std::uncaught_exceptions() > 0)
        {
            return;
        }

        std::abort();
    }

    void UnitOfWork::RecordTask(const uint16_t taskKind, const std::span<const byte> payload,
                                const ETaskTarget target)
    {
        tasks_.push_back(TaskRecord{taskKind, target, std::vector<byte>(payload.begin(), payload.end())});
    }

    void UnitOfWork::Commit()
    {
        // 실패로 끝나는 것도 "처리가 끝났다"는 뜻이라 소멸자 안전망은 통과시킨다.
        committed_ = true;

        if (HasError())
        {
            for (auto it = tasks_.rbegin(); it != tasks_.rend(); ++it)
            {
                OnRollback(it->kind, it->payload);
            }
            tasks_.clear();

            OnFailed(errorCode_);
            return;
        }

        FlushTo(ETaskTarget::Db);
        FlushTo(ETaskTarget::Client);
    }

    void UnitOfWork::FlushTo(const ETaskTarget target)
    {
        const auto serialized = Serialize(target);
        if (serialized.empty())
        {
            return;
        }

        OnFlush(target, serialized);
    }

    std::vector<byte> UnitOfWork::Serialize(const ETaskTarget target) const
    {
        uint16_t taskCount = 0;
        for (const auto& task : tasks_)
        {
            if (Common::HasFlag(task.target, target))
            {
                ++taskCount;
            }
        }

        if (taskCount == 0)
        {
            return {};
        }

        Packet::BinaryWriter writer;
        writer.Write(ownerId_);
        writer.Write(taskCount);

        for (const auto& task : tasks_)
        {
            if (!Common::HasFlag(task.target, target))
            {
                continue;
            }

            writer.Write(task.kind);
            writer.Write(static_cast<uint32_t>(task.payload.size()));
            writer.WriteBytes(task.payload);
        }

        const auto buffer = writer.GetBuffer();
        return std::vector<byte>(buffer.begin(), buffer.end());
    }
}
