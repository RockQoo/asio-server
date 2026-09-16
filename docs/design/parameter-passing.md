# 매개변수 전달 — 언제 값이고 언제 `const&`인가

대상: `Shared/Protocol/Src/StrongId.h`,
`.claude/rules/cpp-patterns.md`의 "값 매개변수에 `const` 붙이기".

## 왜 이 문서가 있나

```cpp
void ApplyMailTask(PlayerManager::Mutexed& playerManager, AutoSpCommands& autoSpCommands,
                   const Protocol::EMailTask subTask, const Network::SessionId clientSessionId,
                   const Protocol::PlayerId playerId, const std::span<const byte> taskPayload)
```

이 시그니처를 보고 **"`PlayerId`는 클래스인데 왜 `const&`가 아닌가, 넘길 때마다 복사가
생기지 않나"** 라는 질문이 나왔다. "클래스는 `const&`로 받아야 복사가 싸다"는 것은 널리
퍼진 직관이지만 **조건부**이고, `StrongId`는 그 조건에 걸리지 않는다. 같은 의문이 반복될
자리라 근거를 측정치까지 남긴다.

## 결론 — 판단 기준

| 조건 | 받는 법 |
|---|---|
| **trivially copyable + 포인터 크기(8B) 이하** | `const T` 값 |
| 위 + 16바이트(2워드) 이하 | `const T` 값 (`std::span`이 여기) |
| 힙을 들고 있다 / 사용자 정의 복사 생성자가 있다 / 그보다 크다 | `const T&` |
| 본문에서 멤버·컨테이너로 `std::move`한다 | `T` 값 (sink, **`const` 금지**) |
| 읽기만 하는 문자열 | `std::string_view` |

`Protocol::PlayerId`/`MailId`/`ZoneId`는 전부 첫 줄에 해당한다.

## 근거 1 — 비싼 것은 "클래스"가 아니라 "복사 생성자가 하는 일"이다

`std::string` 복사가 비싼 이유는 클래스여서가 아니라 복사 생성자가 **힙 할당 + memcpy**를
하고 소멸자가 **free**를 하기 때문이다. `StrongId`의 복사 생성자는 암묵 `= default`이고
멤버가 `TValue` 하나뿐이라 **trivial** — 비트 복사로 취급해도 되고, 그래서 명령어를 하나도
내지 않는다.

또 하나 흔한 오해: **이미 있는 `PlayerId`를 넘길 때 `explicit StrongId(TValue)`는 불리지
않는다.** 그 변환 생성자는 `PlayerId{123}`처럼 raw 정수에서 만들 때만 돈다. 매개변수 전달에
관여하는 것은 복사 생성자다.


## 근거 1-b — 복사 생성자는 "필요 없는" 것이 아니라 "있는데 공짜"인 것

`StrongId`에는 복사 생성자를 한 줄도 쓰지 않았지만 **없는 것이 아니다.** 컴파일러가 암묵적으로
선언·정의하고, `is_copy_constructible_v`가 `true`다. 그리고 **값 전달은 그것을 요구한다** --
`= delete`로 없애면 값으로 받는 함수 자체가 컴파일되지 않는다.

```
error C2280: 'NoCopyId::NoCopyId(const NoCopyId &)': attempting to reference a deleted function
```

즉 "복사 생성자가 없어서 싸다"가 아니라 **"있는데 trivial이라 명령어가 0개"**다. 없앨 수
있었다면 오히려 값 전달을 못 했을 것이다.

### 선언 방식에 따라 trivial 자격이 갈린다 (실측)

| 선언 방식 | `trivially_copyable` | 전달 코드 |
|---|---|---|
| 아무것도 안 씀 (지금의 `StrongId`) | **1** | `jmp Sink` |
| 클래스 **안** `T(const T&) = default;` | **1** | `jmp Sink` |
| 클래스 **밖** `T::T(const T&) = default;` | **0** | `mov rcx,[rcx]` + `jmp` |
| 직접 구현 | **0** | `mov rcx,[rcx]` + `jmp` |

**`= default`인데도 정의를 클래스 밖에 두면 깨진다** -- user-provided가 되어 trivial 자격을
잃는다. 겉보기에 무해한 코드라 특히 위험하다.
## 근거 2 — x64 ABI는 trivially copyable 8바이트를 레지스터로 넘긴다

MSVC x64는 클래스 인자를 이렇게 나눈다.

- 크기가 1/2/4/8바이트 **이고** 복사가 trivial → **값을 레지스터에**
- 그 밖 → **호출자가 메모리에 복사본을 만들고 그 주소를 넘김** (사실상 숨은 `const&`)

`/O2`로 실측한 결과:

```asm
; void ByValue(const Protocol::PlayerId)      <- StrongId 를 값으로
        jmp     Sink                           ; 이게 전부

; void RawByValue(const int64_t)              <- 맨 int64_t 를 값으로
        jmp     Sink                           ; 완전히 동일

; void ByConstRef(const Protocol::PlayerId&)  <- 참조로
        mov     rcx, QWORD PTR [rcx]           ; 역참조가 하나 늘었다
        jmp     Sink
```

**`StrongId` 값 전달과 `int64_t` 값 전달이 명령어 단위로 같다.** 래핑 비용이 0이고,
`const&`가 오히려 한 줄 더 쓴다.

**이 성질을 지탱하는 것이 "복사 생성자를 선언하지 않은 것"이다** -- 위 1-b의 표대로, 여기에
복사 생성자를 추가하면(클래스 밖 `= default` 포함) 크기가 같은데도 메모리 경유로 바뀐다.
그래서 `StrongId.h`에 그 경고를 주석으로 남겨뒀다.

## 근거 3 — `const&`는 역참조에 더해 aliasing 재로드를 부른다

참조는 결국 포인터다. 전달 비용(8바이트)은 값과 같은데, **쓸 때마다 역참조가 붙고**
컴파일러가 "중간에 누가 저 메모리를 바꿨을 수 있나"를 배제하지 못하면 **다시 읽어야 한다**.

```cpp
extern void Opaque();   // 다른 TU 라 컴파일러가 내용을 모른다

int64_t Twice(/* 값 또는 참조 */ playerId)
{
    const int64_t first = playerId.Value();
    Opaque();
    return first + playerId.Value();
}
```

```asm
; TwiceByValue
        mov     rbx, rcx
        call    Opaque
        lea     rax, QWORD PTR [rbx+rbx]   ; 메모리 접근 0회. 2*v 로 접어버렸다

; TwiceByConstRef
        mov     rbx, QWORD PTR [rcx]       ; 첫 읽기
        mov     rdi, rcx                   ; 주소를 따로 살려둬야 해서 레지스터를 더 쓴다
        call    Opaque
        mov     rax, QWORD PTR [rdi]       ; 다시 읽는다 (Opaque 가 바꿨을 수 있으므로)
        add     rax, rbx
```

값 쪽은 "변할 수 없음"이 보장돼 상수 접기까지 갔고, 참조 쪽은 재로드에 레지스터 하나를 더
썼다. **참조가 유리해지는 경우는 없다.**

> 주의: 같은 TU 안에서 인라인되면 `const&`의 역참조가 사라져 차이가 안 보인다. 실제 문제가
> 되는 `ApplyMailTask` 같은 경계는 다른 `.cpp`라 인라인되지 않고 차이가 그대로 남는다.

## 이 저장소에서 값으로 받는 것들

| 타입 | 크기 | |
|---|---|---|
| `Protocol::PlayerId` / `MailId` | 8B | `StrongId<_, int64_t>` |
| `Protocol::ZoneId` | 4B | `StrongId<_, uint32_t>` |
| `Network::SessionId` | 8B | `uint64_t` 별칭 |
| `Protocol::EMailTask` / `ECurrencyType` 등 enum | 1~4B | |
| `std::span<const byte>` | 16B | 그 자체가 view — `const&`는 안티패턴 |

## 측정 환경

MSVC `19.51.36257` (x64), `/O2 /std:c++latest`. 재측정은 `/FAs`로 어셈블리를 뽑아
비교하면 된다.
