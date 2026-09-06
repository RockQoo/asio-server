#pragma once

// 무거운 서드파티/표준 헤더를 모아 미리 컴파일한다. 매 번 재컴파일되는 asio.hpp가
// 빌드 시간의 대부분을 차지하므로 여기 포함시켜 증분 빌드를 크게 단축시킨다.
// Core 자체 헤더는 개발 중 자주 바뀌어 PCH를 무효화시키므로 넣지 않는다.
#include <asio.hpp>

// byte/size_t/고정폭 정수를 std:: 없이 쓰기 위한 전역 using 선언 모음 (BasicTypes.h 자체
// 주석 참고 — pch.h 예외 대상).
#include "Shared/Core/Src/Common/BasicTypes.h"

// LOG.Error(category, "메시지").KV(...) 형태의 전역 로그 진입점 + Core 자신의 기본 카테고리
// (ELogCategory::Network/Packet/Thread/General). BasicTypes.h와 같은 이유로 PCH 예외 대상 —
// 안정적인 크로스커팅 인프라라 개발 중 자주 바뀌지 않는다.
#include "Shared/Core/Src/Log/LogProxy.h"
#include "Shared/Core/Src/Log/LogCategory.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
