#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>

namespace Thread
{
    // FIFO 작업 큐를 소비하는 전용 스레드 하나. 이게 바로 "로직 스레드" 기본 단위다:
    // I/O 스레드(Network)는 게임 로직을 절대 직접 실행하지 않고, 대신 여기에
    // PostTask()로 맡긴다.
    class WorkerThread
    {
    public:
        using Task = std::function<void()>;

        explicit WorkerThread(std::string name);
        ~WorkerThread();

        WorkerThread(const WorkerThread&) = delete;
        WorkerThread& operator=(const WorkerThread&) = delete;

        void Start();
        void Stop();

        void PostTask(Task task);

        // 콜러블에 추가 인자를 함께 바인딩한다. 예: PostTask(&ZoneWorld::OnMove, &world, id, x, y).
        // 최소 1개 이상의 바인딩 인자를 요구하도록 만들어서, 단일 콜러블 호출은 항상 위의
        // PostTask(Task)로 해석되게 하고(중복 래핑 방지) 이 오버로드와 겹치지 않게 한다.
        template <typename F, typename Arg0, typename... Args>
        void PostTask(F&& func, Arg0&& arg0, Args&&... args)
        {
            PostTask(Task([f = std::forward<F>(func),
                           a0 = std::forward<Arg0>(arg0),
                           ... capturedArgs = std::forward<Args>(args)]() mutable
            {
                std::invoke(f, a0, capturedArgs...);
            }));
        }

        // Windows 전용: 이 스레드를 CPU 코어 하나에 고정한다 (인덱스는 마스크 비트 수(64)로 wrap됨).
        // 다른 플랫폼에서는 아무 동작도 하지 않는다.
        void SetAffinity(const size_t cpuIndex) const;

        [[nodiscard]] bool IsRunning() const noexcept { return running_.load(std::memory_order_acquire); }
        [[nodiscard]] size_t QueueSize() const;
        [[nodiscard]] const std::string& Name() const noexcept { return name_; }

    private:
        void Run();

        std::string name_;
        std::unique_ptr<std::thread> thread_;
        std::queue<Task> taskQueue_;
        mutable std::mutex mutex_;
        std::condition_variable condition_;
        std::atomic<bool> running_{false};
        std::atomic<bool> stopping_{false};
    };
}
