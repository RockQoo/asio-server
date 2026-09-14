#pragma once

#include "Currency/Model.h"
#include "Mail/Model.h"

#include "Server/WorldServer/Src/Packet/ZoneLinkPackets.h"
#include "Shared/Core/Src/Packet/BinaryReader.h"

namespace Zone
{
    // World가 존에게 보내는 요청 중 **가변 길이 본문**을 가진 것. 고정 레이아웃만 있는 것은
    // World쪽 ZoneLinkPackets.h의 구조체를 그대로 memcpy하면 되므로 여기 둘 필요가 없다.
    //
    // C2Z 요청(Packet/ClientPackets.h)과 같은 규약이다: **파싱은 디스패치 앞에서 끝나고**,
    // 핸들러는 이미 해석된 구조체를 받는다.

    // 플레이어 한 명을 이 존에 들여보내는 요청. 신규 입장과 핸드오프가 **같은 패킷**이다
    // (World쪽 BuildEnterZoneBody가 두 경로에서 같은 바이트를 만든다).
    //
    // **콘텐츠가 늘 때마다 여기에 한 줄이 는다.** 우편·재화 다음에 아이템이 붙으면
    // `std::vector<Item::Info> items;`가 그 자리다. 고쳐야 할 곳은 셋뿐이다:
    //   ① 이 구조체의 필드   ② Parse의 해당 블록   ③ Zone::Player 생성자의 인자
    // 그리고 그 값은 **그 콘텐츠의 모델 생성자로만** 들어간다 -- 모델을 빈 채로 만들어 두고
    // 나중에 채우면 "아직 안 채워진 모델"을 누가 읽는 틈이 생긴다.
    //
    // 쓰는 쪽은 World의 Packet/EnterZoneBody.h이고, 바이트 포맷의 유일한 계약은
    // World의 Packet/ZoneLinkPackets.h에 있는 표다.
    struct W2ZEnterZone
    {
        World::PlayerZoneStatePacket state{};

        std::vector<Mail::Info> mails;
        std::vector<Currency::Info> currencies;

        [[nodiscard]] bool Parse(const std::span<const byte> payload)
        {
            if (payload.size() < sizeof(World::PlayerZoneStatePacket))
            {
                return false;
            }
            std::memcpy(&state, payload.data(), sizeof(World::PlayerZoneStatePacket));

            Packet::BinaryReader binaryReader(payload.subspan(sizeof(World::PlayerZoneStatePacket)));

            uint16_t mailCount{};
            if (!binaryReader.Read(mailCount))
            {
                return false;
            }

            mails.reserve(mailCount);
            for (uint16_t i = 0; i < mailCount; ++i)
            {
                Mail::Info info{};
                if (!binaryReader.Read(info.mailId) || !binaryReader.ReadString(info.title)
                    || !binaryReader.ReadString(info.body) || !binaryReader.Read(info.sendUt)
                    || !binaryReader.Read(info.endUt))
                {
                    return false;
                }
                mails.push_back(std::move(info));
            }

            uint16_t currencyCount{};
            if (!binaryReader.Read(currencyCount))
            {
                return false;
            }

            currencies.reserve(currencyCount);
            for (uint16_t i = 0; i < currencyCount; ++i)
            {
                Currency::Info info{};
                if (!binaryReader.Read(info.type) || !binaryReader.Read(info.amount))
                {
                    return false;
                }
                currencies.push_back(info);
            }

            return true;
        }
    };
}
