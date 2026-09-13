#include "pch.h"
#include "Cli/ConsoleLoop.h"

#include "App/App.h"

#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Protocol/Src/PacketId.h"

namespace World
{
    // 콘솔에서 "notice <메시지>"를 입력하면 WorldServer가 Zone을 거치지 않고 접속 중인 모든
    // 클라이언트에게 직접 브로드캐스트한다 -- World가 클라이언트 레지스트리를 직접 들고 있어서
    // 가능한 구조를 수동으로 확인하기 위한 REPL이다.
    void RunConsoleLoop(App& app, std::atomic<bool>& running)
    {
        std::string line;
        while (running.load() && std::getline(std::cin, line))
        {
            std::istringstream iss(line);
            std::string command;
            iss >> command;

            if (command == "notice")
            {
                std::string message;
                std::getline(iss, message);
                if (!message.empty() && message.front() == ' ')
                {
                    message.erase(0, 1);
                }

                Packet::BinaryWriter writer;
                writer.WriteString(message);
                app.BroadcastToAll(PacketId::W2CNotice, writer.GetBuffer());
                std::cout << "[notice 전송] " << message << "\n";
            }
            else if (command == "quit" || command == "exit")
            {
                running.store(false);
                app.Stop();
                break;
            }
        }
    }
}
