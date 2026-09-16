#include "pch.h"
#include "Player/PlayerMail.h"
#include "Player/Player.h"
#include "Player/MailModel.h"
#include "Task/ZoneUnitOfWork.h"

#include "Shared/Common/Src/ContentLimit.h"

namespace
{
    // 제목/본문 길이. **DB 컬럼(NVARCHAR)과 짝이고**, 넘기면 SP가 "String or binary data
    // would be truncated"로 실패한다 -- 메모리에는 들어갔는데 DB에는 없는 상태가 된다.
    // 그래서 모델에 넣기 전에 여기서 끊는다.
    [[nodiscard]] bool IsMailTextTooLong(const std::string& title, const std::string& body)
    {
        return title.size() > Common::kMaxMailTitleBytes
            || body.size() > Common::kMaxMailBodyBytes;
    }

    // 우편 하나를 만든다. Add와 Buy가 같은 본문을 쓰므로 한 곳에 모았다 -- 만료 시각
    // 계산이 갈리면 "산 우편만 안 사라진다" 같은 증상이 된다.
    [[nodiscard]] Common::MailInfo MakeMailInfo(std::string title, std::string body, const int64_t durationSec)
    {
        const auto nowUt = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        Common::MailInfo info{};
        info.title = std::move(title);
        info.body = std::move(body);
        info.sendUt = nowUt;
        info.endUt = nowUt + durationSec;
        return info;
    }
}

void PlayerMail::Register(PlayerPacketDispatcher& packetDispatcher)
{
    RegisterPacketHandler<Common::C2ZMailAdd>(packetDispatcher, PacketId::C2ZMailAdd, &PlayerMail::HandleMailAdd);
    RegisterPacketHandler<Common::C2ZMailDel>(packetDispatcher, PacketId::C2ZMailDel, &PlayerMail::HandleMailDel);
    RegisterPacketHandler<Common::C2ZMailBuy>(packetDispatcher, PacketId::C2ZMailBuy, &PlayerMail::HandleMailBuy);
}

void PlayerMail::HandleMailAdd(const PlayerContext& context, const Common::C2ZMailAdd& packet)
{
    // 스코프를 벗어나는 순간 소멸자가 결말을 낸다 -- 성공이면 World와 클라이언트로 전송,
    // 실패면 역순 롤백. 별도의 커밋 호출이 없다.
    ZoneUnitOfWork unitOfWork(context.worldLink, context.player, PacketId::C2ZMailAdd);

    const auto& mailBox = context.player.GetMailBox();
    if (!mailBox)
    {
        unitOfWork.SetError(EErrorCode::MailBoxNotFound);
        return;
    }

    if (IsMailTextTooLong(packet.title, packet.body))
    {
        unitOfWork.SetError(EErrorCode::MailTextTooLong);
        return;
    }

    if (const auto errorCode = mailBox->Write()->AddMail(
            MakeMailInfo(packet.title, packet.body, packet.durationSec), unitOfWork);
        errorCode != EErrorCode::Success)
    {
        unitOfWork.SetError(errorCode);
        return;
    }
}

void PlayerMail::HandleMailDel(const PlayerContext& context, const Common::C2ZMailDel& packet)
{
    ZoneUnitOfWork unitOfWork(context.worldLink, context.player, PacketId::C2ZMailDel);

    const auto& mailBox = context.player.GetMailBox();
    if (!mailBox)
    {
        unitOfWork.SetError(EErrorCode::MailBoxNotFound);
        return;
    }

    if (const auto errorCode = mailBox->Write()->DelMail(packet.mailId, unitOfWork, false);
        errorCode != EErrorCode::Success)
    {
        unitOfWork.SetError(errorCode);
        return;
    }
}

void PlayerMail::HandleMailBuy(const PlayerContext& context, const Common::C2ZMailBuy& packet)
{
    ZoneUnitOfWork unitOfWork(context.worldLink, context.player, PacketId::C2ZMailBuy);

    const auto& mailBox = context.player.GetMailBox();
    if (!mailBox)
    {
        unitOfWork.SetError(EErrorCode::MailBoxNotFound);
        return;
    }

    if (IsMailTextTooLong(packet.title, packet.body))
    {
        unitOfWork.SetError(EErrorCode::MailTextTooLong);
        return;
    }

    // 우편을 **먼저** 넣고 골드를 나중에 깎는 순서가 중요하다 -- 잔액이 부족하면 이미
    // 들어간 우편을 되돌려야 하고, 그게 이 프로젝트에서 역순 롤백이 실제로 밟히는
    // 유일한 경로다(단일 모델 요청은 실패 시점에 되돌릴 것이 없다).
    if (const auto errorCode = mailBox->Write()->AddMail(
            MakeMailInfo(packet.title, packet.body, packet.durationSec), unitOfWork);
        errorCode != EErrorCode::Success)
    {
        unitOfWork.SetError(errorCode);
        return;
    }

    if (const auto errorCode = context.player.GetWallet().DecCurrency(
            ECurrencyType::Gold, packet.price, unitOfWork);
        errorCode != EErrorCode::Success)
    {
        unitOfWork.SetError(errorCode);
        return;
    }
}
