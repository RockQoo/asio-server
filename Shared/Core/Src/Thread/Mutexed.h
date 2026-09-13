#pragma once

#include <array>
#include <cstdint>
#include <shared_mutex>
#include <utility>

namespace Thread
{
    inline constexpr int32_t kMaxLockDepth = 32;

    // 스레드 하나가 "지금 재진입 중인" Mutexed 잠금 목록. thread_local이라 스레드마다 독립
    // 이며, 스코프 진입/이탈(RAII)과 함께 스택처럼 push/pop된다. 이 스택 덕분에:
    //   - 같은 스레드가 이미 잠근 mutex를 다시 요청하면(재귀 호출 등) 실제로 다시 잠그지
    //     않고 스킵한다(그러지 않으면 shared_mutex는 재귀 lock_shared/lock에서 자기 자신과
    //     데드락난다).
    //   - 이미 unique(쓰기)로 갖고 있는 상태에서 shared(읽기)를 요청하면 스킵(이미 배타적
    //     접근 중이라 읽기는 항상 안전).
    //   - 반대로 이미 shared로 갖고 있는데 unique로 "승급"을 요청하면, 다른 스레드의
    //     shared_lock과 얽혀 데드락으로 이어질 수 있어 즉시 크래시시킨다(버그를 조용히
    //     넘기지 않고 바로 드러내기 위함).
    // 최대 32단계까지 추적하며, 이 프로젝트 규모에서 그 이상 중첩되는 경우는 없다고 가정한다.
    class RecursionGuard
    {
    public:
        // 반환값이 nullptr이면 "이미 이 스레드가 잠근 mutex라 실제로 잠그지 않았다"는 뜻이다
        // -- 짝이 되는 UnlockShared/UnlockUnique도 nullptr을 받으면 아무 것도 안 하므로
        // 호출 쪽은 그냥 반환값을 그대로 들고 있다가 넘기기만 하면 된다.
        [[nodiscard]] std::shared_mutex* LockShared(std::shared_mutex& mutex)
        {
            for (int32_t i = topIndex_; i >= 0; --i)
            {
                if (stack_.at(i).mutex == &mutex)
                {
                    return nullptr;
                }
            }

            mutex.lock_shared();
            stack_.at(++topIndex_) = Entry{&mutex, false};
            return &mutex;
        }

        [[nodiscard]] std::shared_mutex* LockUnique(std::shared_mutex& mutex)
        {
            for (int32_t i = topIndex_; i >= 0; --i)
            {
                if (stack_.at(i).mutex != &mutex)
                {
                    continue;
                }

                if (stack_.at(i).uniqueLocked)
                {
                    return nullptr;
                }

                // 이 헤더는 여러 프로젝트(Core/ZoneServer/...)에서 include되고, 프로젝트마다
                // 자기 ELogCategory를 따로 정의한다(cpp-patterns.md 참고) -- 전부가 공통으로
                // 갖는 General 카테고리로 남겨야 어디서 include되든 컴파일된다.
                LOG.Error(ELogCategory::General, "Mutexed 재진입 중 shared -> unique 승급 시도(금지됨)");
                std::abort();
            }

            mutex.lock();
            stack_.at(++topIndex_) = Entry{&mutex, true};
            return &mutex;
        }

        void UnlockShared(std::shared_mutex* mutex)
        {
            if (!mutex)
            {
                return;
            }
            mutex->unlock_shared();
            --topIndex_;
        }

        void UnlockUnique(std::shared_mutex* mutex)
        {
            if (!mutex)
            {
                return;
            }
            mutex->unlock();
            --topIndex_;
        }

    private:
        struct Entry
        {
            std::shared_mutex* mutex{};
            bool uniqueLocked{};
        };

        std::array<Entry, kMaxLockDepth> stack_{};
        int32_t topIndex_{-1};
    };

    inline thread_local RecursionGuard t_recursionGuard;

    // **어피니티로 못 막는 자리에서만** 쓰는 읽기/쓰기 락 래퍼. 레인 전용 상태는 다른
    // 스레드가 애초에 안 건드리므로 이 클래스가 필요 없다 -- 그 불변식이 깨지는 자리
    // (예: 유지보수 타이머가 존 상태를 직접 만지는 경우)에서만 쓴다.
    //
    //   ref->Foo()          읽기(shared_lock). 흔한 쪽이라 기본 문법을 배정했다
    //   ref.Write()->Foo()  쓰기(unique_lock). 임시 프록시라 그 호출이 끝나면 바로 풀린다
    //
    // **여러 호출에 걸쳐 같은 잠금을 유지해야 하면 프록시를 named 변수로 받는다**
    // (`auto writer = ref.Write();`) -- 조회한 결과로 바로 삭제까지 이어가야 해서 사이에
    // 다른 스레드가 끼어들면 안 되는 시퀀스가 그렇다. Read()도 같은 이유로 쌍을 이룬다.
    //
    // 설계 근거(네 가지 락 전략 중 어디에 쓰는가): docs/design/locking-strategy.md
    template <typename T>
    class Mutexed
    {
    public:
        template <typename... Args>
        explicit Mutexed(Args&&... args)
            : mutex_()
            , value_(std::forward<Args>(args)...)
        {
        }

        class ReadLock
        {
        public:
            ReadLock(const T& value, std::shared_mutex& mutex)
                : value_(value)
                , lockedMutex_(t_recursionGuard.LockShared(mutex))
            {
            }

            ReadLock(const ReadLock&) = delete;
            ReadLock& operator=(const ReadLock&) = delete;
            ReadLock(ReadLock&&) = delete;
            ReadLock& operator=(ReadLock&&) = delete;

            ~ReadLock() { t_recursionGuard.UnlockShared(lockedMutex_); }

            [[nodiscard]] const T* operator->() const noexcept { return &value_; }
            [[nodiscard]] const T& operator*() const noexcept { return value_; }

        private:
            const T& value_;
            std::shared_mutex* lockedMutex_;
        };

        class WriteLock
        {
        public:
            WriteLock(T& value, std::shared_mutex& mutex)
                : value_(value)
                , lockedMutex_(t_recursionGuard.LockUnique(mutex))
            {
            }

            WriteLock(const WriteLock&) = delete;
            WriteLock& operator=(const WriteLock&) = delete;
            WriteLock(WriteLock&&) = delete;
            WriteLock& operator=(WriteLock&&) = delete;

            ~WriteLock() { t_recursionGuard.UnlockUnique(lockedMutex_); }

            [[nodiscard]] T* operator->() const noexcept { return &value_; }
            [[nodiscard]] T& operator*() const noexcept { return value_; }

        private:
            T& value_;
            std::shared_mutex* lockedMutex_;
        };

        [[nodiscard]] ReadLock operator->() const { return ReadLock(value_, mutex_); }

        // 프록시는 복사도 이동도 막아뒀지만, C++17의 보장된 복사 생략 덕분에 값으로 반환해도
        // 호출부에서 `auto writer = ref.Write();`처럼 named 변수로 받을 수 있다.
        [[nodiscard]] ReadLock Read() const { return ReadLock(value_, mutex_); }
        [[nodiscard]] WriteLock Write() { return WriteLock(value_, mutex_); }

    private:
        mutable std::shared_mutex mutex_;
        T value_;
    };
}
