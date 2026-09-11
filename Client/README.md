# Client

게임 클라이언트. C# / .NET 10 / MonoGame이며 **별도 솔루션**(`Client.slnx`)이다 — C++ 솔루션에
섞으면 서버만 빌드할 때 NuGet 복원까지 끌려온다(GmTool과 같은 이유).

원래 `Tool/VisualClient`에 있었는데, 서버를 두드리는 **도구**가 아니라 실제로 게임을 그리는
**클라이언트**라 여기로 옮겼다. 지금은 존 격자·핸드오프·채팅·우편·쿠폰을 눈으로 확인하는
수준이고, 리소스와 콘텐츠를 채우면 그대로 게임 클라이언트가 된다.

`Tool/ProtocolClient`(프로토콜 REPL)와 `Tool/StressClient`(부하 측정)는 게임 클라이언트가
아니라 서버를 두드리는 도구라 계속 `Tool/`에 있다.

## 실행

```bat
bat\start_client.bat 2          :: 창 2개 (브로드캐스트 확인에는 최소 2개 필요)
bat\start_client.bat 2 Debug auto   :: 창 2개, 자동 순회 (방향을 서로 반대로)
```

쿠폰 기능을 쓰려면 운영툴도 함께 띄워야 한다(SQL Server 필요) —
`bat\setup_gmtool_db.bat` 그리고 `bat\start_gmtool.bat`.

## 구조

| 위치 | 역할 |
| --- | --- |
| `Client/Src/Protocol/` | 코덱(GmTool.Core에서 복사) + PacketId(C2Z/Z2C/W2C 대역만) + `ZoneLayout` |
| `Client/Src/Net/` | `GameLink`(TCP+프레이밍, 수신은 큐에만 넣는다), `CouponClient`(GmTool.Web HTTP) |
| `Client/Src/Model/WorldModel` | 게임 스레드 전용 상태라 락이 없다(`ZoneInstance`와 같은 이유) |
| `Client/Src/Text/GlyphAtlas` | 한글 글리프를 런타임에 GDI+로 굽는다(`.mgcb` 미사용) |
| `Client/Src/Ui/` | Painter / Widgets / ZoneView / ChatPanel / MailPanel / CouponPanel / Hud |

## 손댈 때 주의

**`ZoneLayout.cs`는 서버 `ParseZoneList`의 `kZoneSize`/`kZonesPerRow`/`kZoneRows`를 복제한
것이다.** 한쪽만 고치면 화면 경계와 실제 핸드오프 지점이 어긋난다 — 반드시 짝을 맞출 것.

**와이어 프로토콜 정의가 아직 서버 C++ 헤더에만 있다**(`Server/ZoneServer/Src/Packet/ZonePackets.h`,
`Server/WorldServer/Src/Packet/ZoneLinkPackets.h`). 이 클라이언트가 같은 포맷을 C#으로 다시
정의하고 있어서, 필드가 바뀌면 조용히 어긋난다. `Shared/Protocol/`에 단일 스키마를 두고 각
언어를 생성하는 방식으로 옮기는 것이 남은 숙제다.
