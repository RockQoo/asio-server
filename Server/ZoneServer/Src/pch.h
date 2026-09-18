#pragma once

// **이 프로젝트가 쓰는 표준 헤더는 전부 여기 있다.** 각 .cpp/.h 는 표준 헤더를 직접
// include 하지 않고 pch 만 include 한다.
//
// 예외는 SDK 헤더뿐이다 -- <sql.h>/<sqlext.h>/<bcrypt.h> 는 매크로를 수백 개 뿌리는데
// 쓰는 파일이 Db/ 몇 개뿐이라 그 파일에 남긴다. <windows.h> 는 반대로 여기 있다:
// asio.hpp 가 이미 전 TU 에 끌고 오므로 넣어도 달라지는 것이 없다.
//
// **5개 프로젝트의 pch 는 표준 헤더 목록이 같다.** Core 의 공개 헤더가 남의 프로젝트 안에서
// 컴파일되므로, 소비자 pch 에 그 헤더가 없으면 거기서 깨진다.
//
// **순서가 중요하다**: 표준 헤더가 아래의 프로젝트 헤더보다 **먼저** 와야 한다. Log/Proxy.h 가
// <fstream> 을 쓰는데 뒤에 두면 pch 를 만드는 동안 아직 못 본 상태로 파싱된다(실제로 겪었다).
//
// 프로젝트 자체 헤더는 개발 중 자주 바뀌어 PCH 를 무효화시키므로 넣지 않는다 -- 아래 둘은
// 예외로, 어디서나 쓰이면서 거의 바뀌지 않는 크로스커팅 인프라다.
#include <asio.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <concepts>
#include <condition_variable>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <exception>
#include <execution>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <shared_mutex>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#include <windows.h>

// byte/size_t/고정폭 정수를 std:: 없이 쓰기 위한 전역 using 선언 모음.
#include "Server/Core/Src/Base/BasicTypes.h"

// LOG.<Level>(category, "메시지").KV(...) 형태의 전역 로그 진입점.
#include "Server/Core/Src/Log/Proxy.h"

// 카테고리는 Core 것이 아니라 서버·도구가 공유하는 Common::ELogCategory 를 쓴다.
#include "Server/Common/Src/LogCategory.h"
