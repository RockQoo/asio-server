#include "pch.h"
#include "App/WorldConfig.h"

#include "Server/Core/Src/Base/ConfigFile.h"

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

WorldConfig LoadConfig(const std::string& path)
{
    const auto file = Base::ConfigFile::Load(path);
    if (!file.IsLoaded())
    {
        LOG.Warning(ELogCategory::General, "설정 파일이 없어 기본값으로 뜬다").KV("Path", path);
    }

    WorldConfig config{};
    config.gatewayPort = file.GetPort("ports.gateway", config.gatewayPort);
    config.zonePort = file.GetPort("ports.zone", config.zonePort);
    config.toolPort = file.GetPort("ports.tool", config.toolPort);
    config.ioThreadCount = file.GetSize("io_threads", config.ioThreadCount);

    // NETWORK는 수신 1차 처리 레인이고 주인이 **링크 세션**이라, 실질 병렬도가 붙어 있는
    // 게이트웨이/존 프로세스 수까지다 -- 스레드를 늘려도 그 이상 갈라지지 않는다.
    config.networkThreadCount = file.GetSize("network_threads", config.networkThreadCount);

    // 레인 백엔드. 모르는 값이면 기본값으로 넘어가지 않고 경고를 남긴다 -- 어느 쪽으로 잰
    // 수치인지 믿을 수 없게 되는 것이 조용한 오타보다 나쁘다.
    {
        const auto backendText = file.GetString("lane_backend", std::string(Pipeline::ToString(config.laneBackend)));
        if (const auto parsed = Pipeline::ParseLaneBackend(backendText))
        {
            config.laneBackend = *parsed;
        }
        else
        {
            LOG.Warning(ELogCategory::General, "알 수 없는 lane_backend, 기본값을 쓴다")
                .KV("Value", backendText).KV("Default", Pipeline::ToString(config.laneBackend));
        }
    }

    // BASIC은 Main/Login/Tool/Test 프로세서가 공유하는 레인이고, 실제 병렬도는 스레드 수가
    // 아니라 **서로 다른 ownerId의 개수**로 정해진다(대부분 clientSessionId라 충분히 많다).
    config.basicThreadCount = file.GetSize("basic_threads", config.basicThreadCount);

    // DB는 커넥션 풀 크기와 1:1이 원칙이다. 실제 DB 연동 전이라 개발 머신 기준 임시값.
    config.dbThreadCount = file.GetSize("db_threads", config.dbThreadCount);

    // **Zone 과 같은 키 이름을 쓴다** -- 부하를 볼 때 두 서버의 설정을 나란히 놓고 고치는데,
    // 이름이 갈리면 한쪽만 바꿔 놓고 양쪽을 바꿨다고 착각한다.
    config.statsDumpInterval =
        file.GetMilliseconds("intervals.stats_dump_ms", config.statsDumpInterval);
    config.slowTaskWarnThreshold =
        file.GetMicroseconds("slow_task_warn_us", config.slowTaskWarnThreshold);

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
