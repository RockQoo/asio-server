#pragma once

#include "Server/Core/Src/Packet/Args.h"
#include "Server/Core/Src/Processor/Group.h"

namespace Message
{
    // 프로세서 id로 메시지를 보내는 라우터. **보내는 쪽이 받는 쪽 객체를 몰라도 되게** 하는 것이
    // 목적이다 -- 이게 없으면 프로세서 셋에 보내려고 참조 셋을 들고 다녀야 하고, 프로세서가 늘
    // 때마다 중간의 모든 시그니처가 같이 늘어난다.
    //
    //   PushMsg(EMsgId::QueueTest, EProcessorId::Tool, ownerId, 10, "zeus", value);
    //   PushMsg(EMsgId::QueueTest, EProcessorId::Main, ownerId, 10, "zeus", value);
    //
    // **같은 msgId를 프로세서마다 다르게 처리할 수 있다** -- 핸들러 표의 키가 (프로세서, msgId)
    // 쌍이기 때문이다. 그게 이 클래스가 Packet::Dispatcher와 다른 점이다.
    //
    // **싱글턴이 아니다. App이 소유한다.** 전역에 있는 것은 "지금 도는 App의 라우터"를 가리키는
    // 포인터 하나뿐이고 소유하지 않는다(Current/SetCurrent). 싱글턴으로 만들면 프로세스 끝까지
    // 살아서, 먼저 죽는 App의 Group을 계속 가리키게 된다 -- 그 수명 역전을 없애려는 것이다.
    // App 생성자에서 SetCurrent(this), 소멸자에서 SetCurrent(nullptr)를 부르면 순서가 항상
    // 맞는다(소멸자 본문이 멤버인 Group들보다 먼저 돈다). 근거: docs/design/config-file.md의
    // 싱글턴 기준 -- 이 라우터는 자기 자원을 소유하는 서비스가 아니라 남의 것을 참조만 한다.
    //
    // **스레드 규약**: Register/Bind는 기동 때 한 스레드에서만. Push는 어느 스레드에서든 된다
    // (그 뒤는 Group::Post 규약 -- ownerId가 같으면 겹치지 않고 보낸 순서대로).
    template <Processor::ProcessorId TProcessorId, typename TMsgId>
    class Router final
    {
    public:
        // 핸들러가 인자를 직접 꺼낸다 -- 메시지마다 인자 구성이 달라 시그니처를 고정할 수 없다.
        using Handler = std::function<void(Packet::BinaryReader&)>;
        using Group = Processor::Group<TProcessorId>;

        Router() = default;

        Router(const Router&) = delete;
        Router& operator=(const Router&) = delete;

        // 지금 이 프로세스에서 쓰는 라우터. 없으면 nullptr -- 호출부가 확인해야 한다.
        [[nodiscard]] static Router* Current() noexcept { return CurrentSlot().load(std::memory_order_acquire); }

        // App 생성자/소멸자에서만 부른다.
        static void SetCurrent(Router* const router) noexcept
        {
            CurrentSlot().store(router, std::memory_order_release);
        }

        // 이 프로세서의 메시지를 어느 큐 그룹에서 돌릴지 정한다. **기동 때 한 번.**
        void Register(const TProcessorId processorId, Group& group)
        {
            groups_[static_cast<size_t>(processorId)] = &group;
        }

        // (프로세서, msgId) 하나에 핸들러 하나. **기동 때만.**
        void Bind(const TProcessorId processorId, const TMsgId msgId, Handler handler)
        {
            handlers_[Key(processorId, msgId)] = std::move(handler);
        }

        template <typename TSelf>
        void Bind(const TProcessorId processorId, const TMsgId msgId, TSelf* const self,
                  void (TSelf::* const method)(Packet::BinaryReader&))
        {
            Bind(processorId, msgId,
                 [self, method](Packet::BinaryReader& binaryReader) { (self->*method)(binaryReader); });
        }

        // 인자를 넣은 순서대로 실어 그 프로세서의 레인으로 넘긴다. 어느 스레드에서든 된다.
        template <typename... TArgs>
        void Push(const TMsgId msgId, const TProcessorId processorId, const uint64_t ownerId,
                  const TArgs&... args)
        {
            Group* const group = groups_[static_cast<size_t>(processorId)];
            if (group == nullptr)
            {
                LOG.Warning(ELogCategory::General, "등록되지 않은 프로세서로 메시지를 보냈다")
                    .KV("Processor", ToString(processorId));
                return;
            }

            Packet::BinaryWriter binaryWriter;
            Packet::WriteArgs(binaryWriter, args...);

            // payload를 **옮겨** 넣는다 -- 호출자의 지역 버퍼는 이 함수가 끝나면 사라지고,
            // 메시지는 그 뒤에 다른 스레드에서 실행된다.
            group->Post(processorId, ownerId,
                [this, msgId, processorId, payload = binaryWriter.MoveBuffer()]
                {
                    const auto it = handlers_.find(Key(processorId, msgId));
                    if (it == handlers_.end())
                    {
                        LOG.Warning(ELogCategory::General, "바인딩되지 않은 메시지")
                            .KV("Processor", ToString(processorId))
                            .KV("MsgId", static_cast<uint64_t>(msgId));
                        return;
                    }

                    Packet::BinaryReader binaryReader(payload);
                    it->second(binaryReader);
                });
        }

    private:
        // 프로세서와 msgId를 한 키로 합친다 -- 같은 msgId를 프로세서마다 다르게 처리하려면
        // 둘 다 키에 들어가야 한다.
        [[nodiscard]] static constexpr uint64_t Key(const TProcessorId processorId, const TMsgId msgId) noexcept
        {
            return (static_cast<uint64_t>(processorId) << 32) | static_cast<uint64_t>(msgId);
        }

        [[nodiscard]] static std::atomic<Router*>& CurrentSlot() noexcept
        {
            static std::atomic<Router*> current{nullptr};
            return current;
        }

        std::array<Group*, static_cast<size_t>(TProcessorId::Count)> groups_{};
        std::unordered_map<uint64_t, Handler> handlers_;
    };
}
