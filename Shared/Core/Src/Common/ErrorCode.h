#pragma once

namespace Common
{
    // 앞으로 종류가 계속 늘어날 걸 감안해 int32_t로 넉넉하게 잡는다 (원소가 적은 일반 enum은
    // uint8_t가 기본이지만, 에러 코드는 예외로 둔다).
    enum class EErrorCode : int32_t
    {
        Success = 0,

        InvalidArgument,        // 생성자/함수에 전달된 인자가 유효 범위를 벗어남
        PacketTooLarge,         // 패킷 본문 크기가 PacketHeader::MaxBodySize()를 초과함
        ProtocolError,          // 그 외 프레이밍/프로토콜 위반
    };
}
