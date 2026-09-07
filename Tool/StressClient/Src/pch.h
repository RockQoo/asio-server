#pragma once

// Core와 동일한 이유로 무거운 서드파티/표준 헤더만 미리 컴파일한다.
#include <asio.hpp>

#include "Shared/Core/Src/Common/BasicTypes.h"

#include "Shared/Core/Src/Log/LogProxy.h"
#include "Tool/StressClient/Src/Log/LogCategory.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
