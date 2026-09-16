#include "pch.h"
#include "Shared/Core/Src/Task/UnitOfWork.h"

namespace Task
{
    UnitOfWork::UnitOfWork(const uint64_t ownerId, const Base::RUID requestId)
        : ownerId_(ownerId)
        , requestId_(requestId)
    {
    }
}
