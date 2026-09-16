#include "pch.h"
#include "App/Config.h"

#include "Shared/Core/Src/Base/ConfigFile.h"

namespace World
{
    namespace
    {
        // 환경 변수 하나를 읽어 값이 있으면 돌려준다. std::getenv는 /sdl 아래에서 C4996으로
        // 걸리므로 getenv_s를 쓴다. 성공 시 length는 널 종단 문자를 포함하므로 값이 있으면 2 이상이다.
        [[nodiscard]] std::optional<std::string> ReadEnv(const char* name)
        {
            char buffer[1024]{};
            size_t length = 0;
            if (getenv_s(&length, buffer, sizeof(buffer), name) == 0 && length > 1)
            {
                return std::string(buffer);
            }
            return std::nullopt;
        }
    }

    Config LoadConfig(const std::string& path)
    {
        const auto file = Base::ConfigFile::Load(path);
        if (!file.IsLoaded())
        {
            LOG.Warning(ELogCategory::General, "설정 파일이 없어 기본값으로 뜬다").KV("Path", path);
        }

        Config config{};
        config.gatewayPort = file.GetPort("ports.gateway", config.gatewayPort);
        config.zonePort = file.GetPort("ports.zone", config.zonePort);
        config.toolPort = file.GetPort("ports.tool", config.toolPort);
        config.ioThreadCount = file.GetSize("io_threads", config.ioThreadCount);

        // BASIC은 Main/Tool 프로세서가 공유하는 큐 그룹이고, 실제 병렬도는 스레드 수가 아니라
        // **서로 다른 ownerId의 개수**로 정해진다(대부분 clientSessionId라 충분히 많다).
        config.basicThreadCount = file.GetSize("basic_threads", config.basicThreadCount);

        // DB는 커넥션 풀 크기와 1:1이 원칙이다. 실제 DB 연동 전이라 개발 머신 기준 임시값.
        config.dbThreadCount = file.GetSize("db_threads", config.dbThreadCount);

        file.WarnUnusedKeys();

        // **시크릿과 연결 문자열은 설정 파일이 아니라 환경 변수로 덮는다.** config/*.cfg 는
        // 저장소에 커밋되는 파일이라 실제 값이 들어가면 그대로 공개된다. 운영툴 쪽도 같은 이름의
        // 환경 변수(또는 appsettings)를 읽으므로 둘을 같이 바꿔야 한다.
        if (const auto secret = ReadEnv("ASIO_SERVER_TOOL_SECRET"))
        {
            config.toolSharedSecret = *secret;
            LOG.Info(ELogCategory::General, "운영툴 시크릿을 환경 변수에서 로드");
        }

        if (const auto connectionString = ReadEnv("ASIO_SERVER_DB_CONN"))
        {
            config.dbConnectionString = *connectionString;
            LOG.Info(ELogCategory::General, "DB 연결 문자열을 환경 변수에서 로드");
        }

        return config;
    }
}
