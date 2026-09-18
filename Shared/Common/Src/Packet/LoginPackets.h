#pragma once

#include "Shared/Common/Src/ContentLimit.h"
#include "Shared/Common/Src/ErrorCode.h"
#include "Shared/Common/Src/Ids.h"
#include "Shared/Common/Src/PacketId.h"

#include "Shared/Core/Src/Packet/BinaryReader.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"

namespace Common
{
    // 로그인 요청. **World 가 끝점인 유일한 대역(C2W)** 이라 존으로 넘어가지 않는다.
    // 계정이 없으면 서버가 그 자리에서 만든다(Sql/players.sql 의 usp_players_upsert).
    struct C2WLogin
    {
        static constexpr PacketId kPacketId = PacketId::C2WLogin;

        std::string playerName;
        std::string password;

        void Set(std::string name, std::string pass)
        {
            playerName = std::move(name);
            password = std::move(pass);
        }

        [[nodiscard]] std::vector<byte> Serialize() const
        {
            Packet::BinaryWriter binaryWriter;
            binaryWriter.WriteString(playerName);
            binaryWriter.WriteString(password);
            return binaryWriter.MoveBuffer();
        }

        [[nodiscard]] bool Parse(const std::span<const byte> payload)
        {
            Packet::BinaryReader binaryReader(payload);
            return binaryReader.ReadString(playerName) && binaryReader.ReadString(password);
        }
    };

    // 로그인 결과. **이름만으로 결과임이 분명해 Result 접미사를 안 붙였다**
    // (.claude/rules/packet-naming.md 의 접미사 규칙).
    struct W2CLogin
    {
        static constexpr PacketId kPacketId = PacketId::W2CLogin;

        EErrorCode errorCode{};
        int64_t playerId{};
        std::string playerName;

        void Set(const EErrorCode error, const int64_t id, std::string name)
        {
            errorCode = error;
            playerId = id;
            playerName = std::move(name);
        }

        [[nodiscard]] std::vector<byte> Serialize() const
        {
            Packet::BinaryWriter binaryWriter;
            binaryWriter.Write(static_cast<int32_t>(errorCode));
            binaryWriter.Write(playerId);
            binaryWriter.WriteString(playerName);
            return binaryWriter.MoveBuffer();
        }

        // 받는 쪽은 서버가 아니라 **도구/클라이언트**다(StressClient, ProtocolClient).
        // 실패면 playerId 는 0 이고 이름이 비어 온다 -- errorCode 로만 판정할 것.
        [[nodiscard]] bool Parse(const std::span<const byte> payload)
        {
            Packet::BinaryReader binaryReader(payload);
            int32_t rawError{};
            if (!binaryReader.Read(rawError) || !binaryReader.Read(playerId)
                || !binaryReader.ReadString(playerName))
            {
                return false;
            }

            errorCode = static_cast<EErrorCode>(rawError);
            return true;
        }
    };

    // 전체 공지. 운영툴이 보내면 World 가 접속자 전원에게 뿌린다.
    struct W2CNotice
    {
        static constexpr PacketId kPacketId = PacketId::W2CNotice;

        std::string message;

        void Set(std::string text) { message = std::move(text); }

        [[nodiscard]] std::vector<byte> Serialize() const
        {
            Packet::BinaryWriter binaryWriter;
            binaryWriter.WriteString(message);
            return binaryWriter.MoveBuffer();
        }
    };

#pragma pack(push, 1)
    // 클라이언트가 붙었다/끊겼다. **보내는 건 GatewayServer 프로세스**라 C2W 가 아니라 G2W 다
    // (접두사는 "실제로 소켓에 쓰는 프로세스" 기준 -- packet-naming.md).
    struct G2WClientConnected
    {
        static constexpr PacketId kPacketId = PacketId::G2WClientConnected;

        uint64_t clientSessionId;

        void Set(const uint64_t id) { clientSessionId = id; }
    };

    struct G2WClientDisconnected
    {
        static constexpr PacketId kPacketId = PacketId::G2WClientDisconnected;

        uint64_t clientSessionId;

        void Set(const uint64_t id) { clientSessionId = id; }
    };
#pragma pack(pop)
}
