#pragma once

#include "Server/WorldServer/Src/Packet/ZoneLinkPackets.h"
#include "Server/WorldServer/Src/World/PlayerManager.h"

#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Core/Src/Packet/Header.h"

namespace World
{
    // W2ZEnterZone 본문을 만든다. 포맷은 ZoneLinkPackets.h 의 표가 유일한 계약이고,
    // 읽는 쪽은 ZoneServer 의 WorldLinkHandler 다.
    //
    // **쓰는 곳이 둘이라 함수로 뺐다** -- 신규 입장(LoginProcessor)과 핸드오프
    // (ZoneLinkHandler). 둘이 같은 바이트를 만들어야 존 쪽 파서가 하나로 끝난다.
    //
    // **바이트 예산으로 잘라낸다.** 우편함 통 수 상한(Common::kMaxMailCount)만으로는
    // 부족하다 -- 제목 128 + 본문 1024 바이트가 상한이라 **몇 통만으로도 8192를 넘는다.**
    // 넘긴 채로 보내면 받는 쪽 Packet::Buffer 가 예외를 던져 **Zone<->World 링크가 끊기고**,
    // 재연결이 없어서 그 프로세스의 존 전체가 서비스에서 빠진다(실제로 재현해서 확인했다).
    //
    // 자르는 순서는 **mailId 내림차순 = 최신순**이다(RUID 는 시간순으로 커진다). 오래된 것이
    // 잘리는 편이 덜 이상하고, 잘린 우편도 DB 와 World 캐시에는 그대로 남아 있다 -- 존이 그
    // 화면에 못 보여줄 뿐이다. 근본 해결(페이징)은 범위 밖이라 잘린 사실을 로그로 남긴다.
    [[nodiscard]] inline std::vector<byte> BuildEnterZoneBody(
        const W2ZEnterZone& enterZone,
        const std::unordered_map<Common::MailId, Common::MailInfo>& mails,
        const std::unordered_map<Common::ECurrencyType, int64_t>& currencies)
    {
        // 재화는 종류 수가 고정이라 먼저 자리를 잡아두고, 남는 예산을 우편에 준다.
        const size_t currencyBytes = sizeof(uint16_t) + currencies.size() * (sizeof(Common::ECurrencyType) + sizeof(int64_t));
        const size_t fixedBytes = sizeof(W2ZEnterZone) + sizeof(uint16_t) + currencyBytes;

        // 예산이 음수가 되지 않게 가드한다 -- 재화 종류가 폭발하면 우편을 0통 싣는다.
        const size_t maxBodyBytes = Packet::Header::MaxBodySize();
        const size_t mailBudget = fixedBytes < maxBodyBytes ? maxBodyBytes - fixedBytes : 0;

        // 최신순으로 정렬해서 예산이 허락하는 만큼만 고른다.
        std::vector<const Common::MailInfo*> sorted;
        sorted.reserve(mails.size());
        for (const auto& [mailId, info] : mails)
        {
            sorted.push_back(&info);
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const Common::MailInfo* left, const Common::MailInfo* right) { return right->mailId < left->mailId; });

        std::vector<const Common::MailInfo*> selected;
        selected.reserve(sorted.size());
        size_t usedBytes = 0;
        for (const auto* const info : sorted)
        {
            const size_t entryBytes = sizeof(Common::MailId::ValueType)
                + sizeof(uint16_t) + info->title.size()
                + sizeof(uint16_t) + info->body.size()
                + sizeof(int64_t) + sizeof(int64_t);
            if (usedBytes + entryBytes > mailBudget)
            {
                break;
            }
            usedBytes += entryBytes;
            selected.push_back(info);
        }

        if (selected.size() < mails.size())
        {
            LOG.Warning(ELogCategory::Zone, "EnterZone 본문이 상한을 넘어 오래된 우편을 잘라낸다")
                .KV("ClientSessionId", enterZone.clientSessionId)
                .KV("Total", mails.size()).KV("Sent", selected.size());
        }

        Packet::BinaryWriter binaryWriter;
        binaryWriter.Write(enterZone);

        binaryWriter.Write(static_cast<uint16_t>(selected.size()));
        for (const auto* const info : selected)
        {
            binaryWriter.Write(info->mailId);
            binaryWriter.WriteString(info->title);
            binaryWriter.WriteString(info->body);
            binaryWriter.Write(info->sendUt);
            binaryWriter.Write(info->endUt);
        }

        binaryWriter.Write(static_cast<uint16_t>(currencies.size()));
        for (const auto& [type, amount] : currencies)
        {
            binaryWriter.Write(type);
            binaryWriter.Write(amount);
        }

        return binaryWriter.MoveBuffer();
    }
}
