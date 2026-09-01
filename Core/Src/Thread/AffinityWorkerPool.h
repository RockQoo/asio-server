#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace Thread
{
    // 상속이 아니라 구조적 제약(concept)이라서, Start()/Stop()만 노출하면 어떤 워커 타입이든
    // 이 풀에 담을 수 있다. ZoneServer의 TaskWorker는 조합(composition)으로 이 요건을 만족시킨다.
    template <typename TWorker>
    concept StartableWorker = requires(TWorker& worker)
    {
        worker.Start();
        worker.Stop();
    };

    // TWorker 인스턴스를 workerCount개 소유하며, 각각 TWorker(index, args...)로 생성한다.
    // 조회 키를 `key % workerCount`로 고정된 워커에 매핑한다. 이것이 바로 키별 상태에 락 없이
    // 접근할 수 있게 해주는, 고정된 "키 -> 스레드" 매핑이다 (예: 존(zone) id별로 CPU에 고정된
    // TaskWorker 하나씩).
    template <StartableWorker TWorker>
    class AffinityWorkerPool
    {
    public:
        template <typename... Args>
        explicit AffinityWorkerPool(const size_t workerCount, Args&... args)
        {
            workers_.reserve(workerCount);
            for (size_t index = 0; index < workerCount; ++index)
            {
                // `args`는 모든 워커가 공유하므로(예: SessionManager&), lvalue 참조로 바인딩하고
                // 이 루프 안에서는 의도적으로 forward/move하지 않는다.
                workers_.push_back(std::make_unique<TWorker>(index, args...));
            }
        }

        void Start()
        {
            for (const auto& worker : workers_)
            {
                worker->Start();
            }
        }

        void Stop()
        {
            for (const auto& worker : workers_)
            {
                worker->Stop();
            }
        }

        [[nodiscard]] TWorker& GetWorker(const size_t key) noexcept
        {
            return *workers_[key % workers_.size()];
        }

        [[nodiscard]] size_t WorkerCount() const noexcept { return workers_.size(); }

        template <typename F>
        void ForEach(F&& func)
        {
            for (const auto& worker : workers_)
            {
                func(*worker);
            }
        }

    private:
        std::vector<std::unique_ptr<TWorker>> workers_;
    };
}
