#pragma once

#include "Shared/Core/Src/Base/Types.h"
#include "Shared/Core/Src/Task/UnitOfWork.h"
#include "Shared/Common/Src/Ids.h"
#include "Shared/Common/Src/PacketId.h"

#include "Currency/Model.h"
#include "Mail/Model.h"
#include "Shared/Core/Src/Network/SessionHolder.h"

class Player;

// 존 로직에서 요청 하나(= 패킷 하나)를 처리하는 동안의 트랜잭션 경계.
// 여러 모델(우편/재화 등)의 변경을 모았다가 **스코프를 벗어나는 순간** 결말을 낸다:
//   성공 -> 태스크 전량이 World로(Z2WUnitOfWorkStream), 같은 바이트가 그 클라이언트에게도
//           (Z2CTaskResult) 나간다. 클라이언트는 같은 태스크 목록을 자기 메모리에 적용하므로
//           서버와 상태가 어긋나지 않는다. DB에 저장하는 변경을 클라이언트에게 알리지 않는
//           경우는 없어서(알리지 않으면 그게 곧 불일치) 대상을 구분하는 플래그가 없다.
//   실패 -> 이미 반영된 메모리 변경을 역순으로 되돌리고, 클라이언트에는 에러 코드만 간다.
//
// **태스크의 뜻을 아는 것은 이 클래스뿐이다.** Core의 Task::UnitOfWork는 목록과 순서만
// 들고 있고, 직렬화(Serialize)와 역연산(Rollback) 둘 다 여기서 taskKind로 분기한다 --
// 그래야 Core가 게임을 모른다는 규칙이 선다.
//
// 커밋이 소멸자인 이유와 final로 닫는 이유는 Task::UnitOfWork 주석 참고.
// 스레드: 그 존을 담당하는 플레이어 레인 스레드(또는 우편 만료 유지보수 스레드)에서만
// 만들고 쓴다.
class ZoneUnitOfWork final : public Task::UnitOfWork
{
public:
    // **롤백이 되돌릴 모델들.** 태스크가 데이터만 갖게 되면서 "무엇을 되돌리나"를 여기서
    // 알아야 한다. UnitOfWork를 여는 쪽이 자기가 만질 모델만 채운다 -- 만료 스윕처럼
    // 우편함만 만지는 경로는 wallet이 비어 있다.
    //
    // wallet이 포인터인 이유: Currency::Model은 Player가 값으로 들고 있어(Mutexed가 아니다)
    // 소유권을 나눠 가질 수 없다. 이 UnitOfWork의 수명이 요청 스코프라 그동안 Player가
    // 사라지지 않는다는 전제는 그대로다(퇴장도 같은 레인에서 돈다).
    struct Models
    {
        std::shared_ptr<Mail::Model::Mutexed> mailBox;
        Currency::Model* wallet{};
    };

    // 클라이언트 요청을 처리하는 경우. requestPacketId는 클라이언트가 "무슨 요청의
    // 결과인지" 짝지을 수 있도록 결과 패킷에 그대로 실린다.
    ZoneUnitOfWork(Network::SessionHolder& worldLink, Player& player, const PacketId requestPacketId);

    // 요청 없이 서버가 스스로 만든 변경(우편 만료 삭제 등). 결과 패킷의 requestPacketId
    // 자리에 0이 실려서, 클라이언트는 "내가 요청한 적 없는 변경 통지"로 구분한다.
    // Player 없이 우편함만 아는 경로(만료 스윕)라 모델을 직접 받는다.
    ZoneUnitOfWork(Network::SessionHolder& worldLink, const Network::SessionId clientSessionId,
               const Common::PlayerId playerId, Models models);

    // 결말을 내는 자리. 예외를 밖으로 내보내면 프로세스가 즉시 종료되므로 noexcept +
    // 내부 try/catch로 막는다.
    ~ZoneUnitOfWork() noexcept;

private:
    // 성공 경로에서만 부른다(실패하면 되돌리므로 내보낼 것이 없다).
    // 와이어 포맷: ownerId(8) + taskCount(2) + taskCount개의 {kind(2) + len(4) + payload}.
    // 길이 프리픽스를 두는 이유: 읽는 쪽이 그 kind를 아직 모르더라도(새 콘텐츠 태스크가
    // 추가되기 전 빌드) 통째로 건너뛰고 다음 태스크로 넘어갈 수 있게 하기 위함.
    [[nodiscard]] std::vector<byte> Serialize() const;

    // 실패 경로에서 기록의 역순으로 되돌린다 -- 나중에 일어난 변경부터 되돌려야 중간
    // 상태를 거치지 않는다.
    void RollbackAll() noexcept;

    void SendToWorld(const std::span<const byte> stream) const;

    // 결과 패킷(Z2CTaskResult) 하나를 그 클라이언트에게 보낸다. 성공이면 태스크 스트림이
    // 붙고, 실패면 에러 코드만 나간다(되돌렸으므로 적용할 태스크가 없다).
    void SendTaskResult(const int32_t errorCode, const std::span<const byte> stream) const;

    Network::SessionHolder& worldLink_;
    Network::SessionId clientSessionId_;
    Common::PlayerId playerId_;
    uint16_t requestPacketId_;
    Models models_;
};
