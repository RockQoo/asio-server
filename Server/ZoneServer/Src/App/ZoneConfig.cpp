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

    config.worldHost = file.GetString("world_host", config.worldHost);
    config.worldPort = file.GetPort("world_port", config.worldPort);
    config.ioThreadCount = file.GetSize("io_threads", config.ioThreadCount);

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

    config.lbThreadCount = file.GetSize("pools.lb_threads", config.lbThreadCount);
    config.basicThreadCount = file.GetSize("pools.basic_threads", config.basicThreadCount);
    config.broadcastThreadCount =
        file.GetSize("pools.broadcast_threads", config.broadcastThreadCount);

    config.tickInterval = file.GetMilliseconds("intervals.tick_ms", config.tickInterval);
    config.fanoutFlushInterval =
        file.GetMilliseconds("intervals.fanout_flush_ms", config.fanoutFlushInterval);
    config.statsDumpInterval =
        file.GetMilliseconds("intervals.stats_dump_ms", config.statsDumpInterval);

    file.WarnUnusedKeys();

    return config;
}
