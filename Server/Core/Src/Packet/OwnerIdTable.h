#pragma once

#include "Server/Core/Src/Packet/OwnerIdPeek.h"

namespace Packet
{
    // 패킷 id -> "주인이 페이로드 어디에 있나" 표.
    //
    // **I/O 스레드에서 쓴다.** 링크 핸들러는 본문을 해석하지 않고 정수 하나만 훔쳐봐
    // 어느 레인으로 넘길지 정하는데, 그 정수의 타입과 오프셋이 패킷마다 다르다.
    //
    // **Dispatcher 로는 못 담는다** -- 핸들러를 부르는 게 아니라 값을 돌려주기 때문이다.
    // 대신 등록 모양을 맞춰서, 그 링크가 받는 패킷 목록이 Register() 한 곳에 보이게 한다.
    //
    // 등록은 생성자에서 끝나고 이후로는 읽기 전용이라 여러 I/O 스레드가 동시에 Find 해도
    // 안전하다(Dispatcher 와 같은 규약).
    template <typename TPacketId>
    class OwnerIdTable
    {
    public:
        // TOwner 는 와이어에 실제로 박혀 있는 정수 타입이고, offset 은 그 앞에 붙은
        // 바이트 수다(`#pragma pack(1)` 이라 패딩이 없다는 것에 기댄다).
        template <typename TOwner>
        void Register(const TPacketId id, const size_t offset = 0)
        {
            readers_[id] = [offset](const std::span<const byte> payload)
            {
                return PeekOwnerId<TOwner>(payload, offset);
            };
        }

        // 등록되지 않은 id면 nullopt -- 호출부가 그대로 버리면 된다.
        [[nodiscard]] std::optional<uint64_t> Find(const TPacketId id,
                                                   const std::span<const byte> payload) const
        {
            const auto it = readers_.find(id);
            if (it == readers_.end())
            {
                return std::nullopt;
            }

            return it->second(payload);
        }

    private:
        using Reader = std::function<std::optional<uint64_t>(std::span<const byte>)>;

        std::unordered_map<TPacketId, Reader> readers_;
    };
}
