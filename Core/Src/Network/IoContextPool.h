#pragma once

#include <asio.hpp>

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

namespace Network
{
    // io_context를 N개 소유하고, 각각을 전용 I/O 스레드로 돌린다. run()이 할 일이 없을 때
    // 바로 반환해버리지 않도록 work guard도 함께 들고 있다. Next()를 호출하면 라운드로빈으로
    // 세션이 분배되어, 여러 I/O 스레드에 소켓 read/write 완료 처리가 고르게 퍼진다.
    class IoContextPool
    {
    public:
        explicit IoContextPool(const size_t size);
        ~IoContextPool();

        IoContextPool(const IoContextPool&) = delete;
        IoContextPool& operator=(const IoContextPool&) = delete;

        void Run();
        void Stop();
        void Join();

        [[nodiscard]] asio::io_context& Next() noexcept;
        [[nodiscard]] asio::io_context& At(const size_t index) noexcept;
        [[nodiscard]] size_t Size() const noexcept { return contexts_.size(); }

    private:
        std::vector<std::unique_ptr<asio::io_context>> contexts_;
        std::vector<asio::executor_work_guard<asio::io_context::executor_type>> workGuards_;
        std::vector<std::thread> threads_;
        std::atomic<size_t> roundRobinIndex_{0};
    };
}
