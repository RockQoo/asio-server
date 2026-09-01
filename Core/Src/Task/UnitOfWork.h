#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace Task
{
    // owner 하나의 상태 변경을 스코프 동안 태스크로 누적했다가, 스코프가 끝나는 시점
    // (소멸자)에 한 번에 직렬화해서 flush 콜백으로 넘기는 범용 Unit-of-Work. Core는 태스크의
    // 실제 의미(메일 추가/삭제, 재화 지급 등)를 몰라야 하므로, 태스크 종류(kind)는 호출자가
    // 정의하는 값이고 페이로드는 이미 직렬화된 바이트로만 받는다 -- 새 콘텐츠 태스크가
    // 생겨도 Core를 손댈 필요가 없다.
    class UnitOfWork
    {
    public:
        using FlushFunc = std::function<void(std::span<const byte>)>;

        UnitOfWork(const uint64_t ownerId, FlushFunc flush);
        ~UnitOfWork();

        UnitOfWork(const UnitOfWork&) = delete;
        UnitOfWork& operator=(const UnitOfWork&) = delete;

        // taskKind: 콘텐츠 계층이 정의하는 태스크 종류 값(예: Mail::MailTaskKind). payload:
        // 이미 BinaryWriter 등으로 직렬화된 바이트.
        void RecordTask(const uint16_t taskKind, const std::span<const byte> payload);

        [[nodiscard]] bool IsEmpty() const noexcept { return tasks_.empty(); }

    private:
        struct TaskRecord
        {
            uint16_t kind;
            std::vector<byte> payload;
        };

        uint64_t ownerId_;
        FlushFunc flush_;
        std::vector<TaskRecord> tasks_;
    };
}
