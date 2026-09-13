#include "Server/GatewayServer/Src/pch.h"
#include "Server/GatewayServer/Src/App/Config.h"

#include "Shared/Core/Src/Common/ConfigFile.h"

namespace Gateway
{
    Config LoadConfig(const std::string& path)
    {
        const auto file = Common::ConfigFile::Load(path);
        if (!file.IsLoaded())
        {
            LOG.Warning(ELogCategory::General, "설정 파일이 없어 기본값으로 뜬다").KV("Path", path);
        }

        Config config{};
        config.clientPort = file.GetPort("client_port", config.clientPort);
        config.worldHost = file.GetString("world_host", config.worldHost);
        config.worldPort = file.GetPort("world_port", config.worldPort);
        config.ioThreadCount = file.GetSize("io_threads", config.ioThreadCount);

        file.WarnUnusedKeys();

        return config;
    }
}
