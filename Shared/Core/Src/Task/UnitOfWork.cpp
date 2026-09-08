#include "Shared/Core/Src/pch.h"
#include "Shared/Core/Src/Task/UnitOfWork.h"

#include "Shared/Core/Src/Packet/BinaryWriter.h"

#include <exception>
#include <utility>

namespace Task
{
    UnitOfWork::UnitOfWork(const uint64_t ownerId, const Common::RequestId requestId)
        : ownerId_(ownerId)
        , requestId_(requestId)
    {
    }

    void UnitOfWork::AddTask(std::unique_ptr<ITask> task)
    {
        if (!task)
        {
            return;
        }

        tasks_.push_back(std::move(task));
    }

    std::vector<byte> UnitOfWork::Serialize() const
    {
        if (tasks_.empty())
        {
            return {};
        }

        Packet::BinaryWriter writer;
        writer.Write(ownerId_);
        writer.Write(static_cast<uint16_t>(tasks_.size()));

        for (const auto& task : tasks_)
        {
            // 태스크마다 따로 직렬화한 뒤 그 길이를 앞에 붙인다 -- 길이를 미리 알 수 없어서
            // 한 버퍼에 이어 쓸 수 없다(BinaryWriter는 되돌아가 덮어쓰지 않는다).
            Packet::BinaryWriter taskWriter;
            task->Serialize(taskWriter);
            const auto& payload = taskWriter.GetBuffer();

            writer.Write(task->Kind());
            writer.Write(static_cast<uint32_t>(payload.size()));
            writer.WriteBytes(payload);
        }

        const auto& buffer = writer.GetBuffer();
        return std::vector<byte>(buffer.begin(), buffer.end());
    }

    void UnitOfWork::RollbackAll() noexcept
    {
        try
        {
            // 되돌리는 과정에서 쌓이는 태스크를 받아 버리는 통. 이게 없으면 정상 함수를
            // 재사용할 수 없다(ITask::Rollback 주석 참고).
            RollbackUnitOfWork sink;

            for (auto it = tasks_.rbegin(); it != tasks_.rend(); ++it)
            {
                (*it)->Rollback(sink);
            }
        }
        catch (const std::exception& ex)
        {
            // 롤백은 조금 전에 성공한 변경을 되돌리는 것뿐이라 실패할 수 없다는 전제다. 여기
            // 걸렸다면 그 전제가 깨진 것이고(모델이 롤백 경로에 새 검증을 넣었다는 뜻),
            // 복구를 시도해도 더 나빠지기만 하므로 드러내기만 한다.
            LOG.Error(ELogCategory::General, "UnitOfWork 롤백 중 예외 -- 메모리와 DB가 어긋난다")
                .KV("OwnerId", ownerId_).KV("TaskCount", tasks_.size()).KV("What", ex.what());
        }
        catch (...)
        {
            LOG.Error(ELogCategory::General, "UnitOfWork 롤백 중 알 수 없는 예외")
                .KV("OwnerId", ownerId_).KV("TaskCount", tasks_.size());
        }

        tasks_.clear();
    }
}
