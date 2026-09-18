#pragma once

#include "Server/Core/Src/Pipeline/Message.h"

namespace Pipeline
{
    class MessageProducer;

    // 레인 위에서 도는 객체 하나. **상속해서 RegistHandler() 안을 Regist()로 채운다.**
    //
    // 세 좌표 중 "어느 객체"가 이것이다. 보내는 쪽은 ProcessorId만 알면 되고 이 객체를
    // 몰라도 된다 -- 그래서 프로세서가 늘어도 중간 시그니처가 같이 늘지 않는다.
    //
    // **스레드 규약**
    //   RegistHandler()는 MessageProducer::AddProcessor가 **기동 때 한 번** 부른다.
    //   OnMessage()는 이 프로세서가 얹힌 레인의 스레드에서 돈다 -- 같은 ownerId끼리는
    //   겹치지 않고 보낸 순서대로다. 그래서 **ownerId 단위로 갈리는 상태에는 락이 없다.**
    //   여러 ownerId가 같이 보는 상태를 멤버로 두면 그 보호가 깨진다(Thread::Mutexed 필요).
    class MessageProcessor
    {
        friend class MessageProducer;

    public:
        virtual ~MessageProcessor() = default;

        MessageProcessor(const MessageProcessor&) = delete;
        MessageProcessor& operator=(const MessageProcessor&) = delete;

        // 로그에 찍히는 이름.
        [[nodiscard]] virtual std::string_view Name() const = 0;

        // 이 프로세서가 받을 msgType과 핸들러를 Regist()로 등록한다.
        virtual void RegistHandler() = 0;

        // **레인 스레드가 부른다.** 표에 없으면 버리고 알린다 -- 조용히 사라지면 "보냈는데
        // 아무 일도 안 일어난다"가 되어 원인을 찾을 데가 없다.
        void OnMessage(const Message& message)
        {
            const auto found = handlers_.find(message.msgType);
            if (found == handlers_.end())
            {
                LOG.Warning(ELogCategory::General, "등록되지 않은 메시지")
                    .KV("Processor", Name()).KV("MsgType", message.msgType);
                return;
            }

            found->second(message);
        }

        [[nodiscard]] ProcessorId GetProcessorId() const noexcept { return processorId_; }

    protected:
        MessageProcessor() = default;

        // 멤버 함수를 그대로 등록한다:
        //   Regist(PacketId::C2WLogin, &LoginProcessor::OnLogin);
        //
        // 핸들러 시그니처는 `void (const OwnerId&, const TBody&)` 고정이고 TBody는 메서드
        // 포인터에서 추론된다.
        template <typename TMsgId, typename TSelf, typename TBody>
        void Regist(const TMsgId msgType, void (TSelf::* const method)(const OwnerId&, const TBody&))
        {
            auto* const self = static_cast<TSelf*>(this);
            handlers_[static_cast<uint32_t>(msgType)] =
                [self, method](const Message& message)
                {
                    (self->*method)(message.msgOwnerId, *static_cast<const TBody*>(message.body.get()));
                };
        }

        // body가 없는 메시지용. 주인만으로 뜻이 서는 통지(연결 종료 등)가 여기 해당한다.
        template <typename TMsgId, typename TSelf>
        void Regist(const TMsgId msgType, void (TSelf::* const method)(const OwnerId&))
        {
            auto* const self = static_cast<TSelf*>(this);
            handlers_[static_cast<uint32_t>(msgType)] =
                [self, method](const Message& message) { (self->*method)(message.msgOwnerId); };
        }

    private:
        std::unordered_map<uint32_t, std::function<void(const Message&)>> handlers_;

        // AddProcessor가 채운다(그래서 friend다). 그 전에는 유효하지 않다.
        ProcessorId processorId_{};
    };
}
