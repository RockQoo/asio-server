#include "pch.h"
#include "Server/Core/Src/Pipeline/MessageProducer.h"

namespace Pipeline
{
    // 레인 스레드에 이름을 붙인다. **디버거가 보는 이름**이라 Visual Studio의 [스레드]/
    // [병렬 스택] 창과 덤프에 `Basic#3`처럼 뜬다.
    //
    // **이 한 줄 때문에 asio::thread_pool을 쓰지 않는다.** thread_pool은 스레드 생성 훅이
    // 없어서 이름을 못 붙이고, 그러면 **대기 중인 스레드가 어느 레인인지 구분되지 않는다**
    // (모든 풀이 asio::detail::scheduler::run으로 똑같이 보인다). 이 저장소는 스레드 배분이
    // 제대로 가는지 보는 것이 목적이라 그 손실이 코드 몇 줄보다 비싸다.
    //
    // 이름이 ASCII라 단순 확장으로 충분하다(레인 이름은 EProducerType의 영문 이름이다).
    void MessageProducer::SetThreadName(const std::string& name)
    {
        const std::wstring wide(name.begin(), name.end());
        ::SetThreadDescription(::GetCurrentThread(), wide.c_str());
    }
}
