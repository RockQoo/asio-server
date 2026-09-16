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

## Include 경로: 자기 프로젝트는 짧게, 남의 것은 전체 경로

각 프로젝트의 `Src`가 include 루트로 걸려 있다(`$(ProjectDir)Src`). 그래서 **같은 프로젝트
안의 헤더는 `Src` 기준 짧은 경로**로 쓰고, **다른 프로젝트 것만 솔루션 루트 기준 전체 경로**를 쓴다.

```cpp
#include "pch.h"                                 // 프로젝트마다 자기 것이 있다
#include "App/App.h"                             // 같은 프로젝트
#include "Cli/DbCheck.h"
#include "Shared/Core/Src/Base/RUID.h"         // 다른 프로젝트
```

접두사가 있으면 남의 것, 없으면 내 것 -- **줄만 보고 갈린다.** 전부 전체 경로였을 때는
모든 줄이 길어서 그 대비가 없었다.

### 예외 -- 다른 프로젝트가 가져다 쓰는 헤더는 전체 경로

`Shared/Core`는 라이브러리라 **그 헤더가 남의 프로젝트 안에서 컴파일된다.** 짧은 경로는
그쪽 include 경로로 풀리지 않으므로(`Log/Entry.h` not found), **Core 안에서는 자기 헤더도
전체 경로로 쓴다**(`pch.h`만 예외 -- `/Yu`가 이름으로 매칭하고 프로젝트마다 자기 것이 있다).

같은 이유로 `Shared/Common` 의 헤더도 자기끼리 전체 경로를 쓴다. 어기면 빌드가 `C1083`으로
즉시 깨지므로 조용히 넘어가지는 않는다.

**서버 프로젝트의 헤더는 애초에 공개되지 않는다.** 예전에는 `WorldServer/Src/Packet/*.h` 가
Gateway·Zone 에 공개돼 있었는데, 그건 와이어 계약이 한 서버 프로젝트 안에 있었기 때문이고
지금은 `Shared/Common/Src/Packet/` 으로 올라갔다. 둘 이상의 서버가 알아야 하는 것이 생기면
남의 서버를 include 하지 말고 `Shared/Common` 으로 올린다.

## 헤더 Include 순서

빈 줄로 구분된 4그룹: **① 대응 헤더**(.cpp면 같은 이름 .h, pch 다음 최상단) → **② 다른 헤더**
(같은 프로젝트 → 다른 프로젝트 순) → **③ 서드파티**(`<asio.hpp>`) → **④ 표준 라이브러리**.

## Precompiled Header (`pch.h`)

실행 파일 프로젝트(`WorldServer`/`ZoneServer`/`GatewayServer`/`StressClient`)와 `Core`는
각자 자체 `Src/pch.h`/`pch.cpp`를 가진다(PCH는 프로젝트 단위라 공유 안 함). 새 `.cpp`는
**첫 줄에 자기 프로젝트 pch include 필수**(빠지면 C1010으로 빌드 즉시 실패).
**이 프로젝트가 쓰는 표준 헤더는 전부 pch.h 에 있다.** 각 `.cpp`/`.h` 는 표준 헤더를 직접
include 하지 않고 자기 프로젝트 pch 만 include 한다(새 `.cpp` 첫 줄에 `#include "pch.h"` 필수 —
빠지면 `C1010` 으로 즉시 실패).

```cpp
#include "pch.h"
#include "App/App.h"          // 프로젝트 헤더는 그대로 직접 include
```

### 지켜야 할 것 셋

**① 6개 프로젝트의 pch 는 표준 헤더 목록이 같다.** `Shared/Core` 의 공개 헤더가 남의 프로젝트
안에서 컴파일되므로, 소비자 pch 에 그 헤더가 없으면 **거기서** 깨진다. 새 표준 헤더가 필요하면
**6개 전부에 추가한다**(`ProtocolClient` 포함 — `.cpp` 하나뿐이지만 같은 이유로 pch 를 둔다).

**② pch 안에서 표준 헤더가 프로젝트 헤더보다 먼저 와야 한다.** `Log/Proxy.h` 가 `<fstream>` 을
쓰는데 순서가 뒤집히면 pch 를 만드는 동안 아직 못 본 상태로 파싱된다(실제로 겪었다).

**③ SDK 헤더는 pch 에 넣지 않는다.** `<sql.h>`/`<sqlext.h>`/`<bcrypt.h>` 는 매크로를 수백 개
뿌리는데 쓰는 파일이 `Db/` 몇 개뿐이라 그 파일에 남긴다 — `ALL_CAPS` 상수를 금지하는 이유와
같은 문제다. **`<windows.h>` 는 반대로 pch 에 있다**: `asio.hpp` 가 이미 전 TU 에 끌고 오므로
넣어도 달라지는 것이 없고, 넣어야 그걸 쓰는 파일이 줄을 지울 수 있다.

**④ `Shared/Common` 은 예외다.** StaticLibrary 이지만 내용이 거의 헤더뿐이라 자기 pch 를
두지 않고, 6개 프로젝트 전부가 가져다 쓴다. 그래서 이 폴더의 헤더는 `<cstdint>` 같은 것을
직접 include 해 **자기 완결적으로** 유지한다. `<asio.hpp>` 도 같은 이유로 그걸 쓰는 헤더에
남겨둔다(표준 헤더가 아니라 서드파티라, 무엇이 asio 에 묶여 있는지 보이는 편이 낫다).

### 왜 이 방식인가

각 파일이 자기가 쓰는 표준 헤더를 적는 방식(IWYU)도 가능하고 장점이 있다 — 파일만 보고
의존성을 알 수 있고, 다른 프로젝트로 옮겨도 컴파일된다. 다만 이 저장소는 **실무 게임 서버
구조 재현**이 목적이고, 그쪽에서는 pch 가 표준 헤더를 전담하는 편이 일반적이라 그쪽을 택했다.
대가는 분명하다: pch 에서 헤더를 빼면 여러 파일이 한꺼번에 깨지고, 파일을 다른 프로젝트로
옮길 때 그 pch 목록에 의존한다. ①이 그 대가를 줄이는 장치다.

## 타입 캐스팅

**C-style 캐스트 금지**, `static_cast`/`reinterpret_cast`만 사용. `reinterpret_cast`로 포인터
변환 시 원본이 `const`면 결과도 `const` 유지. 다형성 다운캐스팅엔 `dynamic_cast`(아직 미등장).

## 값 매개변수에 `const` 붙이기 (Sink는 예외)

**by-value 매개변수는, 본문에서 다시 `std::move()`해서 멤버/컨테이너로 넘기는 sink가 아니라면
`const`를 붙인다.** Sink 패턴(`App(Config config)`,
`Log::Logger(std::string path)`, `Processor::Group(std::string name, ...)`처럼 값으로 받아 본문에서 멤버로
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
의도적으로 바꾸도록 가변 참조/포인터를 반환하는 접근자(`WorkerManager::GetZoneInstance()`,
`IoContextPool::Next()`/`At()`, `Session::Socket()`)는
`const`로 선언할 수 없다(컴파일 에러). 새 getter는 "호출자가 반환값으로 상태를 바꿔야
하는가?"로 판단.

## 컴파일 타임 상수는 `k` 접두사 (`kEpochMs`)

`constexpr`/`const` **값**은 `k` + PascalCase(`kTimestampBits`, `kZoneSize`, `kNodeIdSeed`).
`k`는 konstant의 약자로, C 시절부터 `c`가 count/char에 배정돼 있어 상수 자리를 `k`가 가져간
관례다. 이름만 보고 "런타임에 안 바뀌는 값"임을 알 수 있어 지역 변수(`camelCase`)·멤버
변수(`socket_`)·POD public 필드(밑줄 없음)와 한눈에 갈린다.

**`constexpr` 함수는 붙이지 않는다** — 값이 아니라 함수라 PascalCase 그대로다
(`Header::MaxBodySize()`, `Base::HasFlag()`, `Common::MakeTaskKind()`).

**`ALL_CAPS`(`TIME_STAMP_BITS`)는 쓰지 않는다.** C++에서 그 자리는 매크로 관례라, 매크로는
네임스페이스·스코프를 무시하고 전처리에서 무조건 치환되므로 헤더가 같은 이름을 먼저
`#define` 해두면 선언이 통째로 깨진다. 이 프로젝트는 `pch.h`의 `asio.hpp`가 모든 TU에
`windows.h`를 끌고 오고(`ERROR`/`DELETE`/`MAX_PATH`/`IN`/`OUT` …), `Server/WorldServer/Src/Db/`는
ODBC `sql.h`의 `SQL_*`/`MAX_*` 수백 개를 더 본다. 충돌하면 치환된 뒤의 코드로 에러가 나서
메시지가 원인을 안 가리킨다.

## 지역 변수 이름은 타입 이름을 따른다 (역할 이름을 붙이지 않는다)

**클래스 타입의 지역 변수는 그 클래스 이름을 camelCase로 쓴다.** 그 객체가 "무엇에 쓰이는가"를
이름에 넣지 않는다 — 쓰임은 바로 다음 줄들이 말해준다.

```cpp
Player player;                         // OK
UnitOfWork unitOfWork(...);            // OK

AutoSpCommands select(...);            // X -- select 는 SQL 동작이지 이 객체가 아니다
AutoSpCommands autoSpCommands(...);    // O
```

**왜**: 역할로 이름을 지으면 같은 타입이 함수마다 다른 이름으로 불린다(`select`/`upsert`/`load`가
전부 `AutoSpCommands`였다). 그러면 "이 파일에서 AutoSpCommands 를 어디서 쓰나"를 이름으로 찾을 수
없고, 소멸자에서 일이 끝나는 타입(`AutoSpCommands`/`UnitOfWork`)은 **그 변수가 무엇인지 모르면
스코프의 끝이 무슨 의미인지도 모른다.**

### 같은 타입이 한 스코프에 여럿이면 구분어를 앞에 붙인다

```cpp
Packet::BinaryReader reader(payload);          // 그 함수의 기본 대상
Packet::BinaryReader mailReader(*taskPayload); // 두 번째부터는 무엇을 읽는지 붙인다
```

접미사가 아니라 **접두사**다 — 정렬했을 때 같은 타입끼리 흩어지지 않는다.

### 예외 — 타입 이름이 문맥에서 무의미할 때

`std::vector<byte> payloadCopy` 처럼 컨테이너/표준 타입은 **담긴 내용**으로 이름 짓는다. 이
규칙은 이 프로젝트가 정의한 클래스에만 적용한다.

### `Base::RUID`와 이름

`RUID`는 `int64_t` 별칭이라 타입이 역할을 구분해주지 못한다. 그래서 `RUID`를 그대로 쓰는
자리에서는 **역할 이름을 쓴다** -- 전부 `ruid`로 부르면 무엇의 id인지 알 수 없다.

```cpp
Base::RUID requestId{};   // O -- 타입이 말 못 하는 것을 이름이 말한다
Base::RUID ruid{};        // X
```

**그 자리는 이제 `requestId` 하나뿐이다.** 나머지는 `Common::StrongId<Tag, TValue, kInvalid>`
(`Shared/Common/Src/StrongId.h`)로 **자기만의 타입**이 됐고, 별칭은
`Shared/Common/Src/Ids.h`에 있다. 그 종류들은 `MailId mailId;`처럼 일반 규칙으로 돌아온다.

| 종류 | 밑바탕 | 상태 |
|---|---|---|
| `MailId` | `int64` | 적용됨 |
| `PlayerId` | `int64` | 적용됨 |
| `ZoneId` | `uint32`(RUID 아님) | 적용됨 |

새 id 종류를 만들 때는 `int64` 별칭을 하나 더 두지 말고 `Ids.h`에 StrongId 별칭을 먼저
추가한다.

### `StrongId`에 `R` 접두사를 안 붙인 이유

`RUID`는 이 프로젝트의 **id 체계 자체**라 출처 표시가 필요했지만, `StrongId`는 그 위에 얹는
범용 래퍼다. "전부 붙이면 접두사가 의미를 잃는다"는 같은 절의 단서를 따른다.

### 규칙 셋

- **생성할 때만 값이 들어간다.** 생성자가 `explicit`이라 raw 정수가 흘러들지 못하고, 만든
  뒤에는 같은 타입끼리의 복사 말고는 바꿀 수 없다.
- **`Value()`는 경계에서만** — 와이어/DB 인자로 넘길 때. 일반 로직에서 부르기 시작하면 이
  클래스가 있으나 마나가 된다. 로그는 `std::formatter` 특수화가 처리하므로 붙일 필요 없다.
- **직렬화는 그대로 된다.** 값 하나만 든 trivially-copyable + standard-layout이라
  `Packet::BinaryWriter::Write`의 기존 concept을 통과하고 바이트도 동일하다 — 즉 **와이어
  포맷이 바뀌지 않아서** C# 클라이언트/운영툴은 손댈 것이 없다.

### 예외 — 매개변수 타입이 기반 클래스라 실체를 안 말해줄 때

```cpp
virtual void Rollback(UnitOfWork& sink) const = 0;   // 이름을 unitOfWork 로 바꾸지 않는다
```

여기 실제로 넘어오는 것은 항상 `RollbackUnitOfWork`(전송 기능이 없는 통)인데 **선언 타입은
기반 클래스**라 그 사실이 시그니처에 안 드러난다. 이름이 타입보다 많은 것을 말하고 있으므로
그대로 둔다 — 이 규칙이 막으려는 건 "타입이 이미 말한 것을 이름이 되풀이하지 않는 것"이지,
타입이 못 말하는 것까지 지우는 게 아니다.

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

`Shared/Core/Src/Base/BasicTypes.h`가 `byte`/`size_t`/`int8_t`~`int64_t`/`uint8_t`~`uint64_t`를
전역으로 `using` 해놨고 양쪽 `pch.h`가 include한다(PCH 없는 `ProtocolClient`는 직접 include).
**이 10개 타입 한정** — `std::string`/`std::vector` 등 다른 표준 타입은 그대로 `std::`를 붙인다.

## `enum`은 항상 `enum class` + underlying type 명시

일반 enum 금지. **원소 적은 enum은 `uint8_t`가 기본**(`Log::ELogLevel`), **에러 코드처럼 계속
늘어나는 enum은 `int32_t`**(`Base::ECoreErrorCode`/`Common::EErrorCode`, `uint8_t`는 256개로 부족).

## `switch` 케이스는 스코프로 감싸고 `break`를 중괄호에 맞춘다

**본문이 두 문장 이상이거나 변수를 선언하면 `{ }`로 감싸고, `break;`는 여는 중괄호와 같은
열에 둔다.**

```cpp
switch (subTask)
{
case Common::EMailTask::Added:
    {
        playerManager.Write()->AddMail(clientSessionId, MailInfo{mailId, title, body, sendUt, endUt});
        autoSpCommands.Add(DbCommand{"dbo.usp_mails_upsert", {mailId.Value(), ...}});
    }
    break;

case Common::EMailTask::Removed:
    {
        playerManager.Write()->RemoveMail(clientSessionId, mailId);
        autoSpCommands.Add(DbCommand{"dbo.usp_mails_delete", {mailId.Value(), NowUt()}});
    }
    break;
}
```

### 예외 -- 한 문장이면 감싸지 않는다

`ToString()`류 매핑 switch가 대표적이다. 감싸면 오히려 한 줄짜리 대응표가 안 보인다.

```cpp
switch (category)
{
case ELogCategory::General: return "General";
case ELogCategory::Client:  return "Client";
case ELogCategory::World:   return "World";
}
return "Unknown";
```

여러 줄에 걸치더라도 **문장이 하나면** 그대로 둔다(줄 수가 아니라 문장 수로 판단한다) --
`LOG.Warning(...).KV(...).KV(...);` 한 줄짜리 체인이 두 줄로 접힌 경우가 그렇다.

### 왜

- **`case` 라벨은 스코프를 만들지 않는다.** 감싸지 않으면 한 case에서 선언한 변수가 뒤 case까지
  살아 있고, 초기화를 건너뛰는 경로가 생기면 `C2360`으로 막힌다. 미리 감싸두면 이 부류의
  에러가 아예 생기지 않는다.
- **`break;`를 중괄호 밖에 두면 case의 끝이 보인다.** 안에 넣으면 `}` 바로 위에 묻혀서, case가
  길어졌을 때 fallthrough인지 아닌지를 눈으로 확인하기 어렵다. 밖에 두면 `}`와 `break;`가
  같은 열에 서서 블록의 끝과 case의 끝이 한눈에 갈린다.

## `int` 대신 `int32_t`/`int64_t`

크기 불명확한 `int`/`long` 대신 고정폭 정수(네트워크/직렬화 값은 특히). **예외**: `int main()`,
`main(const int argc, ...)`, asio 콜백처럼 언어/라이브러리가 정확히 `int`를 강제하는 자리는
그대로 둔다.

## 식별자는 무조건 `Base::Ruid`로 발급한다

**새로 만드는 id는 예외 없이 `Base::Ruid::Create()`로 받는다.**
`playerId`, `mailId`처럼 DB에 영구히 남는 것은 물론이고, 새 콘텐츠의 id도 마찬가지다.

프로세스 기동 시 `Base::Ruid::Init(nodeId)`를 **정확히 한 번** 부른다 — 예약값(0)이거나
비트 폭을 넘거나 **두 번째 호출이면 그 자리에서 중단한다**(재초기화는 시퀀스를 0으로 되돌려
이미 나간 id를 다시 내준다). `--idtest`처럼 자기 노드 번호로 초기화하는 모드가 있으면
기동 경로의 `Init`이 그 분기보다 **뒤에** 있어야 한다.

한 프로세스에서 여러 노드를 흉내 내야 하는 테스트만 `Base::RuidGenerator`를 직접 만든다.

금지하는 것들과 이유:

| 하지 말 것 | 왜 |
|---|---|
| 프로세스 지역 카운터(`++nextId_`) | 프로세스가 재시작하면 1부터 다시 시작해 **과거 id와 겹친다.** 여러 프로세스면 애초에 처음부터 겹친다 |
| DB `IDENTITY` | 서버가 메모리에서 먼저 확정하고 클라이언트에 응답한 뒤 DB에 반영하는 구조(UnitOfWork)라, **응답에 담을 id가 없다** |
| `GUID`/무작위 | 클러스터드 인덱스 키로 쓰면 삽입이 인덱스 중간에 꽂혀 페이지 분할과 단편화가 난다 |
| 시각만 쓰기 | 같은 밀리초에 두 개가 나오면 겹친다 |

`RUID`는 **시각(41비트) + 노드(10비트) + 시퀀스(12비트)** 라 위 네 가지를 한 번에 푼다 —
프로세스가 재시작해도 시각이 앞으로만 가고, 노드 번호가 프로세스를 가르고, 시퀀스가 같은
밀리초 안을 가른다. 그리고 시각이 상위 비트라 **id가 시간순으로 커져서** 클러스터드 인덱스에
append-only로 쌓인다.

노드 번호는 대역이 정해져 있다(`Shared/Core/Src/Base/RUID.h`) — 0은 예약,
1\~99가 World, 100\~199가 Zone, 255가 운영툴. **겹치면 서로 같은 id를 발급하므로 새 프로세스
종류가 생기면 대역을 먼저 정하고 상수로 추가한다.**

한 가지 구분: **생성기는 하나지만 쓰는 쪽 이름은 용도를 따른다.** 같은 `RUID`가
`playerId`/`mailId`/`requestId`로 불리고, DB 컬럼도 `request_id`처럼 용도 이름을 쓴다.

## 에러는 문자열이 아니라 에러 코드로 식별 (Core용/콘텐츠용 두 개)

에러 코드 enum이 **두 개**이고, 서로 섞어 쓰지 않는다:

| enum | 위치 | 무엇 | 누가 보나 |
|------|------|------|-----------|
| `Base::ECoreErrorCode` | `Shared/Core/Src/Base/CoreErrorCode.h` | 프레이밍/인자 검증 실패(`PacketTooLarge`, `InvalidArgument`) | Core 내부. 밖으로 안 나감 |
| `Common::EErrorCode` | `Shared/Common/Src/ErrorCode.h` | 콘텐츠 처리 실패(`MailNotFound` 등) | Zone이 판정, 클라이언트가 표시 |

**콘텐츠 에러를 Core에 추가하지 않는다** — Core는 콘텐츠를 모르는 정적 라이브러리라,
에러가 하나 늘 때마다 `Core.lib`과 그걸 참조하는 실행 파일 전부가 다시 빌드된다
(`ELogCategory`를 프로젝트별로 나눈 것과 같은 이유). `Common::EErrorCode`는 클라이언트와
공유하는 계약이라 `PacketId`와 같은 곳에 있고, 콘텐츠별 100 단위 대역을 쓴다.

`Base::CoreException`(`: std::runtime_error`, 메시지+`ECoreErrorCode`를 함께 들고 다님,
`.Code()`로 조회)과 함께 쓴다. 문자열(`ex.what()`) 파싱으로 에러 종류를 구분하지 않는다 —
검증 실패는 `throw CoreException(ECoreErrorCode::Xxx, "메시지")`. 새 에러 조건은 **자기 대역
맨 뒤에 추가**(기존 값 정수가 바뀌면 과거 로그와 어긋남).
asio의 `std::error_code`/`std::system_error`(네트워크 계층)는 이미 코드 기반이라 대상 아님.

## 콘텐츠 로직 실패는 에러 코드로 반환하고, 호출부가 `SetError`로 옮긴다

`UnitOfWork&`를 받는 모델 함수는 **성공/실패를 `[[nodiscard]] Common::EErrorCode`로
반환한다.** 모델은 UoW의 에러 상태를 모르고, 태스크를 기록하는 용도로만 UoW를 쓴다.

```cpp
// 모델 -- 실패면 아무 상태도 바꾸지 않고, 태스크도 기록하지 않는다
[[nodiscard]] Common::EErrorCode Model::AddMail(Info info, Task::UnitOfWork& unitOfWork)
{
    if (mails_.contains(info.mailId))
    {
        return Common::EErrorCode::MailAlreadyExists;   // 이 시점에 바뀐 게 없다
    }

    mails_.emplace(info.mailId, info);
    unitOfWork.AddTask(AddMailTask{info});                // 상태가 바뀐 뒤에만 기록

    return Common::EErrorCode::None;
}

// 호출부(핸들러) -- 실패를 UoW로 옮기고 즉시 중단한다
if (const auto errorCode = player.Mail().Write()->AddMail(info, unitOfWork);
    errorCode != Common::EErrorCode::None)
{
    unitOfWork.SetError(errorCode);
    return;
}
```

**왜 `[[nodiscard]]`인가**: 커밋 지점(`UnitOfWork` 소멸자)이 `HasError()` 하나만 보고
"역순 롤백 + 클라에 에러 통지" / "World·클라로 전송"을 가른다. 호출부가 반환값을 무시하면
그 앞까지 바뀐 메모리가 **성공으로 커밋되고 DB에도 나간다** -- 실패한 요청이 부분 반영된 채로
남는 게 가장 되돌리기 어려운 상태다. `[[nodiscard]]`를 붙이면 그 실수를 사람이 아니라
컴파일러가 잡는다(경고 0 빌드 규칙과 짝).

**왜 모델이 직접 `SetError`를 부르지 않나**: 모델은 자기 데이터만 아는 클래스로 유지한다.
실패를 어떻게 전달할지(어느 UoW에, 어느 요청의 결말로)는 요청 흐름을 아는 호출부의 일이다.
모델이 UoW의 에러 상태까지 만지면, 같은 모델 함수를 다른 흐름(GM 명령, 주기 처리)에서 재사용할
때 그 흐름의 에러 규약과 충돌한다.

같이 지킬 것:

- **부분 적용 금지**: 클램프/부분 차감을 하지 않는다. 전부 적용되거나 하나도 적용되지 않는다.
  골드 50인데 100을 요청하면 0으로 깎지 말고 에러로 끊는다 -- 클라는 "100 썼다"로 알고 서버는
  "50 썼다"가 되면 그 순간부터 양쪽 상태가 갈린다.
- **실패하면 태스크를 기록하지 않는다**: 상태를 바꾼 뒤에만 `AddTask`를 부른다. 그래야 태스크
  목록이 곧 "실제로 적용된 변경"이 되고, 역순 롤백이 정확해진다.
- **첫 에러를 유지한다**: `SetError`는 이미 값이 있으면 덮어쓰지 않는다. 처음 난 실패가 진짜
  원인이고 그 뒤는 연쇄 실패일 가능성이 높다.
- **파싱 실패는 핸들러까지 오지 않는다**: 페이로드 해석은 디스패치 **앞**에서 끝난다
  (`Zone::RegisterPacketHandler`). 형식이 깨졌으면 로그만 남기고 버리고, UoW를 열지 않는다 --
  정상 클라이언트는 자기가 만든 구조체를 그대로 보내므로 실패할 수 없고, 실패했다면 조작이거나
  프로토콜 버전이 어긋난 것이라 콘텐츠가 답할 내용이 아니다. 그래서 핸들러가 받는 값은 이미
  해석이 끝난 요청 구조체(`C2ZMailBuy` 등, `Packet/ClientPackets.h`)이고, 핸들러에 남는 판단은
  **내용이 타당한가**(잔액 부족, 우편함 없음)뿐이다.
- **롤백은 전송 기능이 없는 임시 UoW를 넘겨 정상 함수를 재사용한다**: 롤백 전용 함수를 모델마다
  따로 만들지 않는다(`AddMail`을 되돌릴 때 `DelMail`을 그대로 쓴다). 그때 넘기는
  `Task::RollbackUnitOfWork`는 소멸자가 아무것도 전송하지 않아서, 롤백 중 쌓인 태스크가 조용히
  버려진다 -- 원래 UoW를 넘기면 "지급 안 했는데 삭제했다"는 태스크가 DB로 나가고, 순회 중인
  목록에 추가돼 반복자도 깨진다.
- **롤백은 실패할 수 없다는 게 전제다**: 롤백은 조금 전에 성공한 변경을 되돌리는 것뿐이라
  검증에 걸릴 이유가 없다(그래서 "롤백 실패 처리" 경로를 만들지 않는다). 반환값이 `None`이
  아니면 그건 에러 처리 대상이 아니라 **불변식이 깨졌다는 신호**이므로 `LOG.Error`로 남긴다 --
  실패해도 할 수 있는 일이 없으니 복구를 시도하지 말고 드러내기만 한다. 이 전제를 지키려면
  롤백 경로가 새 검증을 추가하지 않아야 한다(예: 되돌려 넣을 자리가 없어질 수 있는 상한을
  롤백에서 다시 검사하지 않는다).

## 존 콘텐츠는 큰 분류마다 파일 하나 (`PlayerMail`)

존 서버의 클라이언트 요청 처리는 **콘텐츠 큰 분류 = 파일 한 쌍**이다. `PlayerProcessor`는
입장/퇴장과 라우팅만 맡고, 우편이면 `Processor/PlayerMail.{h,cpp}`, 인벤토리면
`Handler/PlayerInventory.{h,cpp}`가 자기 패킷 등록과 핸들러를 전부 들고 있다.

```cpp
class PlayerMail final
{
public:
    PlayerMail() = delete;                                  // 전부 static -- 상태가 없다
    static void Register(PlayerPacketDispatcher& packetDispatcher);
private:
    static void HandleMailAdd(const PlayerContext& context, const C2ZMailAdd& packet);
};
```

지켜야 할 것:

- **등록은 콘텐츠가 스스로 한다**(`PlayerMail::Register`). 우편 패킷을 하나 늘릴 때
  `PlayerProcessor`를 고쳐야 한다면 파일을 나눈 의미가 없다.
- **핸들러는 전부 `static`**이다. 필요한 것은 전부 `PlayerContext`(플레이어, zoneId,
  WorldLink)로 들어온다. 멤버를 두면 플레이어 레인의 여러 스레드가 공유하는 변수가 된다.
- **모델은 나누지 않는다.** `Mail::Model`/`Currency::Model`은 그대로고, 나뉘는 것은 요청
  처리 쪽이다. 재화처럼 요청이 몇 개 없는 것은 자기 파일을 만들지 않고, 그 재화를 쓰는
  콘텐츠(`C2ZMailBuy`의 골드 차감)에 붙는다.
- **UoW는 핸들러가 연다.** 콘텐츠 하나의 트랜잭션 경계는 그 콘텐츠가 정한다.
- **롤백 함수를 콘텐츠에 만들지 않는다.** 실패하면 `UnitOfWork` 소멸자가 기록된 태스크를
  역순으로 훑으며 각 태스크의 역연산을 부른다 -- 콘텐츠마다 "무엇을 되돌리나"를 다시 적으면
  추가한 곳과 되돌리는 곳이 갈린다.
- **모델 변경도 DB 저장도 없는 것은 옮기지 않는다.** Move/Chat은 브로드캐스트가 전부라
  UoW를 열지 않고, 그래서 `PlayerProcessor`에 남는다.

## 가변 길이 본문에는 반드시 상한이 있어야 한다

**문자열이나 목록을 이어 붙여 패킷 본문을 만드는 코드에는 예외 없이 상한을 둔다.**
상한값은 `Shared/Common/Src/ContentLimit.h`에 모아둔다 -- Zone/World/클라이언트가 함께
지켜야 하는 계약이기 때문이다.

**왜 이게 치명적인가**: 프레임 헤더의 `bodySize`가 `uint16`이고, 받는 쪽 `Packet::Buffer`는
`Header::MaxBodySize()`(8192)를 넘는 본문을 만나면 **예외를 던지고 연결을 끊는다.** 그런데
서버 간 링크(Gateway↔World, Zone↔World)는 **재연결이 없다.** 즉 본문 하나가 상한을 넘으면
**그 링크에 붙은 전원이 끊기고 프로세스를 재시작해야 복구된다.** 실제로 둘 다 재현했다:

- 채팅 8,188자 한 줄 → Gateway↔World 링크 사망(그 게이트웨이의 **모든 클라이언트**)
- 우편 250통 보유자가 로그인 → `W2ZEnterZone` 본문 10,028바이트 → Zone↔World 링크 사망

65,536을 넘으면 더 나쁘다 -- `bodySize`가 랩어라운드해서 받는 쪽이 **나머지를 헤더로 해석**한다.
끊기지도 않고 스트림이 조용히 어긋난다.

### 세 겹으로 막는다

| 층 | 무엇 | 어디 |
|---|---|---|
| 콘텐츠 | 채팅 길이, 우편 제목/본문 길이, 우편함 통 수 | `ContentLimit.h` + 각 `Parse`/핸들러 |
| 조립 | 목록을 실을 때 **바이트 예산으로 잘라낸다** | `BuildEnterZoneBody` |
| 최후 | `BuildFrame`이 초과를 예외로 거부 | `Packet::BuildFrame` |

마지막 층이 backstop이다. 콘텐츠 상한을 빠뜨려도 **조용히 어긋나는 대신 보내는 쪽 로그에
원인이 남는다**(`Session::SendPacket`이 잡아서 그 패킷만 버리고 연결은 살린다).

### 어디서 거르나 -- 에러를 돌려줄 통로가 있는지로 가른다

```cpp
// 채팅: UnitOfWork가 없어 돌려줄 길이 없다 -> Parse에서 버린다
return message.size() <= Common::kMaxChatBytes;

// 우편: UnitOfWork가 있다 -> 핸들러가 에러 코드로 끊는다
if (IsMailTextTooLong(packet.title, packet.body))
{
    unitOfWork.SetError(EErrorCode::MailTextTooLong);
    return;
}
```

### DB 컬럼과 짝을 맞춘다

우편 제목/본문 상한은 `Sql/mails.sql`의 `NVARCHAR(128)`/`NVARCHAR(1024)`와 짝이다. 넘기면
SP가 "String or binary data would be truncated"로 실패해서 **메모리에는 들어갔는데 DB에는
없는** 상태가 된다. `NVARCHAR`는 바이트가 아니라 문자 수이므로, ASCII만 들어와도 넘지 않도록
**컬럼 크기를 그대로 바이트 상한으로** 쓴다.

### 상한 검사를 롤백 경로에 넣지 않는다

`Mail::Model::AddMail`에는 통 수 상한이 있고 `InsertMail`(롤백/복원)에는 없다. 넣으면 상한에
걸린 상태에서 삭제를 되돌릴 때 "되돌려 넣을 자리가 없다"가 되어 롤백이 실패한다 --
같은 문서의 "롤백은 실패할 수 없다는 게 전제다" 항목과 짝이다.

## 로그는 `std::cout`/`std::cerr` 대신 `LOG`

최상위 `Log` 네임스페이스(`Core::Log` 아님 — 아래 참고)의 전역 `LOG`(`Log::Proxy`)를 쓴다.
시작 시 한 번 `Log::Logger::Instance().Initialize("logs/xxx.log")` 호출(콘솔 코드페이지를
`CP_UTF8`로 맞춰 한글 로그 깨짐도 방지 — 새 실행 파일 프로젝트는 `main()` 맨 앞에서 호출 필수).

**형태**: `LOG.<Level>(category, "메시지").KV("Key", value).V(value2);`
(`<Level>` = Debug/Info/Warning/Error, `.KV`=이름 있는 값, `.V`=이름 없는 값, 둘 다 생략하고
`LOG.Info(category, "메시지");`만 써도 유효). `LOG.Error(...)`는 `Log::Entry<TCategory>`
임시객체를 반환하고 **문장이 끝나 소멸되는 시점에 한 줄을 커밋**하므로(`.KV`/`.V`는 `*this` 참조
반환) 반환값을 discard해도 정상 동작 — 그래서 `Debug`/`Info`/`Warning`/`Error`엔 일부러
`[[nodiscard]]`를 안 붙인다. 콘솔 색: Error=빨강, Warning=노랑(레거시 콘솔엔 주황이 없어 대체),
Info=초록, Debug=기본색. **예외**: `ProtocolClient` REPL 안내문/수신 로그(`PrintHelp`, `[recv]`)는
로그가 아니라 프로그램 UI라 `std::cout` 유지(단 인코딩 위해 `Logger::Initialize()`는 호출).

```cpp
LOG.Info(ELogCategory::Zone, "플레이어 입장").KV("Zone", zoneId_).KV("SessionId", sessionId);
// [2026-01-01 12:00:00.000] [INFO ] [Zone] 플레이어 입장 . Zone : 0, SessionId : 1
```

**`ELogCategory`는 프로젝트마다 따로 있다** — `Log::Entry<TCategory>`는 카테고리 값을
직접 갖지 않고 템플릿으로 받는다(scoped enum + 같은 네임스페이스의 ADL `ToString()`만 있으면
됨 = `LogCategoryType` concept). Core가 게임 콘텐츠를 몰라야 하므로: `Shared/Core/Src/Log/
LogCategory.h`의 `Log::ELogCategory{General,Network,Packet,Thread}`(Core 전용, 콘텐츠 없음)와
`ZoneServer`/`WorldServer`/`GatewayServer`/`StressClient`가 각자 자기 폴더에 갖는
`Zone`/`World`/`Gateway`/`Load` 네임스페이스의 `ELogCategory`(콘텐츠) — 이렇게 프로젝트 수만큼
분리돼 있다. 전부 `using`으로 전역 노출돼 있어 어디서든 `ELogCategory::Xxx`로 쓰지만, 서로
다른 프로젝트(PCH)에서만 보여 충돌 안 함. 새 프로젝트는 자기 폴더에 자기 `ELogCategory`+
`ToString()`을 새로 정의하면 된다(Core에 추가 금지) — `Entry`/`Proxy`는 손댈 필요 없음.

**왜 `Core::` 접두사가 없는가** — `Core` 아래 네임스페이스(`Network`/`Packet`/`Thread`/
`Timer`/`Common`/`Log`)는 전부 `Core::`를 안 붙인다(`namespace Core {...}`로 감싼 코드 없음).
계속 감싸면 거의 모든 시그니처가 `Core::`로 시작해 잡음이 컸다 — `Core`는 폴더/프로젝트
이름으로만 남고, 폴더-네임스페이스 대응 원칙(`Shared/Core/Src/Network/` ↔ `namespace Network`)은
그대로 유지하되 `Shared/Core/`라는 상위 폴더 두 겹만 생략한다. `ZoneServer`/`WorldServer`/
`GatewayServer`/`StressClient`는 각자 원래 네임스페이스(`Zone`/`World`/`Gateway`/`Load`)
하나뿐이라 이 얘기 자체가 해당 없음(폴더 한 겹 생략할 상위 폴더가 없음).

## 값을 소유할 필요 없으면 복사하지 않는다

**본문에서 `std::move`로 실제로 소유권을 넘기는 게 아니라면 `std::string`/`std::vector<T>`를
값으로 받지 않는다** — 읽기만 할 거면 `std::string_view`/`const T&`를 쓴다(by-value는 "이
함수가 소유권을 가져간다"는 신호로만). 실제 사례: `Log::Entry` 생성자가 원래 `std::string
message`(값)였지만 생성자 안에서 즉시 `std::format`으로 소비되고 저장 안 되길래
`std::string_view`로 바꿔 복사를 없앴다. `Instance::OnChat`도 같은 이유로 `const
std::string_view message`로 바꾸고 호출부의 불필요한 `std::move`도 제거했다. **판단 기준**:
"본문에서 이 매개변수를 다른 곳(멤버/컨테이너/다른 스레드로 가는 캡처)에 진짜 move하는가?" —
그렇다면 sink라 by-value가 맞고(`CoreException(ECoreErrorCode, std::string message)`처럼),
아니면 `string_view`/`const T&`/`span<const T>`를 쓴다.

**반대 방향도 규칙이다 — 작고 trivially copyable한 타입은 `const T&`가 아니라 `const T` 값으로
받는다.** `StrongId` 계열 id(`Common::PlayerId`/`MailId`/`ZoneId`), enum, `std::span`, 산술
타입이 여기 해당한다. x64 ABI가 이런 값을 **레지스터로** 넘기므로 복사 비용이 `int64_t`와 같고
(실측상 명령어가 동일), 참조로 받으면 역참조가 붙는 데다 컴파일러가 aliasing을 배제하지 못해
값을 다시 읽는다. "클래스니까 `const&`가 싸다"는 **힙을 들고 있거나 사용자 정의 복사 생성자가
있는 타입**에만 해당한다. 어셈블리 근거: `docs/design/parameter-passing.md`

## 스마트 포인터로만 소유되는 타입은 중첩 별칭을 둔다 (`SPtr`/`UPtr`/`WPtr`)

**어떤 타입이 항상 특정 스마트 포인터로만 소유된다면, 그 클래스 안에 별칭을 둔다.**
이름은 셋으로 고정한다 — 자리마다 다른 이름(`Ptr`/`Ref`/`Handle`)을 쓰면 별칭의 값이 없다.

| 별칭 | 대상 |
|---|---|
| `SPtr` | `std::shared_ptr<T>` |
| `UPtr` | `std::unique_ptr<T>` |
| `WPtr` | `std::weak_ptr<T>` |

```cpp
class Session final : public std::enable_shared_from_this<Session>
{
public:
    using SPtr = std::shared_ptr<Session>;
    ...
};

// 호출부
void OnPacket(const Network::Session::SPtr& session, ...);
Packet::Dispatcher<PacketId, Network::Session::SPtr> dispatcher_;
```

**왜**: `Session`은 `enable_shared_from_this`라 `shared_ptr`로만 소유된다 — 비동기 완료
핸들러가 도는 동안 자기 생존을 보장해야 하기 때문이다. 그 규약이 지금까지는 클래스 주석에만
있었고 시그니처에는 `std::shared_ptr<Network::Session>`이라는 긴 철자만 반복됐다.
별칭을 두면 **규약이 타입 이름에 붙는다.**

### 전방 선언과 같이 쓸 수 없다

**중첩 별칭은 클래스의 멤버라, 이름을 찾으려면 정의가 필요하다.** 전방 선언만으로는
`C2027 use of undefined type`이 난다.

```cpp
namespace Network { class Session; }        // 불완전 타입

std::shared_ptr<Network::Session> a;        // OK  -- shared_ptr는 불완전 타입을 허용한다
Network::Session::SPtr            b;        // C2027
```

그래서 별칭을 도입하면 **전방 선언하던 곳이 전부 include로 바뀐다.** `Session`의 경우
`IPacketHandler.h` 등 7곳이 그렇게 됐다.

**이 저장소에서는 그 비용이 거의 없어서 받아들였다** — 6개 `pch.h` 전부가 이미
`<asio.hpp>`를 include하므로, `Session.h`를 더 끌어와도 추가로 컴파일되는 것이 없다.
남는 비용은 헤더 결합(그 헤더가 바뀌면 재빌드 파급)뿐이고 `Session.h`는 안정적인 파일이다.

**pch에 무거운 의존이 없는 프로젝트라면 계산이 달라진다.** 그때는 별칭을 포기하거나,
네임스페이스 스코프 별칭(`namespace Network { class Session; using SessionPtr = ...; }`)을
쓴다 — 후자는 불완전 타입으로도 되지만, 이 저장소는 `Mutexed` 중첩 별칭과 모양을 맞추려고
중첩 쪽을 택했다.

### 언제 두나

- **둔다**: 소유 방식이 그 타입의 규약인 것(`Session`은 `shared_ptr` 전용).
- **두지 않는다**: 소유 방식이 쓰는 쪽 사정인 것. `std::unique_ptr<Timer::RepeatingTimer>`는
  그냥 그 자리에서 unique로 들고 있을 뿐이라 별칭이 규약을 말하지 않는다.
- 별칭을 둔 뒤에는 **그 타입에 raw `std::shared_ptr<T>`를 새로 쓰지 않는다** — 두 철자가
  섞이면 grep이 한쪽을 놓친다.

## 네임스페이스가 이미 말해주는 접두사는 타입 이름에서 뗀다

`namespace Zone`의 `ZoneServerConfig`는 호출부에서 `Zone::ZoneServerConfig`가 되어 "Zone"을
두 번 말한다. **타입 이름이 자기 네임스페이스 이름으로 시작하면 그만큼을 뗀다.**

```cpp
Zone::ZoneServerConfig  ->  Zone::Config
Zone::ZoneZoneProcessor ->  Zone::ZoneProcessor
Packet::PacketHeader    ->  Packet::Header
Processor::ProcessorGroup -> Processor::Group
Mail::MailModel         ->  Mail::Model
```

**파일 이름도 같이 바꾼다**(`Game/ZoneDef.h` → `Game/Def.h`) — 한 파일에 타입 하나, 파일
이름은 그 타입 이름이라는 규칙을 유지한다.

### 예외 — 뗐을 때 다른 것과 겹치면 그대로 둔다

| 그대로 두는 것 | 왜 |
|---|---|
| `Currency::CurrencyTask` | `Task`는 이 코드베이스의 **네임스페이스**(`Task::ITask`/`Task::UnitOfWork`)다. `Currency::Task`를 만들면 `Currency` 안에서 `Task::`가 무엇을 가리키는지 흔들린다 |
| `Log::Logger` | `Log` + `ger`이라 애초에 접두사가 아니다 |

### 같이 확인할 것 — 게터 이름과 충돌한다

떼고 나면 **같은 클래스의 멤버 함수 이름과 겹칠 수 있다.** 멤버 함수가 타입 이름을 가려서
선언 자체가 깨지는데, 에러 메시지(`C3646 알 수 없는 재정의 지정자`)가 원인을 안 가리킨다.

```cpp
class Instance
{
    const Def& Def() const { return def_; }   // 이 이름이 타입 Def 를 가린다
    Def def_;                                 // -> C3646
};
```

이럴 때는 게터를 `GetXxx`로 바꾼다(`GetDef()`/`GetStats()`) — 이 프로젝트의 다른 게터와도
일관된다. 타입 이름을 되돌리지 않는다.

### 파일 이름이 같아지면 obj가 충돌한다

`Mail/Model.cpp`와 `Currency/Model.cpp`처럼 이름만 같은 `.cpp`가 한 프로젝트에 생기면 MSVC가
**같은 obj 파일에 덮어쓴다**(`MSB8027`, 잘못된 빌드 결과). 그래서 6개 vcxproj 전부
`ItemDefinitionGroup`의 `ClCompile`에 아래를 둔다 — obj를 소스 폴더 구조 그대로 쌓는다.

```xml
<ObjectFileName>$(IntDir)%(RelativeDir)</ObjectFileName>
```

## 직접 만든 기반 타입에는 `R` 접두사를 붙인다

`UniqueId`처럼 **이 프로젝트가 직접 만든 기반 타입**인데 이름이 일반적이면, 표준/서드파티/
콘텐츠 쪽 이름과 충돌하거나 헷갈릴 여지가 크다. 그런 타입은 **`R` 접두사**를 붙여 출처를
이름에 박는다(개발자 이니셜).

```cpp
UniqueId           ->  RUID
UniqueIdGenerator  ->  RuidGenerator (프로세스 전역 진입점은 Ruid)
kInvalidUniqueId   ->  kInvalidRUID
```

**언제 붙이나**: 직접 설계한 기반 타입이고, 이름만 보면 표준 라이브러리나 남의 것으로
오해할 수 있을 때. `RUID`가 그런 경우다 — GUID/UUID와 쓰임은 비슷하지만 구조도 보장도
전혀 다른(시각 41 + 노드 10 + 시퀀스 12비트) **이 프로젝트 고유의 것**이라, 이름이
`UniqueId`면 "어디서 가져온 표준 유틸"로 읽힌다.

**언제 안 붙이나**: 콘텐츠 타입(`Mail::Model`, `Zone::ZoneProcessor`)과 인프라 타입
(`Session`, `Listener`)에는 붙이지 않는다. 이미 네임스페이스가 출처를 말해주고,
전부 붙이면 접두사가 의미를 잃는다.

한 가지 유지되는 구분: **생성기는 하나지만 쓰는 쪽 이름은 용도를 따른다.** 같은 `RUID`가
`playerId`/`mailId`/`requestId`로 불리고, DB 컬럼도 `request_id`처럼 용도 이름을 쓴다.
