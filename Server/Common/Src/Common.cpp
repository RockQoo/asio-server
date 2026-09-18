#include "Server/Common/Src/PacketId.h"

// Common 은 지금 전부 헤더 전용(구조체 · enum · 상수)이라 컴파일 단위가 하나도 없다.
// 그대로 두면 .lib 에 공개 심볼이 없어 링커가 LNK4221 을 낸다 -- 이 파일은 그걸 막는
// 빌드 앵커다.
//
// **여기에 로직을 넣지 말 것.** 공용 계약에 함수가 필요해지면 그 계약의 헤더 옆에 자기
// .cpp 를 만든다. 이 파일이 잡동사니가 되면 "무엇이 어느 계약에 속하는가"가 흐려진다.
namespace Common
{
    namespace
    {
        // 헤더가 실제로 컴파일되는지 확인하는 용도도 겸한다.
        [[maybe_unused]] constexpr auto kBuildAnchor = PacketId::C2ZEcho;
    }
}
