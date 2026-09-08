#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/App/ZoneServerApp.h"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // 존 하나의 크기(월드 단위). 가로세로 같은 정사각형이라 격자가 눈으로 바로 읽힌다.
    constexpr float kZoneSize = 10.0f;

    // 한 행에 놓는 존 개수. 2면 존 4개가 2x2 격자가 된다:
    //
    //     y:[10,20)   존 1   존 2
    //     y:[0,10)    존 3   존 4
    //                 x:[0,10)  x:[10,20)
    //
    // 이 값을 바꾸면 배치가 바뀐다(1이면 세로 한 줄, 4면 가로 한 줄). 클라이언트도 같은 값을
    // 갖고 있어야 화면의 격자와 실제 핸드오프 지점이 맞는다 -- VisualClient의 ZoneLayout.cs.
    constexpr uint32_t kZonesPerRow = 2;

    // 격자의 행 개수. 행 0을 화면 위쪽(y가 큰 쪽)에 놓으려면 전체 높이를 알아야 해서 필요하다
    // -- 이 프로세스는 자기가 담당하는 존만 알기 때문에(예: "1,2") 전체 배치를 인자에서 알아낼
    // 수 없다. zoneId를 이 격자 밖으로 늘리려면(존 5 이상) 이 값을 같이 올려야 하고, 안 올리면
    // ParseZoneList가 그 존을 거부한다(월드 밖에 존을 만들어 조용히 어긋나는 것보다 낫다).
    constexpr uint32_t kZoneRows = 2;

    // "1,2" 같은 콤마 구분 zoneId 목록을 파싱해 ZoneDef 목록으로 만든다. 한 프로세스가 zoneId
    // 여러 개를 동시에 호스팅할 수 있다는 걸 보여주는 게 목적이라, 실행 인자 하나로 "이
    // 프로세스가 담당할 존 목록"을 그대로 config로 넘긴다.
    //
    // 좌표는 zoneId에서 계산한다(격자 규칙). 나중에 크기·위치가 불규칙한 배치가 필요해지면 이
    // 함수만 CSV 로더로 바꾸면 되고, 아래 계층(ZoneDef를 그대로 들고 다니는 구조)은 그대로다.
    //
    // **zoneId는 1부터 시작한다.** 0을 비워두는 이유는 "존 없음/미배정"을 값 하나로 나타낼
    // 자리가 필요하기 때문이다 -- 클라이언트는 입장 전 zoneId를 0으로 들고 있고, World도
    // 라우팅 대상을 못 찾은 상태를 0으로 구분할 수 있다. 0을 유효한 존으로 쓰면 "0번 존에
    // 있다"와 "아직 아무 존에도 없다"가 같은 값이 된다.
    struct ZoneListParseResult
    {
        std::vector<Zone::ZoneDef> zones;
        // 무시한 인자 토큰. 로거 초기화가 파싱보다 뒤라서(로그 파일명이 존 목록으로 정해진다)
        // 여기서 바로 못 찍고, main이 초기화 후에 경고로 남긴다.
        std::vector<std::string> rejectedTokens;
    };

    ZoneListParseResult ParseZoneList(const std::string& zoneIdListArg)
    {
        ZoneListParseResult result;
        std::stringstream ss(zoneIdListArg);
        std::string token;
        while (std::getline(ss, token, ','))
        {
            if (token.empty())
            {
                continue;
            }

            const auto zoneId = static_cast<uint32_t>(std::stoul(token));
            if (zoneId == 0)
            {
                // 여기서 걸러야 격자 첫 칸을 담당하는 존이 둘(0과 1) 생기는 사고를 막는다.
                result.rejectedTokens.push_back(token);
                continue;
            }

            const auto index = zoneId - 1;
            const auto column = index % kZonesPerRow;
            const auto row = index / kZonesPerRow;
            if (row >= kZoneRows)
            {
                result.rejectedTokens.push_back(token);
                continue;
            }

            Zone::ZoneDef def{};
            def.zoneId = zoneId;
            def.xMin = static_cast<float>(column) * kZoneSize;
            def.xMax = def.xMin + kZoneSize;
            // 행 0이 위쪽이다 -- y는 위로 증가하므로 행 번호가 커질수록 y가 작아진다.
            def.yMax = static_cast<float>(kZoneRows - row) * kZoneSize;
            def.yMin = def.yMax - kZoneSize;
            result.zones.push_back(def);
        }
        return result;
    }
}

int main(const int argc, char* argv[])
{
    // 실행 인자로 이 프로세스가 담당할 zoneId 목록을 콤마로 구분해서 받는다(예: "1,2"). 인자가
    // 없으면 기본값 "1" 하나만 담당한다.
    const std::string zoneIdListArg = argc > 1 ? argv[1] : "1";
    const auto [zones, rejectedTokens] = ParseZoneList(zoneIdListArg);

    // 로그 파일명은 이 프로세스가 담당하는 zoneId 목록으로 구분한다(예: zoneserver-1-2.log).
    // 담당 존이 하나도 없으면 파일명이 "zoneserver-.log"가 되어버리므로 따로 표시한다.
    std::string zoneIdSuffix;
    for (const auto& def : zones)
    {
        if (!zoneIdSuffix.empty())
        {
            zoneIdSuffix += "-";
        }
        zoneIdSuffix += std::to_string(def.zoneId);
    }
    if (zoneIdSuffix.empty())
    {
        zoneIdSuffix = "none";
    }
    Log::Logger::Instance().Initialize("logs/zoneserver-" + zoneIdSuffix + ".log");

    for (const auto& token : rejectedTokens)
    {
        LOG.Warning(ELogCategory::Zone,
                    "격자에 없는 zoneId라 무시했다(0은 '존 없음' 예약값, 상한은 kZonesPerRow*kZoneRows)")
            .KV("Token", token);
    }

    if (zones.empty())
    {
        // 담당 존이 없으면 World에 등록할 것도 없어서 붙어 있어도 아무 일도 하지 않는다.
        // 조용히 도는 대신 인자 오류로 끝낸다.
        LOG.Error(ELogCategory::Zone, "담당할 존이 없다. zoneId는 1부터 시작한다(예: 1,2)")
            .KV("Arg", zoneIdListArg);
        return 1;
    }

    try
    {
        Zone::ZoneServerConfig config{};
        config.zones = zones;
        // 아래 스레드 풀 크기는 전부 존 개수와 무관하게 설정 가능하다 -- 실서비스라면 훨씬
        // 크게 잡겠지만 여기선 학습용으로 작게 잡는다.
        config.lbThreadCount = 2;
        config.poolSizes.basicThreadCount = 2;
        config.poolSizes.tickThreadCount = 2;
        config.poolSizes.broadcastThreadCount = 2;
        config.worldHost = "127.0.0.1";
        config.worldPort = 9200;
        config.ioThreadCount = 2;
        config.tickInterval = std::chrono::milliseconds(100);
        config.mailSweepInterval = std::chrono::milliseconds(1000);

        Zone::ZoneServerApp app(std::move(config));
        app.Run();
    }
    catch (const std::exception& ex)
    {
        LOG.Error(ELogCategory::General, "치명적 오류").KV("Message", ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
