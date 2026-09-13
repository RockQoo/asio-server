#pragma once

#include "Game/Def.h"
#include "Worker/WorkerManager.h"

namespace Zone
{
    struct Config
    {
        // 이 프로세스가 담당하는 존 목록 -- 리스트 길이만큼 이 프로세스가 존을 동시에 호스팅한다.
        std::vector<Def> zones;

        // 수신(LB) 레인 크기. 존 개수와 무관하다.
        size_t lbThreadCount{4};

        // 플레이어 레인 / 존 레인 / 브로드캐스트 레인 크기.
        PoolSizes poolSizes;

        std::string worldHost{"127.0.0.1"};
        uint16_t worldPort{9200};
        size_t ioThreadCount{2};
        std::chrono::milliseconds tickInterval{100};
        std::chrono::milliseconds mailSweepInterval{1000};

        // 이 시간을 넘긴 작업은 경고 로그를 남긴다. 어느 프로세서가 레인을 태우는지 찾는 용도.
        std::chrono::microseconds slowTaskWarnThreshold{50000};  // 50ms

        // 레인 통계를 로그로 남기는 주기. 0으로 두면 끈다.
        std::chrono::milliseconds statsDumpInterval{10000};
    };

    // 설정 파일을 읽어 Config 하나를 만든다. 담당 존 목록은 실행 인자에서 오므로 받아서 옮긴다.
    //
    // **기본값은 위 구조체에만 적는다** -- 읽는 쪽이 구조체의 현재 값을 그대로
    // fallback 으로 넘기므로, 기본값이 두 군데에 적혀 갈리는 일이 없다.
    // 형식과 정책(없는 키/틀린 값/오타 키)은 docs/design/config-file.md 참고.
    [[nodiscard]] Config LoadConfig(const std::string& path, std::vector<Def> zones);
}
