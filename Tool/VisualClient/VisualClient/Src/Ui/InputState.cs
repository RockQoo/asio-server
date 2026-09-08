using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Input;

namespace VisualClient.Ui;

/// <summary>
/// 이번 프레임의 입력. MonoGame의 <c>Keyboard.GetState()</c>는 "지금 눌려 있는가"만 알려주므로,
/// "이번 프레임에 새로 눌렸는가"(edge)를 쓰려면 직전 프레임 상태를 들고 비교해야 한다 —
/// 그 비교를 UI 코드마다 반복하지 않도록 한 곳에 모았다.
///
/// <para>
/// 타이핑은 <c>Keys</c>로 처리하지 않고 <c>GameWindow.TextInput</c> 이벤트로 들어온 문자를
/// 모아 쓴다. 키 코드로 문자를 만들면 자판 배열과 Shift 조합을 직접 재현해야 하고, 그래도
/// 결과가 OS 설정과 어긋난다.
/// </para>
/// </summary>
public sealed class InputState
{
    private KeyboardState keyboard_;
    private KeyboardState previousKeyboard_;
    private MouseState mouse_;
    private MouseState previousMouse_;

    /// <summary>이번 프레임에 <c>TextInput</c>으로 들어온 문자들(제어 문자 포함).</summary>
    private readonly List<char> typed_ = [];

    public Point MousePosition => new(mouse_.X, mouse_.Y);

    public bool MouseLeftDown => mouse_.LeftButton == ButtonState.Pressed;

    /// <summary>이번 프레임에 왼쪽 버튼이 새로 눌렸는가.</summary>
    public bool MouseLeftPressed =>
        mouse_.LeftButton == ButtonState.Pressed && previousMouse_.LeftButton == ButtonState.Released;

    /// <summary>마우스 휠 변화량(위로 굴리면 양수). 목록 스크롤에 쓴다.</summary>
    public int WheelDelta => mouse_.ScrollWheelValue - previousMouse_.ScrollWheelValue;

    public IReadOnlyList<char> TypedCharacters => typed_;

    public bool IsKeyDown(Keys key) => keyboard_.IsKeyDown(key);

    /// <summary>이번 프레임에 새로 눌렸는가.</summary>
    public bool IsKeyPressed(Keys key) => keyboard_.IsKeyDown(key) && !previousKeyboard_.IsKeyDown(key);

    /// <summary><c>GameWindow.TextInput</c> 핸들러에서 부른다.</summary>
    public void OnTextInput(char character) => typed_.Add(character);

    /// <summary>
    /// 프레임 시작에서 부른다. 직전 상태를 밀어 넣고 새로 읽는다. 타이핑 버퍼는 이 시점에
    /// 비우지 않는다 — <c>TextInput</c> 이벤트는 <c>Update</c> 바깥에서 들어오므로, 여기서
    /// 비우면 방금 들어온 글자를 아무도 못 본다. 비우는 건 <see cref="EndFrame"/>가 한다.
    /// </summary>
    public void BeginFrame()
    {
        previousKeyboard_ = keyboard_;
        previousMouse_ = mouse_;
        keyboard_ = Keyboard.GetState();
        mouse_ = Mouse.GetState();
    }

    /// <summary>프레임 끝에서 부른다. 이번 프레임에 소비한 타이핑을 버린다.</summary>
    public void EndFrame() => typed_.Clear();
}
