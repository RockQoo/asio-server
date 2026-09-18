# `StrongId` — id를 자기만의 타입으로 만든다

대상: `Server/Common/Src/StrongId.h`, `Server/Common/Src/Ids.h`

## 왜 만들었나

id를 `int64` 별칭으로만 두면 **여러 종류의 id가 같은 타입을 나눠 쓰게 되고, 컴파일러가
섞이는 것을 막지 못한다.** 실제로 이런 코드가 조용히 통과하고 있었다.

```cpp
enterState.playerId = static_cast<uint32_t>(clientSessionId);   // 컴파일 통과
```

`playerId`(계정의 영속 키)에 `clientSessionId`(연결마다 새로 붙는 번호)를 넣은 것이라,
재접속할 때마다 값이 바뀌는 버그였다. 타입이 같으니 캐스팅 한 번으로 통과한다.

`StrongId`는 태그 타입으로 이것을 갈라, **같은 실수를 컴파일 에러로 만든다.**

```cpp
using PlayerId = StrongId<struct PlayerIdTag, int64_t>;
using MailId   = StrongId<struct MailIdTag,   int64_t>;
using ZoneId   = StrongId<struct ZoneIdTag,   uint32_t>;
```

밑바탕이 똑같이 `int64_t`인 `PlayerId`와 `MailId`가 서로 다른 타입이 된다.

## 막는 것 / 여는 것

| 막는다 | 연다 |
|---|---|
| 다른 종류의 id 대입·비교 | 같은 종류끼리 `==`/`!=`/`<=>` |
| raw 정수와의 대입·비교 | 해시(맵 키) |
| 암묵 변환 | 생성자(`explicit`), `Value()`(와이어·DB 경계용) |

`explicit`이 이 클래스의 전부다. 암묵 변환을 허용하면 raw 정수가 그대로 흘러들어와 타입을
나눈 의미가 사라진다.

## 태그를 정의하지 않는 이유

```cpp
using MailId = StrongId<struct MailIdTag, int64_t>;
//                      ^^^^^^ 선언만. 정의가 없다
```

태그는 **타입을 가르는 역할만** 하고 실체가 필요 없다. 정의하지 않으면 불완전 타입이라
누가 실수로 인스턴스를 만들 수도 없고, 별도 헤더에 선언을 모을 필요도 없다.

## 왜 `Server/Core`가 아니라 `Server/Common`인가

이 id들은 **Zone/World/클라이언트가 함께 읽고 쓰는 계약**이다. `Server/Core`는 게임 콘텐츠를
몰라야 하는 정적 라이브러리라 여기 두면 의존 방향이 뒤집힌다 -- `PacketId`/`TaskKind`를
`Server/Common`에 둔 것과 같은 근거다.

부수 효과로 `Core`는 `Server/Common`을 단 한 줄도 include하지 않는다. 그래서
`Network::SessionId`(연결 축)와 `Common::PlayerId`(계약 축)가 다른 네임스페이스에 있다 --
우연이 아니라 이 의존 방향의 결과다.

## 복사·전달 특성은 건드리면 안 된다

값 하나만 든 **trivially copyable + standard-layout**이라 두 가지가 공짜로 따라온다.

1. **값 전달이 `int64_t`와 같은 비용**이다(x64 ABI가 레지스터로 넘긴다). 그래서 이 저장소는
   id를 전부 `const T` 값으로 받는다 -- 근거와 실측: [parameter-passing.md](parameter-passing.md)
2. **와이어 포맷이 바뀌지 않는다.** `Packet::BinaryWriter::Write`의 기존 concept을 그대로
   통과하고 바이트도 동일해서, C# 클라이언트/운영툴은 손댈 것이 없었다.

**복사 생성자·소멸자를 선언하면 둘 다 깨진다**(클래스 밖 `= default`도 포함 -- user-provided가
되어 trivial 자격을 잃는다). 헤더에 경고 주석이 그래서 남아 있다.

## 같이 볼 것

- 네이밍(`R` 접두사를 안 붙인 이유, `Value()`는 경계에서만, 새 id 종류 추가 절차):
  `.claude/rules/cpp-patterns.md`의 "지역 변수 이름은 타입 이름을 따른다"
- id 발급 체계(RUID): [unique-id.md](unique-id.md)
