namespace Client.Protocol;

/// <summary>
/// 이 클라이언트가 쓰는 패킷 타입. C++ <c>Protocol::PacketId</c>
/// (<c>Shared/Protocol/Src/PacketId.h</c>)와 이름·값이 1:1로 같아야 한다.
///
/// <para>
/// 이름 앞 3글자는 "보내는 노드 2 받는 노드"이고, 방향마다 1000 단위로 번호 대역이 잘려 있다.
/// C++ enum에는 서버 내부 링크(G2W/W2G/W2Z/Z2W)와 운영툴(T2W/W2T) 대역도 함께 들어 있지만,
/// <b>이쪽은 클라이언트 대면 대역만 선언한다</b> — C2Z(존이 처리) / Z2C(존 응답·통지) /
/// C2W(World가 처리 = 로그인) / W2C(World가 직접 보냄) 넷이 클라이언트가 볼 수 있는 전부다. 릴레이 봉투
/// (<c>ClientEnvelopeHeader</c>)는 GatewayServer가 벗겨서 넘겨주므로 클라이언트에 도달하는
/// 바이트는 항상 <c>Header</c> + 순수 페이로드다.
/// </para>
/// </summary>
public enum PacketId : ushort
{
    /// <summary>C2Z: 상태 확인용. 존의 LB 스레드가 게임 로직을 거치지 않고 즉시 되돌려준다.</summary>
    C2ZEcho = 1,

    /// <summary>C2Z: 길이 접두 문자열. 같은 존에 브로드캐스트된다(본인 포함).</summary>
    C2ZChat = 2,

    /// <summary>C2Z: <c>MovePacket</c>(x, y). 목표 좌표가 존 경계를 넘으면 핸드오프가 일어난다.</summary>
    C2ZMove = 3,

    /// <summary>C2Z: title/body/durationSec. 결과는 <see cref="Z2CTaskResult"/>로 돌아온다.</summary>
    C2ZMailAdd = 4,

    /// <summary>C2Z: mailId(int64 RUID). 결과는 <see cref="Z2CTaskResult"/>로 돌아온다.</summary>
    C2ZMailDel = 5,

    /// <summary>
    /// C2Z: targetUnitId(uint32) + attackKind(uint8). <b>서버에서 유일하게 존 레인으로 가는
    /// 요청이다</b> — 때리는 대상이 남이라 주인이 세션이 아니라 존이고, 그래서 틱(리젠·리스폰·
    /// 몬스터 AI)과 같은 스레드에서 처리된다. 결과는 <see cref="Z2CAttackResult"/>로 오고,
    /// <b>실패해도 반드시 온다</b>.
    /// </summary>
    C2ZAttack = 7,

    /// <summary>Z2C: <see cref="C2ZEcho"/>의 응답. 본문은 보낸 바이트 그대로(길이 접두 없음).</summary>
    Z2CEchoAck = 1001,

    /// <summary>Z2C: senderId(uint32) + 메시지(길이 접두). 존 내부 브로드캐스트.</summary>
    Z2CChatNotify = 1002,

    /// <summary>Z2C: moverId(uint32) + <c>MovePacket</c>. 존 내부 브로드캐스트.</summary>
    Z2CMoveNotify = 1003,

    /// <summary>Z2C: 존 배정 통지. 신규 입장과 핸드오프 전입이 같은 패킷으로 온다.</summary>
    Z2CEnterZoneNotify = 1004,

    /// <summary>
    /// Z2C: errorCode(int32) + requestPacketId(uint16) + UnitOfWork 태스크 스트림.
    /// requestPacketId가 0이면 요청 없이 서버가 만든 변경(우편 만료 삭제, 운영툴 우편 발송).
    /// </summary>
    Z2CTaskResult = 1007,

    /// <summary>
    /// Z2C: errorCode(int32) + targetUnitId(uint32) + attackKind(uint8) + damage(int32) +
    /// targetHp(int32). <b>시전자에게만</b> 오고 실패해도 온다 — 화면이 무응답으로 멈추는
    /// 경로를 만들지 않으려는 서버 쪽 규약이다.
    /// </summary>
    Z2CAttackResult = 1008,

    /// <summary>
    /// Z2C: attackerUnitId(uint32) + targetUnitId(uint32) + attackKind(uint8) + damage(int32).
    /// 시전자를 <b>뺀</b> 존 전체에 간다(시전자는 자기 <see cref="Z2CAttackResult"/>로 같은
    /// 연출을 그린다). 성공한 공격만 나가므로 에러 코드가 없다.
    /// </summary>
    Z2CUnitAttackNotify = 1009,

    /// <summary>
    /// Z2C: count(uint16) + count개의 {unitId(uint32), kind(uint8), x/y(float), hp/maxHp/mp/maxMp(int32)}.
    /// 입장할 때는 존 전체 목록이, 리스폰할 때는 한 기가 온다. 수가 많으면 여러 장으로 쪼개서 온다.
    /// </summary>
    Z2CUnitSpawn = 1010,

    /// <summary>Z2C: unitId(uint32). 존을 떠난 것이라 <see cref="Z2CUnitDead"/>와 다르다.</summary>
    Z2CUnitDespawn = 1011,

    /// <summary>
    /// Z2C: count(uint16) + count개의 {unitId(uint32), hp(int32), mp(int32)}.
    /// <b>틱 끝에 한 번</b>, 그 틱에 값이 바뀐 유닛만 모아서 온다.
    /// </summary>
    Z2CUnitStateSync = 1012,

    /// <summary>Z2C: unitId(uint32) + killerUnitId(uint32). 틱을 기다리지 않고 즉시 온다.</summary>
    Z2CUnitDead = 1013,

    /// <summary>
    /// C2W: playerName + password(둘 다 길이 접두 UTF-8). <b>존이 아니라 World가 처리한다</b> —
    /// 계정이 없으면 서버가 그 자리에서 만들고(자동 가입), 성공하면 곧이어 존에 입장시킨다.
    /// 이 패킷이 통과하기 전에는 다른 C2Z 패킷이 존까지 가지 않는다.
    /// </summary>
    C2WLogin = 2001,

    /// <summary>W2C: World가 존을 거치지 않고 접속 중 전체에 직접 뿌리는 공지(길이 접두 문자열).</summary>
    W2CNotice = 3001,

    /// <summary>
    /// W2C: <see cref="C2WLogin"/>의 결과. errorCode(int32) + playerId(int64) + playerName(길이 접두).
    /// 실패면 playerId=0에 이름도 비어 있다.
    /// </summary>
    W2CLogin = 3002,
}
