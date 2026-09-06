#include "Server/WorldServer/Src/pch.h"
#include "Server/WorldServer/Src/App/WorldServerApp.h"

#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Server/ZoneServer/Src/Packet/PacketId.h"

#include <atomic>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

namespace
{
    // 콘솔에서 "notice <메시지>"를 입력하면 WorldServer가 Zone을 거치지 않고 접속 중인 모든
    // 클라이언트에게 직접 브로드캐스트한다 -- World가 클라이언트 레지스트리를 직접 들고 있어서
    // 가능한 구조를 수동으로 확인하기 위한 REPL이다.
    void ConsoleLoop(World::WorldServerApp& app, std::atomic<bool>& running)
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
                app.BroadcastToAll(static_cast<uint16_t>(Zone::PacketId::Notice), writer.GetBuffer());
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

int main()
{
    // Debug 레벨은 기본적으로 끈다 -- UnitOfWork 태스크 로그처럼 요청량에 비례해 늘어나는
    // 항목이 있어(HandleUnitOfWorkStream 참고), 평소 실행에서는 Info부터만 남긴다. 상세히
    // 봐야 할 때만 ELogLevel::Debug로 바꿔서 실행할 것.
    Log::Logger::Instance().Initialize("logs/worldserver.log", Log::ELogLevel::Info);

    try
    {
        World::WorldServerConfig config{};
        config.gatewayPort = 9100;
        config.zonePort = 9200;
        config.ioThreadCount = 2;
        config.dbWorkerCount = 2;

        World::WorldServerApp app(std::move(config));

        std::atomic<bool> running{true};
        std::thread consoleThread(ConsoleLoop, std::ref(app), std::ref(running));
        consoleThread.detach();

        app.Run();
    }
    catch (const std::exception& ex)
    {
        LOG.Error(ELogCategory::General, "치명적 오류").KV("Message", ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
