#pragma once

#include "Shared/Core/Src/Packet/BinaryReader.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"

namespace Packet
{
    // 문자열로 취급할 인자. 길이(uint16) + 데이터로 나가므로 가변 길이를 실을 수 있다.
    template <typename T>
    concept ArgStringLike = std::is_convertible_v<T, std::string_view>;

    // 바이트째로 실을 인자. **StringLike를 빼야 한다** -- `const char*`와 문자열 리터럴은
    // trivially copyable이면서 동시에 string_view로 변환되므로, 빼지 않으면 오버로드가
    // 모호해진다(포인터 값만 실려 나가는 사고로도 이어진다).
    template <typename T>
    concept ArgTrivial = TriviallySerializable<T> && !ArgStringLike<T>;

    template <ArgStringLike T>
    void WriteArg(BinaryWriter& binaryWriter, const T& value)
    {
        binaryWriter.WriteString(value);
    }

    template <ArgTrivial T>
    void WriteArg(BinaryWriter& binaryWriter, const T& value)
    {
        binaryWriter.Write(value);
    }

    inline bool ReadArg(BinaryReader& binaryReader, std::string& outValue)
    {
        return binaryReader.ReadString(outValue);
    }

    template <ArgTrivial T>
    bool ReadArg(BinaryReader& binaryReader, T& outValue)
    {
        return binaryReader.Read(outValue);
    }

    // 인자를 **넘긴 순서 그대로** 직렬화한다. 콤마 fold라 평가 순서가 보장된다.
    template <typename... TArgs>
    void WriteArgs(BinaryWriter& binaryWriter, const TArgs&... args)
    {
        (WriteArg(binaryWriter, args), ...);
    }

    // 넣은 순서 그대로 꺼낸다. 하나라도 실패하면 거기서 멈추고 false --
    // `&&` fold라 왼쪽부터 평가되고 단락 평가된다(실패 뒤의 값은 건드리지 않는다).
    //
    // **쓴 순서와 읽는 순서가 어긋나면 조용히 엉뚱한 값이 나온다.** 길이 프리픽스가 있는
    // 문자열은 대개 실패로 드러나지만, 크기가 같은 산술 타입끼리는 드러나지 않는다.
    template <typename... TArgs>
    [[nodiscard]] bool ReadArgs(BinaryReader& binaryReader, TArgs&... outArgs)
    {
        return (ReadArg(binaryReader, outArgs) && ...);
    }
}
