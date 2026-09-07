#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace Packet
{
    // 패킷 타입 -> 핸들러를 연결해주는 범용 라우터.
    // TPacketId는 보통 `enum class : uint16_t` 형태이고, TContext는 핸들러가 전달받을 값
    // (예: shared_ptr<Session>)이다.
    template <typename TPacketId, typename TContext>
    class PacketDispatcher
    {
    public:
        using Handler = std::function<void(const TContext&, std::span<const byte>)>;

        template <typename F>
        void Register(const TPacketId id, F&& handler)
        {
            handlers_[id] = std::forward<F>(handler);
        }

        // 멤버 함수 포인터 등록 오버로드. 등록 지점마다 있던 전달용 람다
        // (`[this](const auto& ctx, const auto payload) { HandleX(ctx, payload); }`)를 없애기
        // 위한 것 -- `Register(id, this, &ZoneInstance::HandleMove)` 한 줄로 끝난다.
        // self는 캡처만 하고 수명은 관리하지 않는다: 등록 대상이 전부 "핸들러를 소유한
        // 객체 자신(this)"이라 dispatcher가 그 객체보다 오래 살 수 없는 구조다.
        //
        // 컨텍스트를 그대로 넘길 수 있으면 그대로, 아니면 한 번 역참조해서 넘긴다 --
        // ZoneInstance처럼 TContext는 PlayerState*인데 핸들러는 PlayerState&를 받는 경우를
        // 등록 지점에서 `*player`로 풀어쓰지 않아도 되게 하려는 것.
        template <typename TSelf, typename TArg>
        void Register(const TPacketId id, TSelf* const self, void (TSelf::* const method)(TArg, std::span<const byte>))
        {
            handlers_[id] = [self, method](const TContext& context, const std::span<const byte> payload)
            {
                if constexpr (std::is_invocable_v<decltype(method), TSelf*, const TContext&, std::span<const byte>>)
                {
                    (self->*method)(context, payload);
                }
                else
                {
                    (self->*method)(*context, payload);
                }
            };
        }

        void Dispatch(const TPacketId id, const TContext& context, const std::span<const byte> payload) const
        {
            if (const auto it = handlers_.find(id); it != handlers_.end())
            {
                it->second(context, payload);
            }
        }

        [[nodiscard]] bool HasHandler(const TPacketId id) const
        {
            return handlers_.contains(id);
        }

        [[nodiscard]] size_t HandlerCount() const noexcept { return handlers_.size(); }

    private:
        std::unordered_map<TPacketId, Handler> handlers_;
    };
}
