#pragma once

namespace Console
{
    class KeyBinder;
}

namespace Zone
{
    // F키에 테스트 함수를 묶는다. **여기가 테스트를 늘리는 자리다** -- 새 함수를 만들고
    // 이 안에서 Bind 한 줄을 더하면 된다.
    void RegisterTestKeys(Console::KeyBinder& keyBinder);
}
