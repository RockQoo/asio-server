#include "pch.h"
#include "Server/Core/Src/Task/UnitOfWork.h"

namespace Task
{
    UnitOfWork::UnitOfWork(const uint64_t ownerId, const Base::RUID requestId)
        : ownerId_(ownerId)
        , requestId_(requestId)
    {
    }
}
