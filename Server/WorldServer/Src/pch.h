#pragma once

// Core/ZoneServer와 동일한 이유로 무거운 서드파티/표준 헤더만 미리 컴파일한다.
// WorldServer 자체 헤더는 개발 중 자주 바뀌므로 PCH에 넣지 않는다.
#include <asio.hpp>

// byte/size_t/고정폭 정수를 std:: 없이 쓰기 위한 전역 using 선언 모음.
#include "Shared/Core/Src/Common/BasicTypes.h"

// LOG.Error(category, "메시지").KV(...) 형태의 전역 로그 진입점. 카테고리는 Core 것이 아니라
// WorldServer(콘텐츠) 자신의 World::ELogCategory를 쓴다.
#include "Shared/Core/Src/Log/LogProxy.h"
#include "Server/WorldServer/Src/Log/LogCategory.h"

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
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
