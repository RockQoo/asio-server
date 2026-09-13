#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace Gateway
{
    struct Config
    {
        uint16_t clientPort{9000};
        std::string worldHost{"127.0.0.1"};
        uint16_t worldPort{9100};
        size_t ioThreadCount{2};
    };

    // 설정 파일을 읽어 Config 하나를 만든다.
    //
    // **기본값은 위 구조체에만 적는다** -- 읽는 쪽이 구조체의 현재 값을 그대로
    // fallback 으로 넘기므로, 기본값이 두 군데에 적혀 갈리는 일이 없다.
    // 형식과 정책(없는 키/틀린 값/오타 키)은 docs/design/config-file.md 참고.
    [[nodiscard]] Config LoadConfig(const std::string& path);
}
