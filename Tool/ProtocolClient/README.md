# ProtocolClient

게임 프로토콜을 **손으로 왕복시켜 보는 REPL 더미 클라이언트**. 게임 클라이언트가 아니고,
자동화 테스트 스위트도 아니다 — 서버가 보낸 패킷을 눈으로 확인하는 도구다.

이걸 직접 만든 이유: 프로토콜이 길이 프리픽스 붙은 **바이너리**라서 telnet이나 curl로는
한 바이트도 검증할 수 없다. 헤더를 조립하고 응답 프레임을 풀어주는 최소한의 상대가 필요했다.

## 빌드 / 실행

솔루션(`asio-server.slnx`) 빌드에 포함된다. 산출물은 `bin/x64/Debug/ProtocolClient.exe`.

```
bat\start_server_all.bat     서버 3종(World -> Zone -> Gateway) 먼저 기동
bat\start_protocol_client.bat    ProtocolClient 접속 (기본 127.0.0.1:9000 = GatewayServer)
```

접속 대상은 **GatewayServer**다. World/Zone에 직접 붙지 않는다 — 클라이언트가 보는 표면은
Gateway뿐이라는 구조를 그대로 따른다.

## 명령어

| 명령 | 동작 |
|---|---|
| `echo <문자열>` | Echo 패킷. 그대로 되돌아오는지 확인. ZoneServer의 LB 스레드에서 즉시 응답하는 유일한 패킷이라, 게임 상태를 거치지 않는 경로 확인용이다 |
| `move <x> <y>` | Move 패킷. 같은 존에 브로드캐스트되고 본인도 받는다. **`x`가 10 경계를 넘으면 존 핸드오프가 일어난다** |
| `chat <문자열>` | Chat 패킷. 같은 존에 브로드캐스트, 본인도 수신 |
| `mail add <제목> <본문> <초>` | MailAdd 패킷. 지정한 초가 지나면 서버가 자동으로 만료 삭제한다 |
| `mail del <id>` | MailDel 패킷 |
| `help` | 도움말 |
| `quit` / `exit` | 종료 |

## 존 핸드오프를 눈으로 보는 방법

이 프로젝트에서 가장 확인할 가치가 있는 동작이다. `move`로 `x` 좌표를 10 경계 너머로 보내면:

```
move 5 5      -> 존 0에서 브로드캐스트
move 15 5     -> [recv] Z2CEnterZoneNotify zoneId=1
```

**재접속도 재인증도 없이 같은 TCP 연결에서 zoneId만 바뀐다.** WorldServer가 라우팅
테이블(`ClientRegistry`)만 고쳐서 처리하기 때문이고, Gateway는 이동이 일어난 사실 자체를
모른다. 창을 두 개 띄워 한쪽만 존을 옮기면, 옮긴 뒤부터 서로의 `chat`이 보이지 않게 되는 것도
같은 흐름으로 확인할 수 있다.

## 구조

`Src/main.cpp` 파일 하나다. 클래스로 나누지 않은 것은 의도한 선택이다.

수신은 별도 스레드에서 블로킹 `read_some`을 돌린다 — stdin 입력을 기다리는 메인 스레드를
막지 않기 위한 것뿐이고, 서버처럼 `asio::async_read`를 쓸 이유가 없다. 프레이밍만
`Shared/Core`의 `Packet::PacketBuffer`/`BuildFrame`을 그대로 재사용해서, 서버와 클라이언트가
같은 프레이밍 코드를 공유한다(여기서 어긋나면 프로토콜 검증 자체가 무의미해진다).

패킷 정의는 `Server/ZoneServer/Src/Packet/ZonePackets.h`를 직접 include한다. 도구가 서버
헤더를 보는 방향이라 의존 방향은 문제없지만, 나중에 다른 언어 클라이언트가 생기면 스키마를
`Shared/`로 올려야 한다(`Client/README.md` 참고).

## 한계

- 인증이 없다. 접속하면 곧바로 세션이 열린다.
- 한 프로세스에 한 세션이다. 동시 접속 부하는 `Tool/StressClient`가 담당한다.
- 응답을 파싱해 판정하지 않고 화면에 찍기만 한다. 통과/실패 판정은 사람이 한다.
