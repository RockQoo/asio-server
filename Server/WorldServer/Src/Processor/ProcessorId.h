#pragma once

#include "Server/Core/Src/Base/BasicTypes.h"

// 이 프로세스의 프로세서 목록. **프로세서는 스레드가 아니다** -- 하나의 큐 그룹(스레드 N개)
// 위에 여러 프로세서가 함께 얹히고, 어느 스레드에서 도는지는 메시지의 ownerId가 정한다
// (Server/Core/Src/Processor/Group.h 주석 참고). 그래서 Main과 Tool은 같은 BASIC
// 스레드들을 공유하며, ownerId가 같으면 서로 다른 프로세서의 메시지끼리도 같은 스레드에서
// 순서대로 처리된다 -- 그게 이 구조가 락 없이 성립하는 이유다.
//
// ELogCategory와 같은 이유로 Core가 아니라 여기 둔다: Core는 어떤 프로세서가 있는지
// 몰라야 한다. Count는 Group이 통계 배열 크기를 잡는 데 쓰므로 항상 마지막이다.
enum class EWorldProcessorId : uint8_t
{
    Network,// 링크에서 올라온 프레임의 1차 처리 -- NETWORK 레인.
            // **주인이 링크 세션 id다**(클라이언트가 아니라 게이트웨이/존 소켓). 껍질을 까
            // 안의 clientSessionId를 뽑아 BASIC으로 다시 넘기는 것까지가 이 단계다
    Main,   // 클라이언트 라우팅(PlayerManager / ZoneLinkRegistry) -- BASIC 그룹
    Login,  // 로그인/자동 가입의 뒷처리 -- BASIC 그룹(Main과 스레드를 공유한다).
            // 주인이 clientSessionId로 Main과 같아서, 로그인이 PlayerManager에 쓴 값을
            // 그 사람의 다음 패킷이 그대로 본다(다른 레인이면 그 사이가 레이스가 된다)
    Tool,   // 운영툴 명령 -- BASIC 그룹(Main과 스레드를 공유한다)
    Test,   // F키 테스트 메시지 -- BASIC 그룹(Main과 스레드를 공유한다).
            // 통계에서 테스트 트래픽이 실제 라우팅과 섞이지 않게 태그만 나눈 것이다
    Db,     // UnitOfWork 스트림 적재 + 로그인 계정 조회/생성 -- DB 레인(블로킹을 허용)
    Timer,  // 주기 작업의 만기 처리 -- TIMER 레인(스레드 1개).
            // **여기서 일을 하지 않는 것이 규약이다** -- 만기만 판정하고, 실제 작업은 그
            // 작업의 주인으로 원래 레인에 다시 넣는다(TimerProcessor.h 참고)
    Count,
};

[[nodiscard]] inline std::string_view ToString(const EWorldProcessorId processorId)
{
    switch (processorId)
    {
    case EWorldProcessorId::Network: return "Network";
    case EWorldProcessorId::Main:  return "Main";
    case EWorldProcessorId::Login: return "Login";
    case EWorldProcessorId::Tool:  return "Tool";
    case EWorldProcessorId::Test:  return "Test";
    case EWorldProcessorId::Db:    return "Db";
    case EWorldProcessorId::Timer: return "Timer";
    case EWorldProcessorId::Count: break;
    }
    return "Unknown";
}
