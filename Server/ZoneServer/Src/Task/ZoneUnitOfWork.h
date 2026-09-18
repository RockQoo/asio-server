#pragma once

#include "Unit/Unit.h"

#include "Server/Core/Src/Base/RUID.h"
#include "Server/Core/Src/Base/Types.h"
#include "Server/Core/Src/Task/UnitOfWork.h"
#include "Server/Common/Src/Ids.h"
#include "Server/Common/Src/PacketId.h"

// 존 로직에서 요청 하나(= 패킷 하나)를 처리하는 동안의 트랜잭션 경계.
// 여러 모델(우편/재화 등)의 변경을 모았다가 **스코프를 벗어나는 순간** 결말을 낸다:
//   성공 -> 태스크 전량이 World로(Z2WUnitOfWorkStream), 같은 바이트가 그 클라이언트에게도
//           (Z2CTaskResult) 나간다. 클라이언트는 같은 태스크 목록을 자기 메모리에 적용하므로
//           서버와 상태가 어긋나지 않는다.
//   실패 -> 이미 반영된 메모리 변경을 역순으로 되돌리고, 클라이언트에는 에러 코드만 간다.
//
// **주인은 Unit 이고 포인터로 든다.** 되돌리는 방법을 아는 것은 모델을 가진 파생
// (Player)이라, 되돌릴 때 여기서 `ownerUnit_->RollbackUoW(*this)` 한 줄로 넘긴다 --
// 이 클래스가 "우편함이 있나 지갑이 있나"를 알 필요가 없어진다.
//
// **직렬화만 이 클래스가 안다.** Core의 Task::UnitOfWork는 목록과 순서만 들고 있고,
// 태스크를 바이트로 바꾸는 분기는 여기 있다 -- 그래야 Core가 게임을 모른다는 규칙이 선다.
//
// 커밋이 소멸자인 이유와 final로 닫는 이유는 Task::UnitOfWork 주석 참고.
class ZoneUnitOfWork final : public Task::UnitOfWork
{
public:
    // 클라이언트 요청을 처리하는 경우. **패킷 구조체를 그대로 받는다** -- id를 손으로 옮겨
    // 적으면 다른 패킷의 id를 넣는 실수가 컴파일을 통과하는데, 타입에서 뽑으면 그럴 수 없다.
    // 값 자체는 쓰지 않고 타입만 본다.
    template <typename TPacket>
    ZoneUnitOfWork(const TPacket&, Unit* const ownerUnit)
        : Task::UnitOfWork(static_cast<uint64_t>(ownerUnit->GetUnitId().Value()), Base::Ruid::Create())
        , requestPacketId_(static_cast<uint16_t>(TPacket::kPacketId))
        , ownerUnit_(ownerUnit)
    {
    }

    // 요청 없이 서버가 스스로 만든 변경(우편 만료 삭제 등). 결과 패킷의 requestPacketId
    // 자리에 0이 실려서, 클라이언트는 "내가 요청한 적 없는 변경 통지"로 구분한다.
    explicit ZoneUnitOfWork(Unit* const ownerUnit);

    // 결말을 내는 자리. 예외를 밖으로 내보내면 프로세스가 즉시 종료되므로 noexcept +
    // 내부 try/catch로 막는다.
    ~ZoneUnitOfWork() noexcept;

    // 롤백하는 쪽(Unit::RollbackUoW)이 훑는다. **역순으로 돌 것** -- 나중에 일어난 변경부터
    // 되돌려야 중간 상태를 거치지 않는다.
    [[nodiscard]] const TaskList& GetTasks() const noexcept { return Tasks(); }

private:
    // 성공 경로에서만 부른다(실패하면 되돌리므로 내보낼 것이 없다).
    // 와이어 포맷: ownerId(8) + taskCount(2) + taskCount개의 {kind(2) + len(4) + payload}.
    // 길이 프리픽스를 두는 이유: 읽는 쪽이 그 kind를 아직 모르더라도(새 콘텐츠 태스크가
    // 추가되기 전 빌드) 통째로 건너뛰고 다음 태스크로 넘어갈 수 있게 하기 위함.
    [[nodiscard]] std::vector<byte> Serialize() const;

    void SendToWorld(const std::span<const byte> stream) const;

    // 결과 패킷(Z2CTaskResult) 하나를 그 클라이언트에게 보낸다. 성공이면 태스크 스트림이
    // 붙고, 실패면 에러 코드만 나간다(되돌렸으므로 적용할 태스크가 없다).
    void SendTaskResult(const int32_t errorCode, const std::span<const byte> stream) const;

    uint16_t requestPacketId_;
    Unit* ownerUnit_;
};
