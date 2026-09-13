#include "pch.h"

#include "Db/PasswordHash.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <bcrypt.h>

namespace World
{
    namespace
    {
        constexpr size_t kHashLength = 32;  // SHA-256

        // base64 디코더를 직접 둔 이유: CryptStringToBinary를 쓰면 crypt32.lib 의존이 하나 늘고,
        // 하는 일은 30줄이라 링크 대상을 늘릴 만한 값이 아니다.
        [[nodiscard]] int32_t DecodeChar(const char character) noexcept
        {
            if (character >= 'A' && character <= 'Z') { return character - 'A'; }
            if (character >= 'a' && character <= 'z') { return character - 'a' + 26; }
            if (character >= '0' && character <= '9') { return character - '0' + 52; }
            if (character == '+') { return 62; }
            if (character == '/') { return 63; }
            return -1;
        }

        [[nodiscard]] std::string EncodeBase64(const std::span<const uint8_t> data)
        {
            static constexpr std::string_view kAlphabet =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

            std::string out;
            out.reserve((data.size() + 2) / 3 * 4);

            size_t index = 0;
            for (; index + 2 < data.size(); index += 3)
            {
                const uint32_t triple = (static_cast<uint32_t>(data[index]) << 16)
                                        | (static_cast<uint32_t>(data[index + 1]) << 8)
                                        | static_cast<uint32_t>(data[index + 2]);
                out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
                out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
                out.push_back(kAlphabet[(triple >> 6) & 0x3F]);
                out.push_back(kAlphabet[triple & 0x3F]);
            }

            // 남은 1~2바이트는 0으로 채워 4글자를 만들고 모자란 만큼 '='로 덮는다 --
            // .NET Convert.ToBase64String과 같은 규약이라 시드 해시와 형식이 섞여도 된다.
            if (const auto remaining = data.size() - index; remaining > 0)
            {
                uint32_t triple = static_cast<uint32_t>(data[index]) << 16;
                if (remaining == 2)
                {
                    triple |= static_cast<uint32_t>(data[index + 1]) << 8;
                }

                out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
                out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
                out.push_back(remaining == 2 ? kAlphabet[(triple >> 6) & 0x3F] : '=');
                out.push_back('=');
            }

            return out;
        }

        [[nodiscard]] bool DecodeBase64(const std::string_view text, std::vector<uint8_t>& out)
        {
            out.clear();
            out.reserve(text.size() * 3 / 4);

            uint32_t accumulator = 0;
            int32_t bitsCollected = 0;

            for (const char character : text)
            {
                if (character == '=')
                {
                    break;
                }

                const int32_t value = DecodeChar(character);
                if (value < 0)
                {
                    return false;
                }

                accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
                bitsCollected += 6;

                if (bitsCollected >= 8)
                {
                    bitsCollected -= 8;
                    out.push_back(static_cast<uint8_t>((accumulator >> bitsCollected) & 0xFF));
                }
            }

            return true;
        }

        // 길이가 같은 두 바이트열을 **시간 차이 없이** 비교한다. 앞에서부터 다르면 바로 반환하는
        // memcmp를 쓰면, 응답 시간으로 해시 앞부분을 한 바이트씩 맞춰볼 수 있다.
        [[nodiscard]] bool EqualsConstantTime(const std::vector<uint8_t>& left,
                                              const std::array<uint8_t, kHashLength>& right) noexcept
        {
            if (left.size() != right.size())
            {
                return false;
            }

            uint8_t difference = 0;
            for (size_t index = 0; index < left.size(); ++index)
            {
                difference |= static_cast<uint8_t>(left[index] ^ right[index]);
            }
            return difference == 0;
        }

        [[nodiscard]] bool DeriveKey(const std::string& password, const std::vector<uint8_t>& salt,
                                     const uint64_t iterations,
                                     std::array<uint8_t, kHashLength>& out)
        {
            BCRYPT_ALG_HANDLE algorithm = nullptr;

            // HMAC 플래그가 없으면 순수 SHA-256 핸들이 나와 PBKDF2가 실패한다.
            if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                                            nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)))
            {
                return false;
            }

            const auto status = BCryptDeriveKeyPBKDF2(
                algorithm,
                reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
                static_cast<ULONG>(password.size()),
                salt.empty() ? nullptr : const_cast<PUCHAR>(salt.data()),
                static_cast<ULONG>(salt.size()),
                iterations,
                out.data(), static_cast<ULONG>(out.size()), 0);

            BCryptCloseAlgorithmProvider(algorithm, 0);
            return BCRYPT_SUCCESS(status);
        }

        // 자동 가입이 쓰는 값들. **반복 횟수는 시드(Sql/seed.sql)와 같은 1000이고, 실서비스
        // 값이 아니다** -- 로그인이 한꺼번에 몰리는 부하 테스트에서 PBKDF2 CPU가 DB 커넥션
        // 대기 시간을 가려버리면 정작 재려던 값을 못 잰다. 값을 올리려면 저장 형식이 횟수를
        // 품고 있으므로 기존 계정은 그대로 두고 새 계정부터 올라간다.
        constexpr uint64_t kSignUpIterations = 1000;
        constexpr size_t kSaltLength = 16;
    }

    bool VerifyPassword(const std::string& password, const std::string& storedHash)
    {
        // "반복횟수.솔트.해시" -- 구분자가 정확히 둘이 아니면 형식이 아니다.
        const auto firstDot = storedHash.find('.');
        if (firstDot == std::string::npos)
        {
            return false;
        }

        const auto secondDot = storedHash.find('.', firstDot + 1);
        if (secondDot == std::string::npos)
        {
            return false;
        }

        const std::string_view iterationsText(storedHash.data(), firstDot);
        uint64_t iterations = 0;
        const auto parsed = std::from_chars(iterationsText.data(),
                                            iterationsText.data() + iterationsText.size(), iterations);
        if (parsed.ec != std::errc{} || iterations == 0)
        {
            return false;
        }

        std::vector<uint8_t> salt;
        std::vector<uint8_t> expected;
        if (!DecodeBase64(std::string_view(storedHash).substr(firstDot + 1, secondDot - firstDot - 1), salt)
            || !DecodeBase64(std::string_view(storedHash).substr(secondDot + 1), expected))
        {
            return false;
        }

        std::array<uint8_t, kHashLength> derived{};
        if (!DeriveKey(password, salt, iterations, derived))
        {
            return false;
        }

        return EqualsConstantTime(expected, derived);
    }

    std::string HashPassword(const std::string& password)
    {
        // 솔트는 암호학적 난수여야 한다. std::mt19937은 시드만 알면 재현되므로 여기 쓸 수 없다.
        std::vector<uint8_t> salt(kSaltLength);
        if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, salt.data(), static_cast<ULONG>(salt.size()),
                                            BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        {
            return {};
        }

        std::array<uint8_t, kHashLength> derived{};
        if (!DeriveKey(password, salt, kSignUpIterations, derived))
        {
            return {};
        }

        return std::to_string(kSignUpIterations) + "." + EncodeBase64(salt) + "." + EncodeBase64(derived);
    }
}
