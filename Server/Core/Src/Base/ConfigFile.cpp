#include "pch.h"
#include "Server/Core/Src/Base/ConfigFile.h"

#include "Server/Core/Src/Base/CoreException.h"
#include "Server/Core/Src/Log/Proxy.h"

namespace Base
{
    namespace
    {
        [[nodiscard]] std::string_view Trim(std::string_view text) noexcept
        {
            const auto isSpace = [](const char c) { return c == ' ' || c == '\t' || c == '\r'; };
            while (!text.empty() && isSpace(text.front())) { text.remove_prefix(1); }
            while (!text.empty() && isSpace(text.back())) { text.remove_suffix(1); }
            return text;
        }

        // 값 뒤에 붙은 주석을 떼어낸다. **공백 뒤의 # 만** 주석으로 본다 -- 값 자체에 #이
        // 들어갈 여지를 남기려는 것이고, 줄 맨 앞의 #은 호출부에서 이미 걸렀다.
        [[nodiscard]] std::string_view StripInlineComment(const std::string_view value) noexcept
        {
            for (size_t i = 1; i < value.size(); ++i)
            {
                if (value[i] == '#' && (value[i - 1] == ' ' || value[i - 1] == '\t'))
                {
                    return Trim(value.substr(0, i));
                }
            }
            return value;
        }
    }

    ConfigFile ConfigFile::Load(const std::string& path)
    {
        ConfigFile config;
        config.path_ = path;

        std::ifstream file(path);
        if (!file.is_open())
        {
            return config;
        }

        config.loaded_ = true;

        std::string line;
        size_t lineNo = 0;
        while (std::getline(file, line))
        {
            ++lineNo;

            // **첫 줄의 UTF-8 BOM 을 떼고 본다.** 이 저장소의 소스는 BOM 없이 저장하는 것이
            // 규칙이지만 cfg 는 사람이 아무 편집기로나 여는 파일이라 BOM 이 붙어 온다
            // (PowerShell 의 Set-Content -Encoding UTF8 이 대표적이다). 떼지 않으면 첫 줄이
            // 주석으로 인식되지 않아 "1번 줄에 '=' 가 없다"로 서버가 기동을 포기하는데,
            // 정작 그 줄은 눈으로 보면 멀쩡한 주석이라 원인을 찾기가 매우 어렵다.
            if (lineNo == 1 && line.starts_with("\xEF\xBB\xBF"))
            {
                line.erase(0, 3);
            }

            const auto trimmed = Trim(line);
            if (trimmed.empty() || trimmed.front() == '#')
            {
                continue;
            }

            const auto separator = trimmed.find('=');
            if (separator == std::string_view::npos)
            {
                throw CoreException(ECoreErrorCode::InvalidArgument,
                                     path + "(" + std::to_string(lineNo) + "): '=' 가 없는 줄");
            }

            const auto key = Trim(trimmed.substr(0, separator));
            const auto value = StripInlineComment(Trim(trimmed.substr(separator + 1)));
            if (key.empty())
            {
                throw CoreException(ECoreErrorCode::InvalidArgument,
                                     path + "(" + std::to_string(lineNo) + "): 키가 비어 있다");
            }

            config.values_[std::string(key)] = std::string(value);
        }

        return config;
    }

    const std::string* ConfigFile::Find(const std::string_view key) const
    {
        const auto it = values_.find(std::string(key));
        if (it == values_.end())
        {
            return nullptr;
        }

        used_.insert(it->first);
        return &it->second;
    }

    std::string ConfigFile::GetString(const std::string_view key, const std::string_view fallback) const
    {
        const auto* value = Find(key);
        return value != nullptr ? *value : std::string(fallback);
    }

    int64_t ConfigFile::GetInt64(const std::string_view key, const int64_t fallback,
                                  const int64_t minValue, const int64_t maxValue) const
    {
        const auto* text = Find(key);
        if (text == nullptr)
        {
            return fallback;
        }

        int64_t parsed{};
        const auto* begin = text->data();
        const auto* end = begin + text->size();
        const auto result = std::from_chars(begin, end, parsed);

        // 뒤에 뭐가 더 붙어 있으면(`8 threads`) 실패로 본다 -- from_chars 는 앞부분만 읽고
        // 성공을 돌려주므로, ptr 이 끝까지 갔는지 직접 확인해야 오타가 조용히 통과하지 않는다.
        if (result.ec != std::errc{} || result.ptr != end)
        {
            throw CoreException(ECoreErrorCode::InvalidArgument,
                                 path_ + ": '" + std::string(key) + "' 의 값이 정수가 아니다 (" + *text + ")");
        }

        if (parsed < minValue || parsed > maxValue)
        {
            throw CoreException(ECoreErrorCode::InvalidArgument,
                                 path_ + ": '" + std::string(key) + "' 의 값이 범위를 벗어났다 (" + *text + ")");
        }

        return parsed;
    }

    uint16_t ConfigFile::GetPort(const std::string_view key, const uint16_t fallback) const
    {
        // 0은 "아무 포트나"라는 뜻이 되어버려서 설정으로는 받지 않는다.
        return static_cast<uint16_t>(GetInt64(key, fallback, 1, std::numeric_limits<uint16_t>::max()));
    }

    size_t ConfigFile::GetSize(const std::string_view key, const size_t fallback) const
    {
        return static_cast<size_t>(GetInt64(key, static_cast<int64_t>(fallback), 0,
                                             std::numeric_limits<int32_t>::max()));
    }

    std::chrono::milliseconds ConfigFile::GetMilliseconds(const std::string_view key,
                                                           const std::chrono::milliseconds fallback) const
    {
        return std::chrono::milliseconds(GetInt64(key, fallback.count(), 0,
                                                   std::numeric_limits<int32_t>::max()));
    }

    std::chrono::microseconds ConfigFile::GetMicroseconds(const std::string_view key,
                                                           const std::chrono::microseconds fallback) const
    {
        return std::chrono::microseconds(GetInt64(key, fallback.count(), 0,
                                                   std::numeric_limits<int32_t>::max()));
    }

    void ConfigFile::WarnUnusedKeys() const
    {
        for (const auto& [key, value] : values_)
        {
            if (used_.contains(key))
            {
                continue;
            }

            LOG.Warning(ELogCategory::General, "설정 파일에 아무도 읽지 않는 키가 있다(오타?)")
                .KV("Path", path_).KV("Key", key).KV("Value", value);
        }
    }
}
