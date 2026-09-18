#pragma once

namespace Console
{
    // 묶을 수 있는 키. 지금은 F1~F12뿐이다 -- 일반 문자 키는 콘솔에 타이핑하다 실수로
    // 누르기 쉬워서, 눌릴 일이 없는 F키만 연다. Count는 배열 크기용이라 항상 마지막이다.
    enum class EKey : uint8_t
    {
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        Count,
    };

    [[nodiscard]] std::string_view ToString(EKey key);

    // 콘솔 F키를 콜백에 묶는 테스트용 진입점. **지금은 묶는 쪽이 없다** -- 옛 F키 하네스를
    // 새 파이프라인 구조가 대체하면서 걷어냈고, 다시 붙일 때 이 자리를 그대로 쓴다.
    // **전용 스레드 하나**가 키를 읽는다.
    //
    // 키마다 토글 상태를 들고 있어서, 콜백은 누를 때마다 뒤집힌 상태를 받는다
    // (첫 번째 누름 = true). 상태가 필요 없는 테스트는 인자를 무시하면 된다.
    //
    // **콜백은 이 클래스의 입력 스레드에서 돈다 -- 서버 레인이 아니다.** 그래서 공유 상태를
    // 직접 만지면 안 되고, Pipeline::PushMsg 로 레인에 넘겨야 한다.
    //
    // 이 클래스가 콘솔 입력을 독점한다. 같은 프로세스에서 std::cin으로 읽는 코드를 함께
    // 두면 두 스레드가 입력을 나눠 가져가 양쪽 다 오작동한다.
    class KeyBinder
    {
    public:
        // 토글 상태를 받는다(true = 켬).
        using Callback = std::function<void(bool)>;

        KeyBinder() = default;
        ~KeyBinder();

        KeyBinder(const KeyBinder&) = delete;
        KeyBinder& operator=(const KeyBinder&) = delete;

        // **Start() 전에만 부른다.** 입력 스레드가 락 없이 읽으므로, 도는 중에 묶으면 경합한다.
        void Bind(EKey key, std::string description, Callback callback);

        // 묶인 키가 하나도 없으면 스레드를 만들지 않는다.
        void Start();
        void Stop();

    private:
        void Run();

        struct Binding
        {
            Callback callback;
            std::string description;
            bool toggled{};
        };

        std::array<Binding, static_cast<size_t>(EKey::Count)> bindings_;
        std::thread thread_;
        std::atomic<bool> running_{false};
    };
}
