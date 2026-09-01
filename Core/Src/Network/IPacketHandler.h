#pragma once

#include "Core/Src/Packet/PacketHeader.h"

#include <cstddef>
#include <memory>
#include <span>
#include <system_error>

namespace Network
{
    class Session;

    // I/O 스레드에서 호출되는 애플리케이션 쪽 훅.
    // 구현체는 이 콜백들을 "I/O 스레드 코드"로 취급해야 한다: 공유 게임 상태를 여기서 직접
    // 건드리지 말고 로직 스레드로 넘겨야 한다.
    class IPacketHandler
    {
    public:
        virtual ~IPacketHandler() = default;

        // accept 성공(Listener)과 outbound connect 성공(Connector) 양쪽에서 호출된다 -- "세션이
        // 막 시작됐다"는 의미는 둘 다 같아서 별도 OnConnected를 안 두고 하나로 재사용한다
        // (Connector 주석 참고).
        virtual void OnSessionOpened(const std::shared_ptr<Session>& session) = 0;

        virtual void OnPacket(const std::shared_ptr<Session>& session,
                              const Packet::PacketHeader& header,
                              const std::span<const byte> payload) = 0;

        virtual void OnClosed(const std::shared_ptr<Session>& session, const std::error_code& reason) = 0;
    };
}
