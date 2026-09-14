#include "pch.h"
#include "Handler/PlayerMail.h"
#include "Game/Player.h"
#include "Mail/Model.h"
#include "Task/UnitOfWork.h"

namespace Zone
{
    namespace
    {
        // 우편 하나를 만든다. Add와 Buy가 같은 본문을 쓰므로 한 곳에 모았다 -- 만료 시각
        // 계산이 갈리면 "산 우편만 안 사라진다" 같은 증상이 된다.
        [[nodiscard]] Mail::Info MakeMailInfo(std::string title, std::string body, const int64_t durationSec)
        {
            const auto nowUt = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            Mail::Info info{};
            info.title = std::move(title);
            info.body = std::move(body);
            info.sendUt = nowUt;
            info.endUt = nowUt + durationSec;
            return info;
        }
    }

    void PlayerMail::Register(PlayerPacketDispatcher& packetDispatcher)
    {
        RegisterPacketHandler<C2ZMailAdd>(packetDispatcher, PacketId::C2ZMailAdd, &PlayerMail::HandleMailAdd);
        RegisterPacketHandler<C2ZMailDel>(packetDispatcher, PacketId::C2ZMailDel, &PlayerMail::HandleMailDel);
        RegisterPacketHandler<C2ZMailBuy>(packetDispatcher, PacketId::C2ZMailBuy, &PlayerMail::HandleMailBuy);
    }

    void PlayerMail::HandleMailAdd(const PlayerContext& context, const C2ZMailAdd& packet)
    {
        // 스코프를 벗어나는 순간 소멸자가 결말을 낸다 -- 성공이면 World와 클라이언트로 전송,
        // 실패면 역순 롤백. 별도의 커밋 호출이 없다.
        UnitOfWork unitOfWork(context.worldLink, context.player.GetSessionId(),
                              context.player.GetPlayerId(), PacketId::C2ZMailAdd);

        const auto& mailBox = context.player.GetMailBox();
        if (!mailBox)
        {
            unitOfWork.SetError(EErrorCode::MailBoxNotFound);
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

    void PlayerMail::HandleMailDel(const PlayerContext& context, const C2ZMailDel& packet)
    {
        UnitOfWork unitOfWork(context.worldLink, context.player.GetSessionId(),
                              context.player.GetPlayerId(), PacketId::C2ZMailDel);

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

    void PlayerMail::HandleMailBuy(const PlayerContext& context, const C2ZMailBuy& packet)
    {
        UnitOfWork unitOfWork(context.worldLink, context.player.GetSessionId(),
                              context.player.GetPlayerId(), PacketId::C2ZMailBuy);

        const auto& mailBox = context.player.GetMailBox();
        if (!mailBox)
        {
            unitOfWork.SetError(EErrorCode::MailBoxNotFound);
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
}
