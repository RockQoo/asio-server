#pragma once

// Core/ZoneServer/WorldServer와 동일한 이유로 무거운 서드파티/표준 헤더만 미리 컴파일한다.
#include <asio.hpp>

#include "Core/Src/Common/BasicTypes.h"

#include "Core/Src/Log/LogProxy.h"
#include "GatewayServer/Src/Log/LogCategory.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>
