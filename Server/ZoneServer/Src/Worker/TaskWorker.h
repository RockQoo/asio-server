#pragma once

#include "Shared/Core/Src/Thread/WorkerThread.h"

#include <cstddef>
#include <utility>

namespace Zone
{
    // 특정 존을 전용으로 소유하지 않는, 순수 실행기 역할의 범용 직렬 실행 스레드(풀에 속한 OS
    // 스레드 하나) -- 어떤 작업이 여기로 오는지는 이 클래스가 전혀 모르고, 상위(BASIC/TICK/
    // BROADCAST 풀을 쓰는 ZoneWorkerManager, LB 풀을 쓰는 WorldLinkHandler)가 자기 사정에 맞는
    // 키로 골라서 PostTask할 뿐이다. 그래서 존 개수와 이 워커 개수(풀 크기)는 서로 독립적으로
    // 설정할 수 있다.
    class TaskWorker
    {
    public:
        explicit TaskWorker(const size_t index);

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
