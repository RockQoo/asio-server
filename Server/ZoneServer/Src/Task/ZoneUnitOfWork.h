#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Task/UnitOfWork.h"
#include "Shared/Protocol/Src/PacketId.h"

#include <cstdint>
#include <span>

namespace Zone
{
    class WorldLink;

    // 존 로직에서 요청 하나(= 패킷 하나)를 처리하는 동안의 트랜잭션 경계.
    // 여러 모델(우편/재화 등)의 변경을 모았다가 **스코프를 벗어나는 순간** 결말을 낸다:
    //   성공 -> 태스크 전량이 World로(Z2WUnitOfWorkStream), 같은 바이트가 그 클라이언트에게도
    //           (Z2CTaskResult) 나간다. 클라이언트는 같은 태스크 목록을 자기 메모리에 적용하므로
    //           서버와 상태가 어긋나지 않는다. DB에 저장하는 변경을 클라이언트에게 알리지 않는
    //           경우는 없어서(알리지 않으면 그게 곧 불일치) 대상을 구분하는 플래그가 없다.
    //   실패 -> 이미 반영된 메모리 변경을 역순으로 되돌리고, 클라이언트에는 에러 코드만 간다.
    //
    // 커밋이 소멸자인 이유와 final로 닫는 이유는 Task::UnitOfWork 주석 참고.
    // 스레드: 그 존을 담당하는 BASIC 스레드(또는 우편 만료 유지보수 스레드)에서만 만들고 쓴다.
    class ZoneUnitOfWork final : public Task::UnitOfWork
    {
    public:
        // 클라이언트 요청을 처리하는 경우. requestPacketId는 클라이언트가 "무슨 요청의
        // 결과인지" 짝지을 수 있도록 결과 패킷에 그대로 실린다.
        ZoneUnitOfWork(WorldLink& worldLink, const Network::SessionId clientSessionId,
                       const uint32_t playerId, const PacketId requestPacketId);

        // 요청 없이 서버가 스스로 만든 변경(우편 만료 삭제 등). 결과 패킷의 requestPacketId
        // 자리에 0이 실려서, 클라이언트는 "내가 요청한 적 없는 변경 통지"로 구분한다.
        ZoneUnitOfWork(WorldLink& worldLink, const Network::SessionId clientSessionId,
                       const uint32_t playerId);

        // 결말을 내는 자리. 예외를 밖으로 내보내면 프로세스가 즉시 종료되므로 noexcept +
        // 내부 try/catch로 막는다.
        ~ZoneUnitOfWork() noexcept;

    private:
        void SendToWorld(const std::span<const byte> stream) const;

        // 결과 패킷(Z2CTaskResult) 하나를 그 클라이언트에게 보낸다. 성공이면 태스크 스트림이
        // 붙고, 실패면 에러 코드만 나간다(되돌렸으므로 적용할 태스크가 없다).
        void SendTaskResult(const int32_t errorCode, const std::span<const byte> stream) const;

        WorldLink& worldLink_;
        Network::SessionId clientSessionId_;
        uint32_t playerId_;
        uint16_t requestPacketId_;
    };
}
