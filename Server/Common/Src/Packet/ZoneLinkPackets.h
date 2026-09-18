#pragma once

#include "Server/Common/Src/PacketId.h"

#include "Server/Common/Src/Ids.h"

#include "Server/Core/Src/Packet/BinaryReader.h"

namespace Common
{
#pragma pack(push, 1)
    // 존이 자기 담당 사각형을 알려온다. World는 이 사각형만으로 라우팅하므로 존 배치 규칙
    // (격자든 CSV든)을 알 필요가 없다 -- 배치를 바꿔도 World 코드는 그대로다.
    struct Z2WZoneRegister
    {
        static constexpr PacketId kPacketId = PacketId::Z2WZoneRegister;
        ZoneId zoneId;
        float xMin;
        float xMax;
        float yMin;
        float yMax;
    };

    // 플레이어 한 명을 이 존에 들여보낸다. **이건 고정 머리뿐이고** 뒤에 그 사람의 콘텐츠가
    // 가변 길이로 붙는다(포맷 표는 이 파일 아래쪽). 신규 입장과 핸드오프가 같은 패킷이다.
    struct W2ZEnterZoneHead
    {
        // **목표 존.** 한 Zone 프로세스가 존 여러 개를 호스팅하므로 어디로 들어갈지 명시한다.
        ZoneId zoneId;
        uint64_t clientSessionId;

        // 로그인이 확정한 DB의 player_id(RUID). **clientSessionId 파생값이 아니다** --
        // 예전에는 uint32였고 세션 id를 잘라 넣고 있어서 재접속할 때마다 값이 바뀌었다.
        PlayerId playerId;

        float x;
        float y;
    };

    // 존이 "이 플레이어가 내 사각형을 벗어났다"고 올린다. **레이아웃은 W2ZEnterZone과 같지만
    // zoneId의 뜻이 반대다** -- 목표 존이 아니라 보내는 쪽(현재) 존이고, World 로그용일 뿐
    // 실제 라우팅 대상은 x/y로 정해진다. 그래서 구조체를 따로 둔다(ToEnterZone이 그 경계다).
    struct Z2WZoneTransfer
    {
        static constexpr PacketId kPacketId = PacketId::Z2WZoneTransfer;
        ZoneId zoneId;  // 보내는 쪽(현재) 존
        uint64_t clientSessionId;
        PlayerId playerId;
        float x;
        float y;
    };

    struct W2ZLeaveZone
    {
        static constexpr PacketId kPacketId = PacketId::W2ZLeaveZone;
        uint64_t clientSessionId;
    };
#pragma pack(pop)

    // 핸드오프 요청을 입장 요청으로 바꾼다. **zoneId의 뜻이 "보낸 존"에서 "목표 존"으로
    // 뒤집히는 지점**이라, 목표를 인자로 받아 반드시 덮어쓰게 했다 -- 같은 레이아웃이라고
    // 그냥 재해석하면 존이 자기가 보낸 zoneId로 되돌아오는 버그가 조용히 생긴다.
    [[nodiscard]] inline W2ZEnterZoneHead ToEnterZoneHead(const Z2WZoneTransfer& transfer,
                                                          const ZoneId targetZoneId) noexcept
    {
        return W2ZEnterZoneHead{targetZoneId, transfer.clientSessionId, transfer.playerId,
                            transfer.x, transfer.y};
    }

    // ---- W2ZEnterZone 의 가변 길이 꼬리 ----
    //
    // 고정 머리(W2ZEnterZone) 뒤에 **World 가 캐시하고 있는 그 플레이어의 콘텐츠**가
    // 이어진다. 존은 이걸 받아 Mail/Currency 모델을 채운 상태로 Player 를 만든다.
    //
    //   uint16  mailCount
    //     반복: int64 mailId(RUID), string title, string body, int64 sendUt, int64 endUt
    //   uint16  currencyCount
    //     반복: uint8 currencyType, int64 amount
    //
    // (string = uint16 길이 + UTF-8 바이트. Packet::BinaryWriter::WriteString 형식.)
    //
    // **왜 존이 DB 를 직접 안 읽고 World 가 실어 보내는가**: DB 로 나가는 관문을 World 하나로
    // 유지하기 위해서다. 존이 직접 읽기 시작하면 커넥션 관리가 두 프로세스로 갈리고, 나중에
    // 존이 올린 UnitOfWork 를 World 캐시와 대조해 위조를 거르는 자리가 사라진다.
    //
    // **핸드오프에도 같은 포맷이 쓰인다** -- 존을 넘을 때 World 가 캐시에서 다시 실어 보낸다.
    //
    // **캐시는 최신이다** -- 존이 올린 UnitOfWork 를 World 가 BASIC 레인에서 계속 반영한다
    // (ZoneLinkHandler 의 HandleUnitOfWorkStream). 스트림과 핸드오프 요청이 같은 TCP 링크로
    // 오고 같은 주인(clientSessionId)의 같은 strand 에서 처리되므로, 직전에 만든 우편도 이미
    // 캐시에 들어 있다.
    //
    // 세로 이동에서는 World 가 원본 링크에 W2ZLeaveZone 도 보낸다 -- 안 보내면 그쪽 프로세스에
    // Player 와 우편함이 유령으로 남는다. 가로 이동은 Player 객체가 그대로 살아 있으므로
    // 보내지 않고, 이 본문의 콘텐츠도 쓰이지 않는다(살아 있는 모델이 권위다).
    //
    // 쓰는 쪽은 Packet/EnterZoneBody.h, 읽는 쪽은 ZoneServer 의 WorldLinkHandler 다.
    // 한쪽만 고치면 조용히 어긋나므로 이 표가 유일한 계약이다.

    // UnitOfWork 스트림 안의 태스크 하나. `kind` 의 뜻은 Common::TaskKind.h 가 정하고,
    // `payload` 의 내용은 그 kind 가 정한다 -- 여기서는 해석하지 않는다.
    //
    // **수명 주의**: `payload` 는 수신 버퍼를 가리키는 subspan 이다(복사 아님).
    struct TaskRecord
    {
        uint16_t kind{};
        std::span<const byte> payload;
    };

    // 존이 적용한 변경 묶음. 바이트 포맷:
    //
    //   int64 playerId, int64 requestId, uint64 ownerId, uint16 taskCount
    //     반복: uint16 kind, uint32 payloadLen, payloadLen 바이트
    //
    // **전부 읽히지 않으면 false 다(부분 성공이 없다).** 이게 이 구조체를 만든 이유다 --
    // 예전에는 핸들러가 루프 안에서 읽다가 잘리면 `break` 했는데, 그 루프가 이미 열린
    // 트랜잭션(AutoSpCommands) 안이라 **앞쪽 태스크만 커밋되고 뒤쪽은 조용히 사라졌다.**
    // 우편 지급 + 골드 차감 중 차감만 남는 식이다. 파싱을 트랜잭션 **앞**으로 빼면
    // 잘린 스트림은 아무것도 적용하지 않고 버려진다.
    //
    // **모르는 kind 는 여기서 막지 않는다.** 길이 프리픽스 덕에 건너뛸 수 있어서, 서버
    // 버전이 섞여도 아는 태스크는 정상 처리된다. "아는 종류인가"는 핸들러가 판단한다.
    struct Z2WUnitOfWorkStream
    {
        static constexpr PacketId kPacketId = PacketId::Z2WUnitOfWorkStream;

        PlayerId playerId{};
        int64_t requestId{};
        uint64_t ownerId{};
        std::vector<TaskRecord> tasks;

        [[nodiscard]] bool Parse(const std::span<const byte> payload)
        {
            Packet::BinaryReader binaryReader(payload);
            uint16_t taskCount{};
            if (!binaryReader.Read(playerId) || !binaryReader.Read(requestId)
                || !binaryReader.Read(ownerId) || !binaryReader.Read(taskCount))
            {
                return false;
            }

            tasks.reserve(taskCount);
            for (uint16_t i = 0; i < taskCount; ++i)
            {
                TaskRecord taskRecord;
                uint32_t payloadLen{};
                if (!binaryReader.Read(taskRecord.kind) || !binaryReader.Read(payloadLen))
                {
                    return false;
                }

                const auto taskPayload = binaryReader.ReadBytes(payloadLen);
                if (!taskPayload)
                {
                    return false;
                }

                taskRecord.payload = *taskPayload;
                tasks.push_back(taskRecord);
            }

            return true;
        }
    };
}
