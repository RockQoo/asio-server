#include "pch.h"
#include "App/ZoneConfig.h"

#include "Server/Core/Src/Base/ConfigFile.h"

ZoneConfig LoadConfig(const std::string& path, std::vector<ZoneDef> zones)
{
    const auto file = Base::ConfigFile::Load(path);
    if (!file.IsLoaded())
    {
        LOG.Warning(ELogCategory::General, "설정 파일이 없어 기본값으로 뜬다").KV("Path", path);
    }

    ZoneConfig config{};
    config.zones = std::move(zones);

    // 레인마다 크기를 정하는 기준이 다르다:
    //   Player      -- owner가 clientSessionId라 실질 병렬도가 접속자 수만큼이다.
    //                  스레드를 늘린 만큼 실제로 갈린다. 수신 파싱도 이 레인이 한다.
    //   Zone        -- **담당 존 수만큼.** 존 하나는 스레드 하나가 상한이라, 더 줘도
    //                  그 존이 빨라지지 않는다(버거우면 존을 쪼갠다). 그래서 설정에서
    //                  0을 주면 존 개수로 맞춘다 -- 프로세스마다 담당 존 수가 다르다.
    //   Broadcast   -- World 링크가 하나라 어차피 그 소켓에서 직렬화된다.
    config.poolSizes.playerThreadCount =
        file.GetSize("pools.player_threads", config.poolSizes.playerThreadCount);
    config.poolSizes.broadcastThreadCount =
        file.GetSize("pools.broadcast_threads", config.poolSizes.broadcastThreadCount);

    const auto zoneThreads = file.GetSize("pools.zone_threads", 0);
    config.poolSizes.zoneThreadCount = zoneThreads > 0 ? zoneThreads : config.zones.size();

    config.worldHost = file.GetString("world_host", config.worldHost);
    config.worldPort = file.GetPort("world_port", config.worldPort);
    config.ioThreadCount = file.GetSize("io_threads", config.ioThreadCount);
    config.tickInterval = file.GetMilliseconds("intervals.tick_ms", config.tickInterval);
    config.mailSweepInterval =
        file.GetMilliseconds("intervals.mail_sweep_ms", config.mailSweepInterval);
    config.statsDumpInterval =
        file.GetMilliseconds("intervals.stats_dump_ms", config.statsDumpInterval);
    config.slowTaskWarnThreshold =
        file.GetMicroseconds("slow_task_warn_us", config.slowTaskWarnThreshold);

    file.WarnUnusedKeys();

    return config;
}
