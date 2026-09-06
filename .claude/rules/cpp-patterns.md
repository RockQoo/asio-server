---
paths:
  - "**/*.{h,cpp}"
---

# C++ Code Patterns (asio-server)

이 문서는 루트 `CLAUDE.md`에 없는 **세부 코딩 규칙**만 다룬다(네임스페이스/멤버 변수 명명/주석
언어 같은 상위 규칙은 `CLAUDE.md`가 우선). `3rd/asio`(벤더 코드)에는 적용 안 함 — 수정 대상이
아니다.

---

## 파일 인코딩: BOM 대신 `/utf-8` 플래그

두 vcxproj의 `ClCompile`에 `/utf-8` `AdditionalOptions`가 걸려 있다. 소스는 UTF-8 **without
BOM**으로 저장한다 — BOM 없이 이 플래그가 빠지면 MSVC가 CP949로 오인식해 한글 주석 + 그
뒤 식별자까지 파싱 에러가 난다. 새 vcxproj 추가/`ItemDefinitionGroup` 수정 시 이 플래그
유지 여부를 반드시 확인할 것.

## 헤더 Include 순서

빈 줄로 구분된 4그룹: **① 대응 헤더**(.cpp면 같은 이름 .h, pch 다음 최상단) → **② 같은
솔루션의 다른 헤더**(`Shared/Core/Src/...`, `Server/ZoneServer/Src/...`) → **③ 서드파티**(`<asio.hpp>`) →
**④ 표준 라이브러리**.

## Precompiled Header (`pch.h`)

실행 파일 프로젝트(`WorldServer`/`ZoneServer`/`GatewayServer`/`LoadTestClient`)와 `Core`는
각자 자체 `Src/pch.h`/`pch.cpp`를 가진다(PCH는 프로젝트 단위라 공유 안 함). 새 `.cpp`는
**첫 줄에 자기 프로젝트 pch include 필수**(빠지면 C1010으로 빌드 즉시 실패).
`pch.h`엔 무거운 서드파티/표준 헤더만 넣는다 — 프로젝트 자체 헤더는 자주 바뀌어 PCH를
무효화하므로 넣지 않는다(`BasicTypes.h`/`Log/LogProxy.h`류의 안정적 크로스커팅 인프라는
예외). **예외**: `TestClient`처럼 `.cpp` 1개뿐인 프로젝트는 PCH 자체를 안 쓴다(`/utf-8`은 유지).

## 타입 캐스팅

**C-style 캐스트 금지**, `static_cast`/`reinterpret_cast`만 사용. `reinterpret_cast`로 포인터
변환 시 원본이 `const`면 결과도 `const` 유지. 다형성 다운캐스팅엔 `dynamic_cast`(아직 미등장).

## 값 매개변수에 `const` 붙이기 (Sink는 예외)

**by-value 매개변수는, 본문에서 다시 `std::move()`해서 멤버/컨테이너로 넘기는 sink가 아니라면
`const`를 붙인다.** Sink 패턴(`ZoneServerApp(ZoneServerConfig config)`,
`WorkerThread(std::string name)`, `PostTask(Task task)`처럼 값으로 받아 본문에서 멤버로
move)엔 **`const`를 붙이면 안 된다** — 붙이면 `std::move(param)`이 `const T&&`가 되어 이동이
복사로 조용히 강등된다. 이런 sink는 별도 `T&&` 오버로드도 필요 없다(값 매개변수 자체가 양쪽
호출을 다 커버). **판단 기준**: 본문에서 다시 move하면 sink(const 금지), 읽기만 하면(호출자가
move로 넘겼어도) const 가능.

## STL 삽입: `emplace` vs 일반 삽입

**원소를 그 자리에서 직접 생성할 때만 `emplace`/`emplace_back`/`try_emplace`를 쓴다. 이미
존재하는 객체(지역 변수든 다른 함수의 반환값이든)를 넣을 땐 `push_back`/`push`/`insert`를
쓴다.** `std::move`는 원본을 더 이상 안 쓸 때만. `std::make_unique<T>(...)`가 만든
`unique_ptr`을 넣는 건 "이미 완성된 객체"이므로 거의 항상 `push_back`이지 `emplace_back`이
아니다(`map`에 키/값을 그 자리에서 조립하는 `sessions_.emplace(id, session)`류만 진짜
`emplace`).

## Get 계열은 `const` 필수 (가변 참조 반환 접근자는 예외)

읽기 전용 getter(`GetXxx`/`IsXxx`/`Xxx()`)는 예외 없이 `const`. **예외**: 호출자가 내부 상태를
의도적으로 바꾸도록 가변 참조/포인터를 반환하는 접근자(`ZoneWorkerManager::GetZoneWorld()`,
`AffinityWorkerPool::GetWorker()`, `IoContextPool::Next()`/`At()`, `Session::Socket()`)는
`const`로 선언할 수 없다(컴파일 에러). 새 getter는 "호출자가 반환값으로 상태를 바꿔야
하는가?"로 판단.

## 수치 한계값

C 매크로(`UINT32_MAX` 등) 대신 `std::numeric_limits<T>::max()`.

## Scoped Enum 비트플래그

플래그 조합이 필요한 enum이 생기면 매크로 대신 `Common`에 헬퍼 추가:
```cpp
template <typename E> requires std::is_enum_v<E>
[[nodiscard]] constexpr bool HasFlag(const E value, const E flag) noexcept
{
    using U = std::underlying_type_t<E>;
    return (static_cast<U>(value) & static_cast<U>(flag)) == static_cast<U>(flag);
}
```

## asio 비동기 핸들러의 예외 안전

`IoContextPool::Run()`은 `context->run()`을 try/catch 없이 직접 호출한다 — 완료 핸들러에서
새어나간 예외는 그 스레드를 잡아줄 곳 없이 죽이고, 처리 안 된 예외 하나가 **프로세스 전체를
`std::terminate()`로 끝낸다.** 따라서 `Session::DoRead`/`DoWrite`, `Listener` accept 핸들러,
`ZonePacketHandler::OnPacket`, `TaskWorker::PostTask` 콜백처럼 I/O·로직 스레드에서 직접 도는
코드는 예외 가능 연산(`.at()` 등)을 스스로 try/catch해야 한다.

## `byte`/`size_t`/고정폭 정수는 `std::` 생략

`Shared/Core/Src/Common/BasicTypes.h`가 `byte`/`size_t`/`int8_t`~`int64_t`/`uint8_t`~`uint64_t`를
전역으로 `using` 해놨고 양쪽 `pch.h`가 include한다(PCH 없는 `TestClient`는 직접 include).
**이 10개 타입 한정** — `std::string`/`std::vector` 등 다른 표준 타입은 그대로 `std::`를 붙인다.

## `enum`은 항상 `enum class` + underlying type 명시

일반 enum 금지. **원소 적은 enum은 `uint8_t`가 기본**(`Log::ELogLevel`), **에러 코드처럼 계속
늘어나는 enum은 `int32_t`**(`Common::EErrorCode`, `uint8_t`는 256개로 부족).

## `int` 대신 `int32_t`/`int64_t`

크기 불명확한 `int`/`long` 대신 고정폭 정수(네트워크/직렬화 값은 특히). **예외**: `int main()`,
`main(const int argc, ...)`, asio 콜백처럼 언어/라이브러리가 정확히 `int`를 강제하는 자리는
그대로 둔다.

## 에러는 문자열이 아니라 `Common::EErrorCode`로 식별

`Common::ErrorCode.h`(`EErrorCode : int32_t`)와 `Common::CoreException`(`: std::runtime_error`,
메시지+`EErrorCode`를 함께 들고 다님, `.Code()`로 조회)을 쓴다. 문자열(`ex.what()`) 파싱으로
에러 종류를 구분하지 않는다 — 검증 실패는 `throw CoreException(EErrorCode::Xxx, "메시지")`.
새 에러 조건은 `EErrorCode`에 **항상 맨 뒤에 추가**(기존 값 정수가 바뀌면 과거 로그와 어긋남).
asio의 `std::error_code`/`std::system_error`(네트워크 계층)는 이미 코드 기반이라 대상 아님.

## 로그는 `std::cout`/`std::cerr` 대신 `LOG`

최상위 `Log` 네임스페이스(`Core::Log` 아님 — 아래 참고)의 전역 `LOG`(`Log::LogProxy`)를 쓴다.
시작 시 한 번 `Log::Logger::Instance().Initialize("logs/xxx.log")` 호출(콘솔 코드페이지를
`CP_UTF8`로 맞춰 한글 로그 깨짐도 방지 — 새 실행 파일 프로젝트는 `main()` 맨 앞에서 호출 필수).

**형태**: `LOG.<Level>(category, "메시지").KV("Key", value).V(value2);`
(`<Level>` = Debug/Info/Warning/Error, `.KV`=이름 있는 값, `.V`=이름 없는 값, 둘 다 생략하고
`LOG.Info(category, "메시지");`만 써도 유효). `LOG.Error(...)`는 `Log::LogEntry<TCategory>`
임시객체를 반환하고 **문장이 끝나 소멸되는 시점에 한 줄을 커밋**하므로(`.KV`/`.V`는 `*this` 참조
반환) 반환값을 discard해도 정상 동작 — 그래서 `Debug`/`Info`/`Warning`/`Error`엔 일부러
`[[nodiscard]]`를 안 붙인다. 콘솔 색: Error=빨강, Warning=노랑(레거시 콘솔엔 주황이 없어 대체),
Info=초록, Debug=기본색. **예외**: `TestClient` REPL 안내문/수신 로그(`PrintHelp`, `[recv]`)는
로그가 아니라 프로그램 UI라 `std::cout` 유지(단 인코딩 위해 `Logger::Initialize()`는 호출).

```cpp
LOG.Info(ELogCategory::Zone, "플레이어 입장").KV("Zone", zoneId_).KV("SessionId", sessionId);
// [2026-01-01 12:00:00.000] [INFO ] [Zone] 플레이어 입장 . Zone : 0, SessionId : 1
```

**`ELogCategory`는 프로젝트마다 따로 있다** — `Log::LogEntry<TCategory>`는 카테고리 값을
직접 갖지 않고 템플릿으로 받는다(scoped enum + 같은 네임스페이스의 ADL `ToString()`만 있으면
됨 = `LogCategoryType` concept). Core가 게임 콘텐츠를 몰라야 하므로: `Shared/Core/Src/Log/
LogCategory.h`의 `Log::ELogCategory{General,Network,Packet,Thread}`(Core 전용, 콘텐츠 없음)와
`ZoneServer`/`WorldServer`/`GatewayServer`/`LoadTestClient`가 각자 자기 폴더에 갖는
`Zone`/`World`/`Gateway`/`Load` 네임스페이스의 `ELogCategory`(콘텐츠) — 이렇게 프로젝트 수만큼
분리돼 있다. 전부 `using`으로 전역 노출돼 있어 어디서든 `ELogCategory::Xxx`로 쓰지만, 서로
다른 프로젝트(PCH)에서만 보여 충돌 안 함. 새 프로젝트는 자기 폴더에 자기 `ELogCategory`+
`ToString()`을 새로 정의하면 된다(Core에 추가 금지) — `LogEntry`/`LogProxy`는 손댈 필요 없음.

**왜 `Core::` 접두사가 없는가** — `Core` 아래 네임스페이스(`Network`/`Packet`/`Thread`/
`Timer`/`Common`/`Log`)는 전부 `Core::`를 안 붙인다(`namespace Core {...}`로 감싼 코드 없음).
계속 감싸면 거의 모든 시그니처가 `Core::`로 시작해 잡음이 컸다 — `Core`는 폴더/프로젝트
이름으로만 남고, 폴더-네임스페이스 대응 원칙(`Shared/Core/Src/Network/` ↔ `namespace Network`)은
그대로 유지하되 `Shared/Core/`라는 상위 폴더 두 겹만 생략한다. `ZoneServer`/`WorldServer`/
`GatewayServer`/`LoadTestClient`는 각자 원래 네임스페이스(`Zone`/`World`/`Gateway`/`Load`)
하나뿐이라 이 얘기 자체가 해당 없음(폴더 한 겹 생략할 상위 폴더가 없음).

## 값을 소유할 필요 없으면 복사하지 않는다

**본문에서 `std::move`로 실제로 소유권을 넘기는 게 아니라면 `std::string`/`std::vector<T>`를
값으로 받지 않는다** — 읽기만 할 거면 `std::string_view`/`const T&`를 쓴다(by-value는 "이
함수가 소유권을 가져간다"는 신호로만). 실제 사례: `Log::LogEntry` 생성자가 원래 `std::string
message`(값)였지만 생성자 안에서 즉시 `std::format`으로 소비되고 저장 안 되길래
`std::string_view`로 바꿔 복사를 없앴다. `ZoneWorld::OnChat`도 같은 이유로 `const
std::string_view message`로 바꾸고 호출부의 불필요한 `std::move`도 제거했다. **판단 기준**:
"본문에서 이 매개변수를 다른 곳(멤버/컨테이너/다른 스레드로 가는 캡처)에 진짜 move하는가?" —
그렇다면 sink라 by-value가 맞고(`CoreException(EErrorCode, std::string message)`처럼),
아니면 `string_view`/`const T&`/`span<const T>`를 쓴다.
