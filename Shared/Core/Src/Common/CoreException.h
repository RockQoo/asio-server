#pragma once

#include "Shared/Core/Src/Common/CoreErrorCode.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace Common
{
    // 내부 검증 실패를 신호하는 표준 예외. ex.what() 문자열을 파싱해서 에러 종류를 구분하는
    // 상황을 피하기 위해, 사람이 읽는 메시지와는 별개로 ECoreErrorCode를 항상 함께 들고 다닌다.
    class CoreException : public std::runtime_error
    {
    public:
        CoreException(const ECoreErrorCode code, std::string message)
            : std::runtime_error(std::move(message))
            , code_(code)
        {
        }

        [[nodiscard]] ECoreErrorCode Code() const noexcept { return code_; }

    private:
        ECoreErrorCode code_;
    };
}
