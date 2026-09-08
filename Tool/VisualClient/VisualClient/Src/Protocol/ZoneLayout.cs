namespace VisualClient.Protocol;

/// <summary>
/// 존 하나가 담당하는 x 구간. 존 경계를 화면에 그리고, 어느 좌표가 어느 존인지 표시하는 데 쓴다.
///
/// <para>
/// <b>이 값은 서버가 알려주는 게 아니라 클라이언트가 같은 규칙을 복제한 것이다.</b>
/// <c>Z2CEnterZoneNotify</c>의 페이로드는 playerId + zoneId뿐이어서 경계 좌표가 오지 않는다.
/// 서버는 <c>Server/ZoneServer/Src/main.cpp</c>의 <c>ParseZoneList</c>에서 "각 존은 10칸 폭으로
/// 나란히 붙어 있다"(zoneId 0 → x:[0,10), 1 → x:[10,20), ...)고 정하고 있고, 여기서 그 규칙을
/// 그대로 재현한다. <b>한쪽만 고치면 화면의 존 경계와 서버의 실제 핸드오프 지점이 어긋난다</b>
/// — 서버 쪽을 고쳤으면 <see cref="ZoneWidth"/>도 같이 고칠 것.
/// </para>
/// </summary>
public static class ZoneLayout
{
    /// <summary>존 하나의 x 폭. 서버 <c>ParseZoneList</c>의 <c>10.0f</c>와 같아야 한다.</summary>
    public const float ZoneWidth = 10.0f;

    /// <summary>
    /// 화면에 그리는 존 개수. <c>bat/start_server_all.bat</c>이 <c>ZoneServer.exe 0,1</c>로
    /// 존 2개를 한 프로세스에 띄우는 것과 맞췄다. 더 늘리면 그 존은 서버에 없어서 그 구간으로
    /// 이동한 순간 핸드오프 요청이 라우팅 대상을 못 찾는다.
    /// </summary>
    public const int ZoneCount = 2;

    /// <summary>월드 전체 x 범위의 끝(존 개수 × 폭).</summary>
    public const float WorldMaxX = ZoneWidth * ZoneCount;

    /// <summary>
    /// y는 서버가 경계 검사를 전혀 하지 않는다(<c>ZoneInstance::HandleMove</c>는 x만 본다).
    /// 화면 밖으로 나가지 않게 클라이언트가 자체적으로 잡아두는 값일 뿐이다.
    /// </summary>
    public const float WorldMaxY = 10.0f;

    /// <summary>x 좌표가 속한 zoneId. 서버가 핸드오프 대상을 x로 정하는 것과 같은 계산이다.</summary>
    public static uint ZoneIdOf(float x)
    {
        var index = (int)(x / ZoneWidth);
        return (uint)Math.Clamp(index, 0, ZoneCount - 1);
    }

    public static float MinXOf(uint zoneId) => zoneId * ZoneWidth;

    public static float MaxXOf(uint zoneId) => (zoneId + 1) * ZoneWidth;
}
