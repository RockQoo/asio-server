#pragma once

#include <cstdint>

namespace Zone
{
    class WorldLink;
}

namespace Mail
{
    class MailRegistry;

    // 존 워커(로직 스레드)에 속하지 않는 별도 유지보수 타이머가 주기적으로 SweepOnce를
    // 호출해 만료된 메일을 지운다. 이게 바로 Mutexed가 실제로 방어 역할을 하는 유일한
    // 지점이다: 이 스윕과 존 로직 스레드의 AddMail/DelMail(UnitOfWork 경유)이 같은 MailModel
    // 인스턴스를 동시에 건드릴 수 있기 때문이다. 나머지 게임 상태(PlayerState 등)는 여전히
    // 존 로직 스레드 전용이라 락이 없다는 것과 대비된다.
    class MailExpiryService
    {
    public:
        MailExpiryService(MailRegistry& mailRegistry, Zone::WorldLink& worldLink);

        void SweepOnce(const int64_t nowUt);

    private:
        MailRegistry& mailRegistry_;
        Zone::WorldLink& worldLink_;
    };
}
