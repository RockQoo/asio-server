#pragma once

#include <array>
#include <cstdint>
#include <shared_mutex>
#include <utility>

namespace Threading
{
    inline constexpr int32_t kMaxLockDepth = 32;

    // 스레드 하나가 "지금 재진입 중인" Synchronized 잠금 목록. thread_local이라 스레드마다 독립
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
                LOG.Error(ELogCategory::General, "Synchronized 재진입 중 shared -> unique 승급 시도(금지됨)");
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

    // 여러 스레드가 "우연히" 같은 객체를 건드릴 수 있는 예외적인 지점만 보호하려고 만든
    // 래퍼다. 존(zone) 로직 스레드 전용 상태(PlayerState 등)는 애초에 다른 스레드가 절대
    // 안 건드리므로 이 클래스가 필요 없다 -- 이건 그 불변식이 깨지는 자리(예: zone 워커가
    // 아닌 별도 유지보수 타이머 스레드가 zone 상태를 직접 만지는 경우)에서만 쓴다.
    //
    // 콘텐츠 클래스는 보통 `using ARef = Threading::Synchronized<MyClass>;`를 자기 안에 선언해
    // 두고 `MyClass::ARef`로 짧게 쓴다(MailModel 참고).
    //
    // 접근 방법 두 가지:
    //   - `ref->Foo()` : 최상위 operator->()는 항상 읽기(shared_lock) 접근. 가장 흔한 경우가
    //     읽기라서 기본 문법을 여기에 배정했다.
    //   - `ref.Write()->Foo()` : 쓰기(unique_lock) 접근. 임시 프록시가 만들어졌다가 그 한 번의
    //     호출이 끝나는 즉시 소멸하면서 잠금도 풀린다.
    // 여러 호출에 걸쳐 같은 잠금을 유지해야 하는 드문 경우(예: 만료 대상을 조회한 뒤 그
    // 결과로 바로 삭제까지 이어가야 해서 그 사이 다른 스레드가 끼어들면 안 되는 시퀀스)에는
    // 프록시를 named 변수로 받는다(`auto writer = ref.Write();`) -- 프록시가 그 변수의 수명
    // 동안 살아 있으므로 잠금도 그만큼 유지된다. `Read()`도 같은 이유로 쌍을 이룬다.
    template <typename T>
    class Synchronized
    {
    public:
        template <typename... Args>
        explicit Synchronized(Args&&... args)
            : mutex_()
            , value_(std::forward<Args>(args)...)
        {
        }

        class ReadProxy
        {
        public:
            ReadProxy(const T& value, std::shared_mutex& mutex)
                : value_(value)
                , lockedMutex_(t_recursionGuard.LockShared(mutex))
            {
            }

            ReadProxy(const ReadProxy&) = delete;
            ReadProxy& operator=(const ReadProxy&) = delete;
            ReadProxy(ReadProxy&&) = delete;
            ReadProxy& operator=(ReadProxy&&) = delete;

            ~ReadProxy() { t_recursionGuard.UnlockShared(lockedMutex_); }

            [[nodiscard]] const T* operator->() const noexcept { return &value_; }
            [[nodiscard]] const T& operator*() const noexcept { return value_; }

        private:
            const T& value_;
            std::shared_mutex* lockedMutex_;
        };

        class WriteProxy
        {
        public:
            WriteProxy(T& value, std::shared_mutex& mutex)
                : value_(value)
                , lockedMutex_(t_recursionGuard.LockUnique(mutex))
            {
            }

            WriteProxy(const WriteProxy&) = delete;
            WriteProxy& operator=(const WriteProxy&) = delete;
            WriteProxy(WriteProxy&&) = delete;
            WriteProxy& operator=(WriteProxy&&) = delete;

            ~WriteProxy() { t_recursionGuard.UnlockUnique(lockedMutex_); }

            [[nodiscard]] T* operator->() const noexcept { return &value_; }
            [[nodiscard]] T& operator*() const noexcept { return value_; }

        private:
            T& value_;
            std::shared_mutex* lockedMutex_;
        };

        [[nodiscard]] ReadProxy operator->() const { return ReadProxy(value_, mutex_); }

        // 프록시는 복사도 이동도 막아뒀지만, C++17의 보장된 복사 생략 덕분에 값으로 반환해도
        // 호출부에서 `auto writer = ref.Write();`처럼 named 변수로 받을 수 있다.
        [[nodiscard]] ReadProxy Read() const { return ReadProxy(value_, mutex_); }
        [[nodiscard]] WriteProxy Write() { return WriteProxy(value_, mutex_); }

    private:
        mutable std::shared_mutex mutex_;
        T value_;
    };
}
