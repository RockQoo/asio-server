#include "ZoneServer/Src/pch.h"
#include "ZoneServer/Src/Worker/TaskWorker.h"

#include <string>

namespace Zone
{
    TaskWorker::TaskWorker(const size_t index)
        : index_(index)
        , worker_("TaskWorker-" + std::to_string(index))
    {
    }

    void TaskWorker::Start()
    {
        worker_.Start();
        worker_.SetAffinity(index_);
    }

    void TaskWorker::Stop()
    {
        worker_.Stop();
    }
}
