#pragma once

#include <cstddef>
#include <cstdint>

// 코딩 컨벤션(cpp-patterns.md 참고): byte/size_t 및 고정폭 정수 계열은 std:: 접두사 없이
// 쓴다. 이 별칭들은 앞으로도 바뀔 일이 없는 순수 표준 타입 재노출이라, "Core 자체 헤더는
// 자주 바뀌니 PCH에 넣지 않는다"는 원칙의 예외로 두고 pch.h가 직접 include한다.
using std::byte;
using std::size_t;

using std::int8_t;
using std::int16_t;
using std::int32_t;
using std::int64_t;

using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
