# Client

게임 클라이언트 자리. 아직 비어 있다.

MonoGame(C#) 또는 Python 클라이언트를 붙일 예정이며, 둘 다 C++ `Shared/Core`를 쓰지 않는다.
현재 저장소에 있는 `Tool/TestClient`(프로토콜 REPL)와 `Tool/LoadTestClient`(부하 측정)는
게임 클라이언트가 아니라 서버를 두드리는 도구라 `Tool/`에 있다.

## 붙이기 전에 정리해야 할 것

와이어 프로토콜 정의가 지금은 서버 프로젝트 안에 C++ 헤더로만 존재한다
(`Server/ZoneServer/Src/Packet/ZonePackets.h`, `Server/WorldServer/Src/Packet/ZoneLinkPackets.h`).
C#/Python 클라이언트가 생기면 같은 포맷을 각 언어로 다시 정의해야 하고, 손으로 베끼면
필드가 바뀔 때마다 조용히 어긋난다. `Shared/Protocol/`에 단일 스키마를 두고 각 언어를
생성하는 방식으로 옮기는 것이 전제 작업이다.
