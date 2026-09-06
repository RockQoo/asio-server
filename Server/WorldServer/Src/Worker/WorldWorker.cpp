#include "Server/WorldServer/Src/pch.h"
#include "Server/WorldServer/Src/Worker/WorldWorker.h"

namespace World
{
    WorldWorker::WorldWorker()
        : worker_("WorldWorker")
    {
    }

    void WorldWorker::Start()
    {
        worker_.Start();
    }

    void WorldWorker::Stop()
    {
        worker_.Stop();
    }
}
