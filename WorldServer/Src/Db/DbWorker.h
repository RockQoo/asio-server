#pragma once

#include "Core/Src/Thread/WorkerThread.h"

#include <cstddef>
#include <utility>

namespace Db
{
    // DB 처리 전용 로직 스레드 하나. Thread::AffinityWorkerPool<DbWorker>에 꽂혀서
    // owner-id(clientSessionId) 해시로 고정 배정된다 -- 같은 플레이어의 UnitOfWork 태스크는
    // 항상 같은 DbWorker에서 순서대로 처리되므로 락이 필요 없다(TaskWorker와 동일한
    // owner-hash 생산자-소비자 원리). 실제 DB 연결/쿼리는
    // Docker DB 연동 후 구현 예정이라 지금은 로그로만 처리 내용을 남긴다.
    class DbWorker
    {
    public:
        explicit DbWorker(const size_t index);

        void Start();
        void Stop();

        template <typename F>
        void PostTask(F&& task)
        {
            worker_.PostTask(std::forward<F>(task));
        }

    private:
        size_t index_;
        Thread::WorkerThread worker_;
    };
}
