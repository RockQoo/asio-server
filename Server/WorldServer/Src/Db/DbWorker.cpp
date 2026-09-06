#include "Server/WorldServer/Src/pch.h"
#include "Server/WorldServer/Src/Db/DbWorker.h"

namespace Db
{
    DbWorker::DbWorker(const size_t index)
        : index_(index)
        , worker_("DbWorker-" + std::to_string(index))
    {
    }

    void DbWorker::Start()
    {
        worker_.Start();
    }

    void DbWorker::Stop()
    {
        worker_.Stop();
    }
}
