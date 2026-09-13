# 설정 파일 — 왜 파서를 직접 만들었나

대상 코드: `Shared/Core/Src/Common/ConfigFile.h`, `config/*.cfg`

## 형식

```ini
# 줄 맨 앞의 #, 또는 공백 뒤의 # 부터는 주석
world_port = 9200

# 중첩은 점으로 평탄화한다
pools.player_threads = 8
intervals.tick_ms = 100
```

## 왜 TOML/JSON/YAML이 아닌가

**표준 C++에는 셋 다 파서가 없다.** 어떤 형식을 고르든 라이브러리를 가져와야 한다.

| | 가져오는 것 | 빌드 영향 |
|---|---|---|
| toml++ | `.hpp` 1개 | include 경로만 |
| nlohmann/json | `.hpp` 1개 | 동일 |
| yaml-cpp | 소스 트리 + `.lib` | vcxproj 추가, 링크 설정 |

그런데 이 저장소는 **"외부 의존성은 벤더링한 standalone ASIO 하나뿐"**이 설명의 일부다.
설정 파일 하나 읽자고 그 문장을 깨는 것보다, `key = value` 파서 40줄을 두는 쪽이 남는 게 많다.
실무 서버도 설정은 자체 포맷을 쓰는 경우가 흔하다.

**중첩을 점으로 평탄화**하면 `[pools]` 같은 섹션 문법이 없어도 구조체와 1:1로 맞는다 —
`pools.player_threads` → `config.poolSizes.playerThreadCount`.

## 없는 키와 틀린 값의 처리가 다르다

| 상황 | 처리 | 왜 |
|------|------|-----|
| 파일이 없다 | 경고 + 기본값으로 기동 | 설정 없이도 뜨는 편이 개발에 편하다 |
| 키가 없다 | 호출부가 준 기본값 | 파일에 모든 키를 적지 않아도 된다 |
| **값이 틀렸다** | **던진다** | 기본값으로 덮으면 "왜 9200이 아니라 9100에 붙지"를 런타임에 추적하게 된다 |
| **아무도 안 읽은 키** | **경고** | 오타를 잡는 유일한 수단 |

마지막 항목이 중요하다. `pool.player_threads`라고 잘못 적으면 그 키는 그냥 무시되고
**기본값으로 뜨는데 아무 증상이 없다.** 그래서 기동 때 읽히지 않은 키를 전부 경고로 남긴다.

정수 파싱도 `std::from_chars`가 성공했는지만 보지 않고 **끝까지 읽었는지**(`ptr == end`)를
확인한다 — 안 그러면 `8 threads` 같은 값이 `8`로 조용히 통과한다.

## 기본값은 구조체에만 적는다

```cpp
Zone::Config config{};                                    // 구조체의 기본값
config.lbThreadCount = file.GetSize("lb_threads", config.lbThreadCount);   // 그 값을 fallback 으로
```

기본값이 구조체와 `main.cpp` 두 군데에 적히면 한쪽만 고쳤을 때 갈린다. 그래서 **읽는 쪽이
구조체의 현재 값을 그대로 fallback 으로 넘긴다.**

## 인자로 남긴 것 — 프로세스마다 달라야 하는 값

`ZoneServer`의 담당 존 목록(`ZoneServer.exe 1,2`)은 설정 파일이 아니라 실행 인자다.
**프로세스마다 달라야 하는 유일한 값**이라, 파일에 넣으면 존 프로세스 수만큼 파일이 갈린다.
지금은 존 4개를 프로세스 2개가 나눠 맡으면서 같은 `zone.cfg` 하나를 공유한다.

같은 이유로 `pools.zone_threads = 0`은 "담당 존 수에 맞춘다"는 뜻이다 — 존 하나는 스레드
하나가 상한이라 그 이상 줘도 빨라지지 않고, 담당 존 수는 프로세스마다 다르다.

## 설정 파일이 실행 파일 옆으로 복사된다

`bat` 스크립트가 `bin/x64/{Debug,Release}`로 `cd` 한 뒤 실행하므로, 프로그램은
`config/zone.cfg`를 **상대 경로**로 찾는다. 그래서 각 서버 vcxproj의 PostBuildEvent가
자기 설정 파일 하나만 `$(OutDir)config\`로 복사한다.

자기 것만 복사하는 이유는 **병렬 빌드에서 세 프로젝트가 같은 파일을 동시에 쓰면 충돌**하기
때문이다. 다른 설정으로 띄우려면 경로를 인자로 준다:

```bat
ZoneServer.exe 1,2 D:\configs\zone-load-test.cfg
WorldServer.exe   D:\configs\world.cfg
```
