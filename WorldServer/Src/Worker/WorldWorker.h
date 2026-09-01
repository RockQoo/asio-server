#pragma once

#include "Core/Src/Thread/WorkerThread.h"

#include <utility>

namespace World
{
    // World 전체를 대표하는 단일 처리 스레드(풀이 아니라 인스턴스 하나). ClientRegistry/
    // ZoneLinkRegistry는 zoneId처럼 나눌 샤드가 없는 "세계에 하나뿐인 라우팅 테이블"이라
    // ZoneServer의 BASIC 풀처럼 여러 스레드로 쪼갤 이유가 없다 -- 대신 이 스레드 하나만
    // 건드리게 해서 락을 걷어낸다. Gateway/Zone 두 연결의 I/O 스레드(그리고 콘솔 REPL
    // 스레드)는 여기 PostTask로 작업만 넘기고, 실제 라우팅 로직은 전부 이 스레드에서 순서대로
    // 처리된다(WorldServerApp/GatewayLinkHandler/ZoneLinkHandler 참고).
    class WorldWorker
    {
    public:
        WorldWorker();

        void Start();
        void Stop();

        template <typename F>
        void PostTask(F&& task)
        {
            worker_.PostTask(std::forward<F>(task));
        }

    private:
        Thread::WorkerThread worker_;
    };
}
