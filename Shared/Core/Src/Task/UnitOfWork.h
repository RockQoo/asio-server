#pragma once

#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace Task
{
    // 기록한 태스크가 어디로 나가야 하는지. 대부분의 변경은 DB에도 남아야 하고 클라이언트
    // 메모리에도 적용돼야 해서 Both가 기본값이지만, 한쪽에만 의미가 있는 변경(내부 기록만,
    // 혹은 연출성 통지만)을 위해 나눠 둔다.
    enum class ETaskTarget : uint8_t
    {
        Db = 1 << 0,
        Client = 1 << 1,
        Both = Db | Client,
    };

    // owner 하나의 상태 변경을 한 요청 스코프 동안 태스크로 누적했다가, Commit() 시점에
    // 한 번에 내보내는 범용 Unit-of-Work의 기반 클래스.
    //
    // Core는 태스크의 실제 의미(메일 추가/삭제, 재화 지급 등)를 몰라야 하므로 이 클래스가
    // 다루는 건 taskKind(uint16_t)와 이미 직렬화된 바이트뿐이다. 실제 전송/역연산은 콘텐츠
    // 계층이 파생 클래스에서 구현한다(ZoneServer의 Zone::ZoneUnitOfWork).
    //
    // **왜 소멸자가 아니라 명시적 Commit()인가**: 기반 클래스 소멸자가 도는 시점에는 파생
    // 부분이 이미 파괴돼 있어서 가상 함수가 파생 구현으로 불리지 않는다. 소멸자에서
    // 내보내면 파생이 담당하는 전송/롤백이 조용히 실행되지 않는다. 그래서 내보내기는 전부
    // Commit()에서 하고, 소멸자는 "Commit 없이 스코프를 벗어났다"를 잡는 안전망만 맡는다.
    //
    // 롤백은 기록해둔 태스크를 역순으로 되짚어 파생에게 역연산을 시키는 방식이라, **태스크
    // 페이로드에는 역연산에 필요한 정보가 들어 있어야 한다**(삭제 태스크가 지워진 원본
    // 전체를 싣는 이유, 증감 태스크라면 결과값이 아니라 증감량을 실어야 하는 이유).
    class UnitOfWork
    {
    public:
        explicit UnitOfWork(const uint64_t ownerId);

        UnitOfWork(const UnitOfWork&) = delete;
        UnitOfWork& operator=(const UnitOfWork&) = delete;
        UnitOfWork(UnitOfWork&&) = delete;
        UnitOfWork& operator=(UnitOfWork&&) = delete;

        // taskKind: 콘텐츠 계층이 정의하는 값(Protocol::MakeTaskKind). payload: 이미
        // BinaryWriter 등으로 직렬화된 바이트.
        void RecordTask(const uint16_t taskKind, const std::span<const byte> payload,
                        const ETaskTarget target = ETaskTarget::Both);

        // 콘텐츠 계층이 정의하는 에러 코드(Protocol::EErrorCode)를 받는다 -- Core는 이 정수의
        // 의미를 모르고, 0이 아니면 실패로만 취급한다(taskKind와 같은 이유).
        // 이미 에러가 설정돼 있으면 덮어쓰지 않는다: 처음 난 실패가 진짜 원인이고, 그 뒤는
        // 그것 때문에 연쇄로 실패한 것일 가능성이 높다.
        template <typename TErrorCode>
            requires std::is_enum_v<TErrorCode>
        void SetError(const TErrorCode errorCode) noexcept
        {
            if (errorCode_ == 0)
            {
                errorCode_ = static_cast<int32_t>(errorCode);
            }
        }

        [[nodiscard]] bool HasError() const noexcept { return errorCode_ != 0; }
        [[nodiscard]] int32_t GetError() const noexcept { return errorCode_; }
        [[nodiscard]] bool IsEmpty() const noexcept { return tasks_.empty(); }

        // 요청 스코프가 끝나는 자리에서 정확히 한 번 호출한다.
        //   에러 없음 -> 대상별로 직렬화해서 OnFlush
        //   에러 있음 -> 기록의 역순으로 OnRollback, 그 다음 OnFailed
        void Commit();

    protected:
        // 다형적으로 삭제할 일이 없는(항상 스택에 두는) 타입이라 가상 소멸자를 두지 않는다.
        // protected라 기반 포인터로 delete하는 것 자체가 컴파일되지 않는다.
        ~UnitOfWork();

        // 그 대상으로 기록된 태스크가 하나라도 있을 때만 대상별로 한 번씩 불린다.
        // stream 와이어 포맷: ownerId(8) + taskCount(2) + taskCount개의 {kind(2) + len(4) + payload}.
        // 길이 프리픽스를 두는 이유: 읽는 쪽이 그 kind를 아직 모르더라도(새 콘텐츠 태스크가
        // 추가되기 전 빌드) 통째로 건너뛰고 다음 태스크로 넘어갈 수 있게 하기 위함.
        virtual void OnFlush(const ETaskTarget target, const std::span<const byte> stream) = 0;

        // 실패 시 기록의 역순으로 태스크마다 불린다 -- 나중에 일어난 변경부터 되돌려야
        // 중간 상태를 거치지 않는다. 파생은 taskKind로 어느 모델의 어떤 역연산인지 고른다.
        // **역연산은 UnitOfWork에 다시 기록하면 안 된다**(모델의 Undo 전용 함수를 쓴다).
        virtual void OnRollback(const uint16_t taskKind, const std::span<const byte> payload) = 0;

        // 롤백이 끝난 뒤 한 번. 요청자에게 실패를 알리는 자리.
        virtual void OnFailed(const int32_t errorCode) = 0;

        [[nodiscard]] uint64_t GetOwnerId() const noexcept { return ownerId_; }

    private:
        [[nodiscard]] std::vector<byte> Serialize(const ETaskTarget target) const;
        void FlushTo(const ETaskTarget target);

        struct TaskRecord
        {
            uint16_t kind;
            ETaskTarget target;
            std::vector<byte> payload;
        };

        uint64_t ownerId_;
        int32_t errorCode_{};
        bool committed_{};
        std::vector<TaskRecord> tasks_;
    };
}
