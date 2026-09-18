#pragma once

#include "Server/Core/Src/Base/BasicTypes.h"

namespace Pipeline
{
    // 메시지 하나를 보낼 때 정해지는 좌표는 **서로 독립인 셋**이다.
    //
    //   msgType            어느 **함수**   프로세서 안 핸들러 표의 키
    //   targetProcessorId  어느 **객체**   그 레인이 든 프로세서 배열의 인덱스
    //   ownerId            어느 **스레드** `hash(ownerId) % 레인 수`
    //
    // 이 파일은 뒤의 둘을 타입으로 가른다. **둘 다 정수로 두면 자리를 바꿔 넣어도 컴파일이
    // 되고**, 여기서 헷갈리면 흐름이 통째로 무너지는데 증상은 "엉뚱한 스레드에서 처리됨"이라
    // 추적이 어렵다.

    // "누가" 처리할지 -- 프로세서 배열의 인덱스. AddProcessor가 돌려준다.
    struct ProcessorId final
    {
        int32_t value{-1};

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value >= 0; }
    };

    // "어느 스레드에서" 처리할지 -- 직렬화 단위. 같은 값이면 같은 레인(strand)이다.
    struct OwnerId final
    {
        int64_t value{};
    };

    // 주인 하나가 어느 레인으로 가는지. **이 규칙의 정의는 여기 하나뿐이어야 한다.**
    //
    // 레인 밖에서 같은 규칙으로 샤딩하는 자료구조(존 서버의 플레이어 레지스트리처럼 락 없이
    // 쓰려고 나눠 둔 것)가 반드시 이 함수를 불러야 한다. 두 군데서 따로 계산하면 "같은
    // 샤드인데 다른 레인"이 생기고, 그러면 락 없는 맵에 두 스레드가 동시에 들어간다 --
    // 크래시가 아니라 조용히 깨지는 쪽이다.
    //
    // **useHash 를 끄는 경우**: 주인 값이 0부터 촘촘한 서수일 때다. 그때는 나머지 연산이
    // `ordinal % N == ordinal` 이라 **1:1이 보장된다**. 해시를 쓰면 오히려 흩뜨려서 서로 다른
    // 서수가 같은 레인에 얹힐 수 있다.
    //
    // 반대로 주인이 드문드문한 값(세션 id, playerId)이면 켜 둔다 -- 나머지 연산만으로는
    // 하위 비트가 편중될 수 있다.
    [[nodiscard]] inline size_t LaneIndexOf(const OwnerId owner, const size_t laneCount,
                                            const bool useHash) noexcept
    {
        const auto key = useHash ? std::hash<int64_t>()(owner.value)
                                 : static_cast<size_t>(static_cast<uint64_t>(owner.value));
        return key % laneCount;
    }

    // 레인 하나 = **스레드 풀 하나 + 프로세서 목록 하나**.
    //
    // **왜 Core에 있고 불변 규칙 1을 깨지 않는가**: 값 전부가 "메시지가 거치는 단계"이지
    // 이 게임의 개념이 아니다. Db는 "블로킹 I/O를 허용하는 레인"이라는 뜻일 뿐 어떤 테이블이
    // 있는지 모르고, Tick은 "주기 실행"이지 존을 모른다.
    //
    // **서버마다 쓰는 값이 다르다.** 안 쓰는 값은 InitProducer를 부르지 않으면 그만이다.
    //   World -- Basic / Db / Timer
    //   Zone  -- Lb / Basic / Tick / Broadcast / Timer
    //
    // **NETWORK 레인이 없는 것은 빠뜨린 것이 아니다.** 레인 구성을 일관되게 하려면 소켓 단계도
    // 레인 하나여야 하고, 그 레인이 할 일은 수신(완료를 받아 프레임 조립)과 송신(세션 단위
    // 직렬화) 둘이다. **그런데 asio가 그 둘을 이미 갖고 있다**: 수신은 io_context, 송신
    // 직렬화는 Session의 strand다. 레인을 또 얹으면 같은 일을 하는 장치가 두 겹이 되고,
    // 겹치는 자리(전송 완료 콜백 · 연결 종료)마다 "누가 주인이냐"를 정해줘야 한다.
    //
    // 나머지 레인은 asio에 대응물이 없어서 그대로 두면 된다 -- 겹치는 층은 여기 하나뿐이다.
    // 무엇을 잃는지와 되살릴 때의 조건: docs/design/network-lane.md
    //
    // **선언 순서 = 흐름 순서 = 종료 순서**다. 앞 단계를 먼저 끊어야 뒤 단계가 밀린 것을
    // 마저 받아 처리한다(Stop은 큐를 소진한 뒤 join한다).
    //
    // 알려진 문제 -- TIMER는 만기를 다른 모든 레인으로 되돌리므로 흐름상 가장 앞이지만
    // 선언은 맨 뒤다. 그래서 Basic/Db가 닫힌 뒤 깨어난 만기는 갈 곳이 없다. 지금은 붙은
    // 주기 작업이 없어 증상이 없고, 실제 작업을 붙일 때 같이 정리한다.
    enum class EProducerType : uint8_t
    {
        Lb,         // 수신 1차 처리 -- 껍질을 까고 주인을 찾아 다음 레인으로 넘긴다
        Basic,      // 콘텐츠 본체. 판단하고 예약한다
        Tick,       // 박자. 예약된 것을 실행한다
        Broadcast,  // 발송 전담. 모아서 한꺼번에 내보낸다
        Db,         // 영속화. **이 레인만 블로킹을 허용한다**
        Timer,      // 만기 판정만. 일 자체는 등록한 레인으로 되돌린다
        Max,
    };

    // 레인을 굴리는 방식. **둘 다 구현해 두고 config로 고른다** -- 지우지 않는다.
    // 차이는 하나, **어피니티 vs 워크 스틸링**이다(자세한 건 ILaneSet.h).
    enum class ELaneBackend : uint8_t
    {
        Queue,   // 스레드 1개 + 큐 1개 + condvar. 스레드↔레인 1:1 (어피니티)
        Strand,  // asio::strand. 한가한 스레드가 아무 레인이나 집어간다
    };

    [[nodiscard]] inline std::string_view ToString(const ELaneBackend backend)
    {
        switch (backend)
        {
        case ELaneBackend::Queue:  return "queue";
        case ELaneBackend::Strand: return "strand";
        }
        return "Unknown";
    }

    // 설정 파일의 문자열을 레인 백엔드로. 모르는 값이면 nullopt -- 설정이 틀렸는데 조용히
    // 기본값으로 돌면 "어느 쪽으로 잰 수치인가"를 믿을 수 없게 된다.
    [[nodiscard]] inline std::optional<ELaneBackend> ParseLaneBackend(const std::string_view text)
    {
        if (text == "queue")
        {
            return ELaneBackend::Queue;
        }
        if (text == "strand")
        {
            return ELaneBackend::Strand;
        }
        return std::nullopt;
    }

    // 레인 이름. 그대로 스레드 이름(`Basic#0`)과 로그에 쓰인다.
    [[nodiscard]] inline std::string_view ToString(const EProducerType producerType)
    {
        switch (producerType)
        {
        case EProducerType::Lb:        return "Lb";
        case EProducerType::Basic:     return "Basic";
        case EProducerType::Tick:      return "Tick";
        case EProducerType::Broadcast: return "Broadcast";
        case EProducerType::Db:        return "Db";
        case EProducerType::Timer:     return "Timer";
        case EProducerType::Max:       break;
        }
        return "Unknown";
    }
}
