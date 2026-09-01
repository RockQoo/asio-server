#include "Core/Src/pch.h"
#include "Core/Src/Thread/WorkerThread.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Thread
{
    WorkerThread::WorkerThread(std::string name)
        : name_(std::move(name))
    {
    }

    WorkerThread::~WorkerThread()
    {
        Stop();
    }

    void WorkerThread::Start()
    {
        if (running_.exchange(true))
        {
            return;
        }

        stopping_ = false;
        thread_ = std::make_unique<std::thread>([this] { Run(); });
    }

    void WorkerThread::Stop()
    {
        if (!running_.load())
        {
            return;
        }

        stopping_ = true;
        condition_.notify_all();

        if (thread_ && thread_->joinable())
        {
            thread_->join();
        }

        running_ = false;
    }

    void WorkerThread::PostTask(Task task)
    {
        {
            std::lock_guard lock(mutex_);
            taskQueue_.push(std::move(task));
        }
        condition_.notify_one();
    }

    void WorkerThread::SetAffinity([[maybe_unused]] const size_t cpuIndex) const
    {
#ifdef _WIN32
        if (thread_ && thread_->joinable())
        {
            constexpr auto kBitsPerMask = sizeof(DWORD_PTR) * 8;
            const auto mask = static_cast<DWORD_PTR>(1ull << (cpuIndex % kBitsPerMask));
            ::SetThreadAffinityMask(thread_->native_handle(), mask);
        }
#endif
    }

    size_t WorkerThread::QueueSize() const
    {
        std::lock_guard lock(mutex_);
        return taskQueue_.size();
    }

    void WorkerThread::Run()
    {
        while (!stopping_.load(std::memory_order_relaxed))
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return !taskQueue_.empty() || stopping_.load(); });

            while (!taskQueue_.empty())
            {
                auto task = std::move(taskQueue_.front());
                taskQueue_.pop();
                lock.unlock();

                try
                {
                    task();
                }
                catch (const std::exception& ex)
                {
                    LOG.Error(ELogCategory::Thread, "작업 실행 중 예외")
                        .KV("Worker", name_)
                        .KV("Exception", ex.what());
                }

                lock.lock();
            }
        }
    }
}
