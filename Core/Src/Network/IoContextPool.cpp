#include "Core/Src/pch.h"
#include "Core/Src/Network/IoContextPool.h"

#include "Core/Src/Common/CoreException.h"

namespace Network
{
    IoContextPool::IoContextPool(const size_t size)
    {
        if (size == 0)
        {
            throw Common::CoreException(Common::EErrorCode::InvalidArgument,
                                         "IoContextPool: size는 0보다 커야 한다");
        }

        contexts_.reserve(size);
        workGuards_.reserve(size);

        for (size_t i = 0; i < size; ++i)
        {
            contexts_.push_back(std::make_unique<asio::io_context>());
            workGuards_.push_back(asio::make_work_guard(*contexts_.back()));
        }
    }

    IoContextPool::~IoContextPool()
    {
        Stop();
        Join();
    }

    void IoContextPool::Run()
    {
        threads_.reserve(contexts_.size());
        for (const auto& context : contexts_)
        {
            // 원시 포인터를 값으로 캡처한다. range-for의 루프 변수 자체를 캡처하면 안 된다:
            // `context`는 반복마다 다시 바인딩되므로 루프를 벗어나는 순간 댕글링된다.
            auto* const rawContext = context.get();
            threads_.emplace_back([rawContext] { rawContext->run(); });
        }
    }

    void IoContextPool::Stop()
    {
        workGuards_.clear();
        for (const auto& context : contexts_)
        {
            context->stop();
        }
    }

    void IoContextPool::Join()
    {
        for (auto& thread : threads_)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
    }

    asio::io_context& IoContextPool::Next() noexcept
    {
        const auto index = roundRobinIndex_.fetch_add(1, std::memory_order_relaxed) % contexts_.size();
        return *contexts_[index];
    }

    asio::io_context& IoContextPool::At(const size_t index) noexcept
    {
        return *contexts_[index % contexts_.size()];
    }
}
