#include "pch.h"
#include "Server/Core/Src/Pipeline/MessageProducer.h"

namespace
{
    // 레인 스레드에 이름을 붙인다. **디버거가 보는 이름**이라 Visual Studio의 [스레드]/
    // [병렬 스택] 창과 덤프에 `Basic#3`처럼 뜬다.
    //
    // **뜻이 백엔드마다 다르다** -- queue 면 `Basic#3`이 3번 레인의 주인이고, strand 면
    // 그냥 세 번째 일꾼이다(어느 레인을 처리할지 정해져 있지 않다).
    //
    // 이름이 ASCII라 단순 확장으로 충분하다(레인 이름은 EProducerType의 영문 이름이다).
    void ApplyThreadName(const std::string& name)
    {
        const std::wstring wide(name.begin(), name.end());
        ::SetThreadDescription(::GetCurrentThread(), wide.c_str());
    }
}

namespace Pipeline
{
    void QueueLaneSet::SetThreadName(const std::string& name) { ApplyThreadName(name); }
    void StrandLaneSet::SetThreadName(const std::string& name) { ApplyThreadName(name); }
}
