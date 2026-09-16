#pragma once

// PushMsg가 구조체도 그대로 실어 나른다는 것을 보이는 예시 타입. trivially copyable +
// standard layout이면 BinaryWriter가 바이트째로 넣는다(Packet::ArgTrivial).
//
// **포인터나 std::string을 멤버로 넣지 말 것** -- 바이트 복사라 주소만 건너가서, 받는
// 쪽에서 남의 스레드의 수명 끝난 메모리를 가리키게 된다. 문자열은 별도 인자로 넘긴다.
struct TestValue
{
    int32_t hp{};
    float speed{};
};
