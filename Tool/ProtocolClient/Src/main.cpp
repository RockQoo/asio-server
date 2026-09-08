#include "Shared/Core/Src/Common/BasicTypes.h"
#include "Shared/Core/Src/Log/LogProxy.h"
#include "Shared/Core/Src/Packet/BinaryReader.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Core/Src/Packet/PacketBuffer.h"
#include "Shared/Core/Src/Packet/PacketFramer.h"
#include "Server/ZoneServer/Src/Log/LogCategory.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Shared/Protocol/Src/TaskKind.h"
#include "Server/ZoneServer/Src/Packet/ZonePackets.h"

#include <asio.hpp>

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace
{
    // ZoneServer가 실제로 정의하는 프로토콜(Shared/Core/Src/Packet, Server/ZoneServer/Src/Packet의 헤더만
    // include)을 그대로 재사용한다. ZoneServer.exe를 링크하지 않아도 되는 이유는 이 헤더들이
    // 전부 POD 구조체/enum이라 별도 구현체가 필요 없기 때문이다.

    void PrintHelp()
    {
        std::cout <<
            "명령어:\n"
            "  echo <문자열>   - Echo 패킷 전송, 그대로 되돌아오는지 확인\n"
            "  move <x> <y>    - Move 패킷 전송, 같은 존에 브로드캐스트됨(본인도 수신).\n"
            "                    x가 10 이상/미만 경계를 넘으면 서버가 조용히 다른 존으로 핸드오프한다\n"
            "  chat <문자열>   - Chat 패킷 전송, 같은 존에 브로드캐스트됨(본인도 수신)\n"
            "  mail add <제목> <본문> <초>  - MailAdd 패킷 전송(초 뒤 서버가 자동 만료 삭제)\n"
            "  mail del <id>               - MailDel 패킷 전송\n"
            "  help            - 이 도움말 다시 출력\n"
            "  quit / exit      - 종료\n";
    }

    void SendPacket(asio::ip::tcp::socket& socket, const PacketId packetId,
                     const std::span<const byte> payload)
    {
        const auto frame = Packet::BuildFrame(packetId, payload);
        asio::write(socket, asio::buffer(frame));
    }

    // Z2CTaskResult 하나를 사람이 읽을 수 있게 풀어 찍는다. 실제 게임 클라이언트라면 여기서
    // 화면에 찍는 대신 같은 태스크 목록을 자기 메모리(우편함 등)에 적용해서 서버와 동기화한다
    // -- 콘텐츠마다 Ack 패킷을 따로 만들지 않는 이유가 이것이다.
    void PrintTaskResult(const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        int32_t errorCode{};
        uint16_t requestPacketId{};
        int64_t requestId{};
        if (!reader.Read(errorCode) || !reader.Read(requestPacketId) || !reader.Read(requestId))
        {
            return;
        }

        if (errorCode != 0)
        {
            // requestId는 요청 하나를 가리키는 값이다 -- 같은 패킷을 연달아 보내도 값이
            // 달라서, 어느 응답이 어느 요청의 결과인지 짝지을 수 있다(실패해도 실린다).
            std::cout << "[recv] TaskResult 실패 request=" << requestPacketId
                      << " requestId=" << requestId
                      << " error=" << errorCode << '\n';
            return;
        }

        uint64_t ownerId{};
        uint16_t taskCount{};
        if (!reader.Read(ownerId) || !reader.Read(taskCount))
        {
            return;
        }

        // requestPacketId=0은 요청 없이 서버가 만든 변경(메일 만료 삭제 등)이다.
        std::cout << "[recv] TaskResult request=" << requestPacketId
                  << " requestId=" << requestId << " tasks=" << taskCount << '\n';

        for (uint16_t i = 0; i < taskCount; ++i)
        {
            uint16_t kind{};
            uint32_t payloadLen{};
            if (!reader.Read(kind) || !reader.Read(payloadLen))
            {
                return;
            }

            const auto taskPayload = reader.ReadBytes(payloadLen);
            if (!taskPayload)
            {
                return;
            }

            if (Protocol::CategoryOf(kind) != Protocol::ETaskCategory::Mail)
            {
                std::cout << "        - 알 수 없는 태스크 kind=" << kind << '\n';
                continue;
            }

            Packet::BinaryReader mailReader(*taskPayload);
            uint32_t mailId{};
            std::string title;
            std::string body;
            int64_t sendUt{};
            int64_t endUt{};
            if (!mailReader.Read(mailId) || !mailReader.ReadString(title) || !mailReader.ReadString(body)
                || !mailReader.Read(sendUt) || !mailReader.Read(endUt))
            {
                continue;
            }

            const auto subTask = static_cast<Protocol::EMailTask>(Protocol::SubTaskOf(kind));
            std::cout << "        - Mail " << (subTask == Protocol::EMailTask::Added ? "Added" : "Removed")
                      << " mailId=" << mailId << " title=" << title << '\n';
        }
    }

    // 서버가 보내는 패킷(EnterZoneNotify/Echo/Move/Chat)을 계속 읽어서 화면에 찍는다.
    // 별도 스레드에서 블로킹 read_some을 돌리는 이유는 stdin 입력을 기다리는 메인 스레드를
    // 막지 않기 위해서다 -- 실제 서버처럼 asio::async_read를 쓸 필요는 없는 더미 클라이언트다.
    void ReceiveLoop(asio::ip::tcp::socket& socket, std::atomic<bool>& running)
    {
        Packet::PacketBuffer packetBuffer;
        std::array<byte, 4096> receiveBuffer{};

        while (running.load())
        {
            std::error_code ec;
            const auto bytesTransferred = socket.read_some(asio::buffer(receiveBuffer), ec);
            if (ec)
            {
                if (running.exchange(false))
                {
                    std::cout << "[ProtocolClient] 연결 종료: " << ec.message() << '\n';
                }
                break;
            }

            packetBuffer.Append(std::span(receiveBuffer.data(), bytesTransferred));

            Packet::PacketHeader header{};
            std::vector<byte> payload;
            while (packetBuffer.TryExtract(header, payload))
            {
                switch (static_cast<PacketId>(header.id))
                {
                case PacketId::Z2CEnterZoneNotify:
                {
                    Zone::EnterZoneNotifyPacket notify{};
                    if (payload.size() >= sizeof(notify))
                    {
                        std::memcpy(&notify, payload.data(), sizeof(notify));
                        std::cout << "[recv] EnterZoneNotify playerId=" << notify.playerId
                                  << " zoneId=" << notify.zoneId << '\n';
                    }
                    break;
                }
                case PacketId::Z2CEchoAck:
                {
                    const std::string text(reinterpret_cast<const char*>(payload.data()), payload.size());
                    std::cout << "[recv] EchoAck: " << text << '\n';
                    break;
                }
                case PacketId::Z2CMoveNotify:
                {
                    // 요청(C2ZMove)과 달리 브로드캐스트에는 sessionId가 앞에 붙는다.
                    Packet::BinaryReader reader(payload);
                    uint32_t moverId{};
                    Zone::MovePacket move{};
                    if (reader.Read(moverId) && reader.Read(move))
                    {
                        std::cout << "[recv] MoveNotify from " << moverId
                                  << " x=" << move.x << " y=" << move.y << '\n';
                    }
                    break;
                }
                case PacketId::Z2CChatNotify:
                {
                    Packet::BinaryReader reader(payload);
                    uint32_t senderId{};
                    std::string message;
                    if (reader.Read(senderId) && reader.ReadString(message))
                    {
                        std::cout << "[recv] Chat from " << senderId << ": " << message << '\n';
                    }
                    break;
                }
                case PacketId::W2CNotice:
                {
                    Packet::BinaryReader reader(payload);
                    std::string message;
                    if (reader.ReadString(message))
                    {
                        std::cout << "[recv] Notice(WorldServer 직접 브로드캐스트): " << message << '\n';
                    }
                    break;
                }
                case PacketId::Z2CTaskResult:
                    PrintTaskResult(payload);
                    break;
                default:
                    std::cout << "[recv] 알 수 없는 패킷 id=" << header.id
                              << " size=" << payload.size() << '\n';
                    break;
                }
            }
        }
    }

    // "echo foo bar" -> "foo bar" (첫 토큰 다음 공백 하나만 잘라내고 나머지는 그대로 보존)
    std::string RestOfLine(std::istringstream& iss)
    {
        std::string rest;
        std::getline(iss, rest);
        if (!rest.empty() && rest.front() == ' ')
        {
            rest.erase(0, 1);
        }
        return rest;
    }
}

int main(const int argc, char** argv)
{
    Log::Logger::Instance().Initialize("logs/protocolclient.log");

    try
    {
        const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
        const std::string port = argc > 2 ? argv[2] : "9000";

        asio::io_context ioContext;
        asio::ip::tcp::socket socket(ioContext);
        asio::ip::tcp::resolver resolver(ioContext);
        asio::connect(socket, resolver.resolve(host, port));

        std::cout << "[ProtocolClient] " << host << ":" << port << " 접속 완료\n";
        PrintHelp();

        std::atomic<bool> running{true};
        std::thread receiveThread([&socket, &running] { ReceiveLoop(socket, running); });

        std::string line;
        while (running.load() && std::getline(std::cin, line))
        {
            std::istringstream iss(line);
            std::string command;
            iss >> command;

            if (command == "quit" || command == "exit")
            {
                break;
            }
            if (command == "help")
            {
                PrintHelp();
            }
            else if (command == "echo")
            {
                const auto text = RestOfLine(iss);
                const auto* const bytes = reinterpret_cast<const byte*>(text.data());
                SendPacket(socket, PacketId::C2ZEcho, std::span(bytes, text.size()));
            }
            else if (command == "move")
            {
                Zone::MovePacket move{};
                iss >> move.x >> move.y;
                SendPacket(socket, PacketId::C2ZMove,
                           std::as_bytes(std::span(&move, 1)));
            }
            else if (command == "chat")
            {
                Packet::BinaryWriter writer;
                writer.WriteString(RestOfLine(iss));
                SendPacket(socket, PacketId::C2ZChat, writer.GetBuffer());
            }
            else if (command == "mail")
            {
                std::string sub;
                iss >> sub;
                if (sub == "add")
                {
                    std::string title;
                    std::string body;
                    int64_t durationSec{};
                    iss >> title >> body >> durationSec;

                    Packet::BinaryWriter writer;
                    writer.WriteString(title);
                    writer.WriteString(body);
                    writer.Write(durationSec);
                    SendPacket(socket, PacketId::C2ZMailAdd, writer.GetBuffer());
                }
                else if (sub == "del")
                {
                    uint32_t mailId{};
                    iss >> mailId;
                    SendPacket(socket, PacketId::C2ZMailDel,
                               std::as_bytes(std::span(&mailId, 1)));
                }
                else
                {
                    std::cout << "사용법: mail add <제목> <본문> <초> | mail del <id>\n";
                }
            }
            else if (!command.empty())
            {
                std::cout << "알 수 없는 명령어: " << command << " (help 입력)\n";
            }
        }

        running = false;
        std::error_code ec;
        socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        socket.close(ec);
        receiveThread.join();
    }
    catch (const std::exception& ex)
    {
        LOG.Error(ELogCategory::General, "치명적 오류").KV("Message", ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
