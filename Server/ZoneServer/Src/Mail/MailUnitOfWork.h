#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Task/UnitOfWork.h"

#include <cstdint>

namespace Zone
{
    class WorldLink;
}

namespace Mail
{
    // Task::UnitOfWork 생성 시 넘길 flush 콜백(태스크를 다 모은 뒤 World로 UnitOfWorkStream
    // 패킷 하나로 보내는 로직)을 만들어준다 -- ZoneWorld의 MailAdd/MailDel 처리와
    // MailExpiryService가 이 로직을 공유한다.
    [[nodiscard]] Task::UnitOfWork MakeMailUnitOfWork(Zone::WorldLink& worldLink,
                                                      const Network::SessionId clientSessionId,
                                                      const uint32_t playerId);
}
