namespace Client.Model;

/// <summary>
/// 유닛 종류. 서버 <c>Combat::EUnitKind</c>와 값이 같다.
/// </summary>
public enum UnitKind : byte
{
    None = 0,
    Player = 1,
    Monster = 2,
}

/// <summary>공격 종류. 서버 <c>Combat::EAttackKind</c>와 값이 같다.</summary>
public enum AttackKind : byte
{
    Melee = 0,
    Ranged = 1,
}

/// <summary>
/// 전투 유닛 하나. <b>플레이어 유닛의 <see cref="UnitId"/>는 세션 id와 같은 값</b>이라
/// <see cref="WorldModel.Players"/>의 키와 그대로 이어진다 — 서버가 일부러 그렇게 잘라서
/// 보낸다(<c>Protocol::UnitId</c> 주석). 그래서 "움직이는 원"과 "HP 바"를 잇는 변환표가
/// 클라이언트에 없다.
///
/// <para>
/// 좌표는 <b>몬스터만</b> 여기 들고 있다. 플레이어의 권위 좌표는 서버가 이동 통지로 따로
/// 보내주므로(<see cref="RemotePlayer"/>), 같은 값을 두 군데 두면 어느 쪽이 최신인지
/// 알 수 없어진다.
/// </para>
/// </summary>
public sealed class CombatUnit
{
    public uint UnitId { get; init; }

    public UnitKind Kind { get; init; }

    /// <summary>몬스터 좌표. 플레이어는 <see cref="RemotePlayer"/> 쪽을 본다.</summary>
    public float X { get; set; }

    public float Y { get; set; }

    public int Hp { get; set; }

    public int MaxHp { get; set; }

    public int Mp { get; set; }

    public int MaxMp { get; set; }

    public bool IsDead => Hp <= 0;

    /// <summary>
    /// 무기가 바라보는 방향(라디안). 공격할 때 대상 쪽으로 돌려두고 그대로 유지한다 —
    /// 서버에는 방향 값이 없어서 클라이언트가 만들어 내는 값이다.
    /// </summary>
    public float WeaponRadians { get; set; }

    /// <summary>마지막으로 공격한 시각(초). 휘두르기 연출의 시작점이다.</summary>
    public double AttackAtSeconds { get; set; } = double.NegativeInfinity;

    public AttackKind LastAttackKind { get; set; }

    /// <summary>마지막으로 피격당한 시각(초). 몸통을 잠깐 붉게 물들이는 데 쓴다.</summary>
    public double HitAtSeconds { get; set; } = double.NegativeInfinity;

    public double DeadAtSeconds { get; set; } = double.NegativeInfinity;
}

/// <summary>
/// 날아가는 화살 하나. <b>순수 연출이다</b> — 서버는 원거리 공격도 즉발로 판정해서 데미지를
/// 이미 확정해 보냈고, 이 화살이 도착하든 말든 결과는 바뀌지 않는다.
///
/// <para>
/// 서버에 비행 시간을 두지 않은 이유는 연출이 아니라 상태 때문이다 — 비행 중이라는 상태가
/// 생기면 "날아가는 중에 대상이 죽으면", "존을 나가면", "시전자가 끊기면"의 답을 전부
/// 정해야 한다. 보이는 그림은 이쪽이나 저쪽이나 같다.
/// </para>
/// </summary>
public sealed record ArrowEffect(
    float FromX, float FromY, float ToX, float ToY, double StartSeconds, double DurationSeconds)
{
    public double EndSeconds => StartSeconds + DurationSeconds;

    /// <summary>0(발사)에서 1(도착)까지의 진행도.</summary>
    public float Progress(double nowSeconds) =>
        (float)Math.Clamp((nowSeconds - StartSeconds) / DurationSeconds, 0.0, 1.0);
}

/// <summary>피격 지점에서 떠오르는 데미지 숫자. 런타임 글꼴로 그리므로 리소스가 필요 없다.</summary>
public sealed record DamagePopup(float X, float Y, int Damage, bool IsMine, double StartSeconds)
{
    /// <summary>화면에 남아 있는 시간(초).</summary>
    public const double DurationSeconds = 0.9;

    public double EndSeconds => StartSeconds + DurationSeconds;

    public float Progress(double nowSeconds) =>
        (float)Math.Clamp((nowSeconds - StartSeconds) / DurationSeconds, 0.0, 1.0);
}
