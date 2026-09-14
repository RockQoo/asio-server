#pragma once

#include "Packet/ZonePackets.h"

#include "Shared/Core/Src/Packet/BinaryReader.h"
#include "Shared/Protocol/Src/Ids.h"

namespace Zone
{
    // 클라이언트가 보내는 요청 하나 = 여기 구조체 하나. 이름은 패킷 id 그대로 쓴다
    // (.claude/rules/packet-naming.md).
    //
    // **파싱이 핸들러 안에 있지 않은 이유**: 예전에는 핸들러마다 BinaryReader를 열고
    // ReadString/Read를 늘어놓은 뒤 실패하면 InvalidPayload를 UnitOfWork에 넣었다. 그러면
    // 콘텐츠 코드 절반이 바이트 해석이고, "형식이 틀렸다"와 "내용이 틀렸다"(잔액 부족 등)가
    // 같은 자리에서 섞인다. 파싱을 디스패치 앞으로 빼면 핸들러는 이미 해석된 값만 받는다
    // -- PlayerProcessor::RegisterHandler 주석 참고.
    //
    // Parse는 **형식만** 본다. 값의 타당성(음수 가격, 말이 안 되는 만료 시간 등)은 콘텐츠
    // 판단이라 핸들러가 EErrorCode로 돌려준다.

    struct C2ZMove
    {
        MovePacket move{};

        [[nodiscard]] bool Parse(const std::span<const byte> payload)
        {
            if (payload.size() < sizeof(MovePacket))
            {
                return false;
            }
            std::memcpy(&move, payload.data(), sizeof(MovePacket));
            return true;
        }
    };

    struct C2ZChat
    {
        std::string message;

        [[nodiscard]] bool Parse(const std::span<const byte> payload)
        {
            Packet::BinaryReader binaryReader(payload);
            return binaryReader.ReadString(message);
        }
    };

    struct C2ZMailAdd
    {
        std::string title;
        std::string body;
        int64_t durationSec{};

        [[nodiscard]] bool Parse(const std::span<const byte> payload)
        {
            Packet::BinaryReader binaryReader(payload);
            return binaryReader.ReadString(title) && binaryReader.ReadString(body)
                && binaryReader.Read(durationSec);
        }
    };

    struct C2ZMailDel
    {
        Protocol::MailId mailId;

        [[nodiscard]] bool Parse(const std::span<const byte> payload)
        {
            // MailId는 int64 하나를 감싼 값이라 와이어에서도 8바이트 그대로다
            // (Shared/Protocol/Src/StrongId.h).
            Protocol::MailId::ValueType value{};
            if (payload.size() < sizeof(value))
            {
                return false;
            }
            std::memcpy(&value, payload.data(), sizeof(value));
            mailId = Protocol::MailId{value};
            return true;
        }
    };

    struct C2ZMailBuy
    {
        std::string title;
        std::string body;
        int64_t durationSec{};
        int64_t price{};

        [[nodiscard]] bool Parse(const std::span<const byte> payload)
        {
            Packet::BinaryReader binaryReader(payload);
            return binaryReader.ReadString(title) && binaryReader.ReadString(body)
                && binaryReader.Read(durationSec) && binaryReader.Read(price);
        }
    };
}
