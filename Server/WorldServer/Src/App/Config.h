#pragma once

namespace World
{
    struct Config
    {
        uint16_t gatewayPort{9100};
        uint16_t zonePort{9200};
        // 운영툴(Tool/GmTool) 전용 accept 포트. 클라이언트 트래픽이 오는 게이트웨이 포트와
        // 분리해둬야 "운영 권한 패킷은 이 포트에서만 온다"가 성립한다(ToolProcessor 주석 참고).
        uint16_t toolPort{9300};
        size_t ioThreadCount{2};

        // BASIC 큐 그룹의 스레드 수. Main/Tool 프로세서가 이 스레드들을 **공유**하고, 어느
        // 스레드로 갈지는 메시지의 ownerId가 정한다(ProcessorId.h 주석 참고).
        // ClientRegistry의 샤드 개수가 이 값과 같아야 한다 -- 둘 다 `% N`으로 나누기 때문이다.
        size_t basicThreadCount{8};

        // DB 큐 그룹의 스레드 수. **커넥션 풀 크기와 1:1이 원칙이다** -- 스레드가 커넥션보다
        // 많으면 커넥션을 기다리며 노는 스레드가 생기고, 적으면 커넥션이 논다. 지금은 실제
        // DB가 붙어 있지 않아 개발 머신 기준의 임시값이고, 연동 후 **커넥션 대기 시간**을 재서
        // 조정한다(0이 아니면 커넥션 부족).
        size_t dbThreadCount{4};

        // 이 시간을 넘긴 작업은 경고 로그를 남긴다. 어느 프로세서가 레인을 태우는지 찾는 용도.
        std::chrono::microseconds slowTaskWarnThreshold{50000};  // 50ms

        // 레인 통계를 로그로 남기는 주기.
        std::chrono::milliseconds statsDumpInterval{10000};
        // 운영툴 링크의 공유 시크릿. 개발 기본값이며 실제 운영에서는 환경 변수
        // ASIO_SERVER_TOOL_SECRET로 덮어쓴다(main.cpp 참고).
        std::string toolSharedSecret{"dev-only-gmtool-secret"};

        // 게임 DB(asio_game) ODBC 연결 문자열. 위 시크릿과 같은 이유로 개발 기본값만 소스에 두고
        // 환경 변수 ASIO_SERVER_DB_CONN이 있으면 그걸 우선한다.
        //
        // **드라이버 버전을 박아둔 이유**: ODBC는 설치된 드라이버 이름을 문자열로 지정해야 하고,
        // 개발 머신에 깔린 것이 "ODBC Driver 17 for SQL Server"다. 18을 쓰는 환경이라면 이 값을
        // 환경 변수로 덮으면 된다(18은 기본이 암호화 연결이라 TrustServerCertificate가 필수다).
        std::string dbConnectionString{
            "Driver={ODBC Driver 17 for SQL Server};Server=127.0.0.1,1433;Database=asio_game;"
            "UID=asio_game;PWD=0000;TrustServerCertificate=yes;"};
    };

    // 설정 파일과 환경 변수를 읽어 Config 하나를 만든다.
    //
    // **기본값은 위 구조체에만 적는다** -- 읽는 쪽이 구조체의 현재 값을 그대로
    // fallback 으로 넘기므로, 기본값이 두 군데에 적혀 갈리는 일이 없다.
    // 형식과 정책(없는 키/틀린 값/오타 키)은 docs/design/config-file.md 참고.
    [[nodiscard]] Config LoadConfig(const std::string& path);
}
