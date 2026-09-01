#include "ZoneServer/Src/pch.h"
#include "ZoneServer/Src/Mail/MailExpiryService.h"
#include "ZoneServer/Src/Mail/MailRegistry.h"
#include "ZoneServer/Src/Mail/MailUnitOfWork.h"

namespace Mail
{
    MailExpiryService::MailExpiryService(MailRegistry& mailRegistry, Zone::WorldLink& worldLink)
        : mailRegistry_(mailRegistry)
        , worldLink_(worldLink)
    {
    }

    void MailExpiryService::SweepOnce(const int64_t nowUt)
    {
        mailRegistry_.ForEach([this, nowUt](const Network::SessionId clientSessionId,
                                             const std::shared_ptr<MailModel::ARef>& mailModel)
        {
            // 이 타이머는 존 워커 스레드가 아니라 별도 유지보수 스레드에서 돈다 -- 그 사이
            // 존 워커가 같은 플레이어의 AddMail/DelMail을 호출할 수 있으므로, Write()의
            // shared_mutex(unique_lock)가 두 스레드를 실제로 직렬화한다. 조회(TakeExpiredMailIds)
            // 후 그 결과로 바로 삭제(DelMail)까지 이어가야 해서 잠금을 두 호출에 걸쳐 유지해야
            // 하므로, 매번 새 프록시를 만드는 임시 Write() 대신 named 프록시로 잠금을 유지한다.
            const auto writeProxy = mailModel->Write();
            const auto expiredIds = writeProxy->TakeExpiredMailIds(nowUt);
            if (expiredIds.empty())
            {
                return;
            }

            // clientSessionId를 그대로 playerId로도 쓰는 이 프로젝트의 단순화 규칙(WorldServer의
            // GatewayLinkHandler::HandleClientConnected 참고)을 그대로 따른다.
            const auto playerId = static_cast<uint32_t>(clientSessionId);
            auto unitOfWork = MakeMailUnitOfWork(worldLink_, clientSessionId, playerId);

            for (const auto mailId : expiredIds)
            {
                // 방금 TakeExpiredMailIds가 알려준 id라 항상 성공한다 -- ack를 보낼 대상도
                // 없으므로(만료 자동삭제) 반환값은 버린다.
                static_cast<void>(writeProxy->DelMail(mailId, unitOfWork, true));
            }
        });
    }
}
