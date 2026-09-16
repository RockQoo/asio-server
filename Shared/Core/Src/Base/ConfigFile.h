#pragma once

namespace Base
{
    // `key = value` 한 줄짜리 설정 파일.
    //
    //   # 로 시작하거나 공백 뒤에 오는 # 부터는 주석
    //   중첩은 점으로 평탄화한다  ->  pools.player_threads = 8
    //
    // **왜 파서를 직접 만들었나**: 이 저장소의 외부 의존성은 벤더링한 standalone ASIO 하나뿐이고,
    // 설정 파일 하나 읽자고 그 조건을 깨지 않으려는 것이다. TOML/JSON/YAML 은 표준 C++에 파서가
    // 없어 셋 다 라이브러리를 가져와야 한다. 근거: docs/design/config-file.md
    //
    // **없는 키와 틀린 값의 처리가 다르다.**
    //   없는 키  -> 호출부가 준 기본값을 쓴다(설정 파일이 아예 없어도 뜬다)
    //   틀린 값  -> 던진다. 기본값으로 덮으면 "왜 9200이 아니라 9100에 붙지"를 런타임에 추적하게 된다
    class ConfigFile
    {
    public:
        // 파일이 없으면 빈 상태로 돌아온다 -- 그 판단(경고만 할지 끝낼지)은 호출부 몫이다.
        [[nodiscard]] static ConfigFile Load(const std::string& path);

        [[nodiscard]] bool IsLoaded() const noexcept { return loaded_; }
        [[nodiscard]] const std::string& GetPath() const noexcept { return path_; }

        [[nodiscard]] std::string GetString(const std::string_view key, const std::string_view fallback) const;
        [[nodiscard]] uint16_t GetPort(const std::string_view key, const uint16_t fallback) const;
        [[nodiscard]] size_t GetSize(const std::string_view key, const size_t fallback) const;
        [[nodiscard]] std::chrono::milliseconds GetMilliseconds(const std::string_view key,
                                                                 const std::chrono::milliseconds fallback) const;
        [[nodiscard]] std::chrono::microseconds GetMicroseconds(const std::string_view key,
                                                                 const std::chrono::microseconds fallback) const;

        // 파일에는 있는데 아무도 읽지 않은 키를 경고로 남긴다. **오타를 잡는 유일한 수단**이다 --
        // `pool.player_threads`라고 잘못 적으면 그냥 기본값으로 뜨고 아무 증상이 없다.
        void WarnUnusedKeys() const;

    private:
        [[nodiscard]] const std::string* Find(const std::string_view key) const;
        [[nodiscard]] int64_t GetInt64(const std::string_view key, const int64_t fallback,
                                        const int64_t minValue, const int64_t maxValue) const;

        std::unordered_map<std::string, std::string> values_;

        // 읽힌 키 기록용이라 const 메서드에서 채운다.
        mutable std::unordered_set<std::string> used_;

        std::string path_;
        bool loaded_{false};
    };
}
