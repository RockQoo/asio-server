#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Mail/MailExpiryService.h"
#include "Server/ZoneServer/Src/Mail/MailRegistry.h"
#include "Server/ZoneServer/Src/Task/ZoneUnitOfWork.h"

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
                                             const std::shared_ptr<MailModel::Mutexed>& mailModel)
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

            // 클라이언트 요청이 아니라 서버가 스스로 만든 변경이라 requestPacketId가 없는
            // 생성자를 쓴다 -- 클라이언트는 "요청한 적 없는 변경 통지"로 받아 자기 우편함에서
            // 그 메일들을 지운다.
            // writeProxy보다 나중에 선언해야 한다 -- 소멸은 역순이라, 이 UnitOfWork가 결말을
            // 내는 동안에도 writeProxy(unique_lock)가 아직 살아 있다. 롤백이 같은 MailModel을
            // 다시 Write()로 잠가도 RecursionGuard가 같은 스레드의 재진입을 건너뛰므로 데드락은
            // 나지 않는다(Mutexed.h 주석 참고).
            Zone::ZoneUnitOfWork unitOfWork(worldLink_, clientSessionId, playerId);

            for (const auto mailId : expiredIds)
            {
                // 방금 TakeExpiredMailIds가 알려준 id라 실패할 일이 없다 -- 그래도 반환값을
                // 버리지 않는 이유는, 실패했다면 조회와 삭제 사이에 누가 끼어들었다는 뜻이고
                // (불변식이 깨졌다는 신호) 조용히 넘기면 원인을 찾을 수 없기 때문이다.
                if (const auto errorCode = writeProxy->DelMail(mailId, unitOfWork, true);
                    errorCode != EErrorCode::Success)
                {
                    unitOfWork.SetError(errorCode);
                    return;
                }
            }
        });
    }
}
