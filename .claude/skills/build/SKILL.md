---
name: build
description: asio-server를 Visual Studio GUI 없이 MSBuild CLI로 빌드하고 에러를 진단한다. "빌드해줘", "빌드 안돼", "컴파일 에러 확인해줘", "빌드 되는지 확인" 같은 요청에 사용한다.
---

# asio-server 빌드 (MSBuild CLI)

이 프로젝트는 CMake가 아니라 클래식 `.vcxproj`/`.slnx`다 (`CLAUDE.md` 참고, 되돌리지 말 것).
Visual Studio GUI 없이 Bash(Git Bash)에서 빌드를 검증할 때 아래 순서를 따른다.

## 1. MSBuild.exe 경로 찾기

`msbuild`는 기본 PATH에 없다. `vswhere`로 설치된 VS 인스턴스에서 찾는다. Git Bash에서 백슬래시
글롭 패턴이 씹히므로 **작은따옴표로 감싸서** 넘긴다:

```bash
VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
MSBUILD=$("$VSWHERE" -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe')
echo "$MSBUILD"
```

## 2. Git Bash 경로 치환 문제 우회

Git Bash는 `/p:Configuration=Debug` 같은 인자를 파일 경로로 오인해서 뭉갠다(`/m`이 `M:/`로
바뀌는 등). 반드시 두 가지를 같이 한다:

- `export MSYS_NO_PATHCONV=1`
- `/p:` 대신 **`-p:`** 스타일 스위치 사용 (MSBuild는 `-`와 `/` 둘 다 허용한다)

## 3. 빌드 실행

```bash
export MSYS_NO_PATHCONV=1
cd /c/Work/asio-server
"$MSBUILD" asio-server.slnx -p:Configuration=Debug -p:Platform=x64 -m -nologo -v:minimal
```

- `Platform`은 항상 `x64`만 지원한다 (`PlatformToolset=v143`, x64 전용).
- `Configuration`은 `Debug` 또는 `Release`.
- 클린 재빌드가 필요하면 `-t:Rebuild` 추가. 특정 프로젝트만 빌드하려면 `-t:ZoneServer`처럼
  프로젝트명 추가(`-t:Rebuild`와 같이 못 씀 — 하나만 선택).
- 산출물: `bin/x64/$Configuration/{Core.lib, GatewayServer, WorldServer, ZoneServer,
  TestClient, LoadTestClient}.exe`.

## 4. 에러 읽는 법

- 출력은 한글 로케일 MSBuild라 메시지 자체는 한글이지만, **에러 코드(`C2061`, `C1010` 등)는
  로케일과 무관하게 항상 영문 그대로** 나온다. `error C` 로 grep하면 빠르게 추린다.
- `C4819`(코드 페이지 경고)가 다시 보이면 `/utf-8` 플래그가 어떤 `.vcxproj`에서 빠졌다는
  신호다 — `.claude/rules/cpp-patterns.md`의 "파일 인코딩" 절 참고.
- `C1010`(precompiled header 관련)이 나면 새로 추가한 `.cpp`에 `#include ".../pch.h"`를
  첫 줄에 안 넣은 것이다.
- 로그가 길면 `tail -n 150` 정도로 잘라서 본다 — 에러는 보통 끝부분에 몰려 있다.

## 5. 실행 확인 (선택)

`ZoneServer`는 클라이언트를 직접 accept하지 않고 `Connector`로 World에 나가서 붙는 구조라,
단독 실행만으로는 "정상 기동"을 확인하기 어렵다(World 없이 실행하면 접속 재시도 로그만
계속 남음). 전체 파이프라인을 빠르게 확인하려면:

```bash
cd /c/Work/asio-server
"./bat/start_server_all.bat"   # WorldServer -> ZoneServer(0,1) -> GatewayServer 순서로 새 창 3개
```

각 창의 로그(`logs/*.log`)에 "대기 시작"/"연결 성공" 라인이 정상적으로 찍히는지 확인한다.
한글이 깨져 보이면 `/utf-8` 플래그 회귀를 의심할 것.
