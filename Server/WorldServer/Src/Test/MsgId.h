#pragma once

#include "Shared/Core/Src/Message/Router.h"

#include "Processor/ProcessorId.h"

// 프로세스 안에서만 도는 메시지 종류. **Common::PacketId와 무관하다** -- 와이어로
// 나가지 않아서 대역 규칙도 적용받지 않는다.
//
// **같은 msgId를 여러 프로세서가 각자 다르게 처리할 수 있다.** 핸들러 표의 키가
// (프로세서, msgId) 쌍이라, QueueTest를 Main과 Tool이 따로 바인딩해도 섞이지 않는다.
enum class EMsgId : uint16_t
{
    QueueTest,
    LaneProbe,  // 같은 주인의 일이 어느 스레드에서 도는지 보는 관측용(F2/F3)
};

using MsgRouter = Message::Router<EWorldProcessorId, EMsgId>;

// 이 프로세스 어디서든 쓰는 보내기 진입점. 받는 쪽 객체를 몰라도 되고, 프로세서 id만
// 바꾸면 같은 메시지가 다른 레인에서 처리된다.
//
//   PushMsg(EMsgId::QueueTest, EWorldProcessorId::Test, ownerId, 10, "zeus", value);
//
// ownerId가 같은 메시지끼리는 겹치지 않고 보낸 순서대로 실행된다(Group::Post 규약).
//
// **라우터는 App이 소유한다.** App이 없는 동안(기동 전/종료 후)에는 보낼 곳이 없으므로
// 조용히 버리지 않고 경고를 남긴다 -- 그 시점에 메시지를 보내는 코드는 대개 실수다.
template <typename... TArgs>
void PushMsg(const EMsgId msgId, const EWorldProcessorId processorId, const uint64_t ownerId,
             const TArgs&... args)
{
    MsgRouter* const router = MsgRouter::Current();
    if (router == nullptr)
    {
        LOG.Warning(ELogCategory::General, "App이 없는 동안 메시지를 보냈다")
            .KV("Processor", ToString(processorId)).KV("MsgId", static_cast<uint16_t>(msgId));
        return;
    }

    router->Push(msgId, processorId, ownerId, args...);
}
