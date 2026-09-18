#include "pch.h"
#include "Server/Core/Src/Console/KeyBinder.h"

// _kbhit/_getch. SDK 헤더라 pch가 아니라 여기에 둔다(cpp-patterns.md의 pch 규칙).
#include <conio.h>

namespace Console
{
    namespace
    {
        // 확장 키는 두 번에 나눠 온다: 첫 바이트가 0x00(또는 0xE0) 프리픽스이고 그다음이
        // 스캔 코드다. F11/F12만 0xE0 쪽으로 오는데, 어느 프리픽스든 스캔 코드는 같아서
        // 프리픽스를 구분하지 않고 한 표로 본다.
        constexpr int kExtendedPrefix = 0x00;
        constexpr int kExtendedPrefixAlt = 0xE0;

        [[nodiscard]] std::optional<EKey> FromScanCode(const int scanCode)
        {
            switch (scanCode)
            {
            case 0x3B: return EKey::F1;
            case 0x3C: return EKey::F2;
            case 0x3D: return EKey::F3;
            case 0x3E: return EKey::F4;
            case 0x3F: return EKey::F5;
            case 0x40: return EKey::F6;
            case 0x41: return EKey::F7;
            case 0x42: return EKey::F8;
            case 0x43: return EKey::F9;
            case 0x44: return EKey::F10;
            case 0x85: return EKey::F11;
            case 0x86: return EKey::F12;
            default:   return std::nullopt;
            }
        }

        // 키가 안 눌린 동안 도는 주기. _getch()로 막아두면 Stop()이 스레드를 못 깨워서
        // detach 말고는 방법이 없어진다 -- 폴링이라 join으로 깔끔하게 끝낼 수 있다.
        constexpr auto kPollInterval = std::chrono::milliseconds(50);
    }

    std::string_view ToString(const EKey key)
    {
        switch (key)
        {
        case EKey::F1:  return "F1";
        case EKey::F2:  return "F2";
        case EKey::F3:  return "F3";
        case EKey::F4:  return "F4";
        case EKey::F5:  return "F5";
        case EKey::F6:  return "F6";
        case EKey::F7:  return "F7";
        case EKey::F8:  return "F8";
        case EKey::F9:  return "F9";
        case EKey::F10: return "F10";
        case EKey::F11: return "F11";
        case EKey::F12: return "F12";
        case EKey::Count: break;
        }
        return "Unknown";
    }

    KeyBinder::~KeyBinder()
    {
        Stop();
    }

    void KeyBinder::Bind(const EKey key, std::string description, Callback callback)
    {
        if (key == EKey::Count)
        {
            return;
        }

        auto& binding = bindings_[static_cast<size_t>(key)];
        binding.description = std::move(description);
        binding.callback = std::move(callback);
        binding.toggled = false;
    }

    void KeyBinder::Start()
    {
        if (running_.load())
        {
            return;
        }

        const bool hasAny = std::ranges::any_of(bindings_,
            [](const Binding& binding) { return static_cast<bool>(binding.callback); });
        if (!hasAny)
        {
            return;
        }

        running_.store(true);
        thread_ = std::thread(&KeyBinder::Run, this);

        for (size_t index = 0; index < bindings_.size(); ++index)
        {
            if (bindings_[index].callback)
            {
                LOG.Info(ELogCategory::General, "테스트 키 바인딩")
                    .KV("Key", ToString(static_cast<EKey>(index)))
                    .KV("Desc", bindings_[index].description);
            }
        }
    }

    void KeyBinder::Stop()
    {
        if (!running_.exchange(false))
        {
            return;
        }

        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    void KeyBinder::Run()
    {
        while (running_.load())
        {
            if (_kbhit() == 0)
            {
                std::this_thread::sleep_for(kPollInterval);
                continue;
            }

            const int prefix = _getch();
            if (prefix != kExtendedPrefix && prefix != kExtendedPrefixAlt)
            {
                // F키가 아닌 입력. 이 클래스는 F키만 다루므로 그냥 버린다.
                continue;
            }

            const auto key = FromScanCode(_getch());
            if (!key)
            {
                continue;
            }

            auto& binding = bindings_[static_cast<size_t>(*key)];
            if (!binding.callback)
            {
                continue;
            }

            binding.toggled = !binding.toggled;

            // **콜백에서 새어나온 예외는 여기서 잡는다.** 이 스레드에서 빠져나가면 잡아줄
            // 곳이 없어 std::terminate로 프로세스 전체가 죽는다 -- 테스트 코드를 짜다 던지는
            // 일이 흔한 자리라 서버가 같이 죽으면 안 된다.
            try
            {
                binding.callback(binding.toggled);
            }
            catch (const std::exception& ex)
            {
                LOG.Error(ELogCategory::General, "테스트 키 콜백에서 예외")
                    .KV("Key", ToString(*key)).KV("Message", ex.what());
            }
        }
    }
}
