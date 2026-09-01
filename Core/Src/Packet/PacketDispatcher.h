#pragma once

#include <cstddef>
#include <functional>
#include <span>
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
