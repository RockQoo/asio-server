#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Task/UnitOfWork.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Shared/Protocol/Src/TaskKind.h"

#include <cstdint>

namespace Mail
{
    class MailRegistry;
}

namespace Zone
{
    class WorldLink;

    // 존 로직에서 요청 하나(= 패킷 하나)를 처리하는 동안의 트랜잭션 경계.
    // 여러 모델(우편/인벤 등)의 변경을 한 번에 모았다가 Commit()에서 결말을 낸다:
    //   성공 -> DB용 태스크는 World로(Z2WUnitOfWorkStream), 클라이언트용 태스크는 그 클라에게
    //           (Z2CTaskResult) 각각 패킷 하나로 나간다. 클라이언트는 같은 태스크 목록을
    //           자기 메모리에 적용하므로 서버와 상태가 어긋나지 않는다.
    //   실패 -> 이미 반영된 메모리 변경을 역순으로 되돌리고, 클라이언트에는 에러 코드만 간다.
    //
    // 스레드: 그 존을 담당하는 BASIC 스레드(또는 메일 만료 유지보수 스레드)에서만 만들고 쓴다.
    class ZoneUnitOfWork final : public Task::UnitOfWork
    {
    public:
        // 클라이언트 요청을 처리하는 경우. requestPacketId는 클라이언트가 "무슨 요청의
        // 결과인지" 짝지을 수 있도록 결과 패킷에 그대로 실린다.
        ZoneUnitOfWork(WorldLink& worldLink, Mail::MailRegistry& mailRegistry,
                       const Network::SessionId clientSessionId, const uint32_t playerId,
                       const PacketId requestPacketId);

        // 요청 없이 서버가 스스로 만든 변경(메일 만료 삭제 등). 결과 패킷의 requestPacketId
        // 자리에 0이 실려서, 클라이언트는 "내가 요청한 적 없는 변경 통지"로 구분한다.
        ZoneUnitOfWork(WorldLink& worldLink, Mail::MailRegistry& mailRegistry,
                       const Network::SessionId clientSessionId, const uint32_t playerId);

    protected:
        void OnFlush(const Task::ETaskTarget target, const std::span<const byte> stream) override;
        void OnRollback(const uint16_t taskKind, const std::span<const byte> payload) override;
        void OnFailed(const int32_t errorCode) override;

    private:
        void RollbackMailTask(const Protocol::EMailTask subTask, const std::span<const byte> payload) const;

        // 결과 패킷(Z2CTaskResult) 하나를 그 클라이언트에게 보낸다. 성공이면 태스크 스트림이
        // 붙고, 실패면 에러 코드만 나간다(되돌렸으므로 적용할 태스크가 없다).
        void SendTaskResult(const int32_t errorCode, const std::span<const byte> stream) const;

        WorldLink& worldLink_;
        Mail::MailRegistry& mailRegistry_;
        Network::SessionId clientSessionId_;
        uint32_t playerId_;
        uint16_t requestPacketId_;
    };
}
