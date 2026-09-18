#pragma once

namespace Packet
{
    // 역직렬화 실패 한 줄. 정의가 Dispatcher.cpp 에 있는 이유는 거기 주석 참고
    // (로그 카테고리가 Core 것이라 헤더에서 부를 수 없다).
    void LogParseFailure(const uint16_t packetId, const size_t payloadBytes);

    // 패킷 타입 -> 핸들러를 연결해주는 범용 라우터.
    // TPacketId는 보통 `enum class : uint16_t` 형태이고, TContext는 핸들러가 전달받을 값
    // (예: Network::Session::SPtr)이다.
    template <typename TPacketId, typename TContext>
    class Dispatcher
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
        // 위한 것 -- `Register(id, this, &Instance::HandleMove)` 한 줄로 끝난다.
        // self는 캡처만 하고 수명은 관리하지 않는다: 등록 대상이 전부 "핸들러를 소유한
        // 객체 자신(this)"이라 dispatcher가 그 객체보다 오래 살 수 없는 구조다.
        //
        // 컨텍스트를 그대로 넘길 수 있으면 그대로, 아니면 한 번 역참조해서 넘긴다 --
        // Instance처럼 TContext는 PlayerState*인데 핸들러는 PlayerState&를 받는 경우를
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

        // 해석된 구조체를 받는 핸들러용. **id 를 적지 않는다** -- `TPacket::kPacketId` 에서
        // 나오므로 등록 지점과 구조체가 어긋날 수가 없다.
        //
        // 역직렬화를 디스패치 **앞**으로 당기는 것이 요점이다. 형식이 깨진 페이로드는 여기서
        // 끝나고(로그 + 버림), 핸들러는 검증을 통과한 구조체만 본다 -- 그래서 검증 안 된
        // 바이트를 만지는 코드가 `Parse` 안에만 남는다.
        //
        // 버리기만 하고 답하지 않는 이유: 정상 클라이언트는 자기가 만든 구조체를 그대로
        // 보내므로 실패할 수 없고, 실패했다면 조작이거나 프로토콜 버전이 어긋난 것이라
        // 콘텐츠가 답할 내용이 아니다. 답을 주면 어떤 형식이 통과하는지 알려주는 셈이기도 하다.
        //
        // **역직렬화 함수는 ADL 로 찾는다.** Core 는 와이어 포맷을 모르므로
        // `FromBytes(packet, payload)` 를 한정 없이 부르고, TPacket 이 사는 네임스페이스
        // (`Common`) 의 것이 걸린다 -- `Log::Entry` 가 카테고리의 `ToString()` 을 찾는 방식과 같다.
        // 그래서 등록 지점은 함수 포인터 하나만 넘기면 된다.
        template <typename TSelf, typename TPacket>
        void Register(TSelf* const self, void (TSelf::* const method)(const TContext&, const TPacket&))
        {
            handlers_[TPacket::kPacketId] =
                [self, method](const TContext& context, const std::span<const byte> payload)
            {
                TPacket packet{};
                if (!FromBytes(packet, payload))
                {
                    LogParseFailure(static_cast<uint16_t>(TPacket::kPacketId), payload.size());
                    return;
                }

                (self->*method)(context, packet);
            };
        }

        // 위와 같되 핸들러가 자유 함수(static)인 경우. 콘텐츠가 스스로 등록하는 쪽
        // (PlayerMail 등)이 이 형태를 쓴다.
        template <typename TPacket>
        void Register(void (* const function)(const TContext&, const TPacket&))
        {
            handlers_[TPacket::kPacketId] =
                [function](const TContext& context, const std::span<const byte> payload)
            {
                TPacket packet{};
                if (!FromBytes(packet, payload))
                {
                    LogParseFailure(static_cast<uint16_t>(TPacket::kPacketId), payload.size());
                    return;
                }

                function(context, packet);
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

    private:
        std::unordered_map<TPacketId, Handler> handlers_;
    };
}
