namespace VisualClient.Protocol;

/// <summary>
/// 존 하나가 담당하는 사각형. 존 격자를 화면에 그리고, 어느 좌표가 어느 존인지 표시하는 데 쓴다.
///
/// <para>
/// <b>이 값은 서버가 알려주는 게 아니라 클라이언트가 같은 규칙을 복제한 것이다.</b>
/// <c>Z2CEnterZoneNotify</c>의 페이로드는 playerId + zoneId뿐이어서 경계 좌표가 오지 않는다.
/// 서버는 <c>Server/ZoneServer/Src/main.cpp</c>의 <c>ParseZoneList</c>에서 존을 <c>kZonesPerRow</c>
/// 개씩 줄바꿈하는 격자로 배치하고, 여기서 그 규칙을 그대로 재현한다.
/// <b>한쪽만 고치면 화면의 존 경계와 서버의 실제 핸드오프 지점이 어긋난다</b> — 서버의
/// <c>kZoneSize</c>/<c>kZonesPerRow</c>/<c>kZoneRows</c>를 고쳤으면 여기도 같이 고칠 것.
/// </para>
///
/// <para>
/// 현재 배치(존 4개, 한 행에 2개):
/// <code>
///   y:[10,20)   존 1   존 2      ← ZoneServer.exe 1,2
///   y:[0,10)    존 3   존 4      ← ZoneServer.exe 3,4
///               x:[0,10)  x:[10,20)
/// </code>
/// </para>
/// </summary>
public static class ZoneLayout
{
    /// <summary>존 하나의 한 변 길이. 서버 <c>kZoneSize</c>와 같아야 한다.</summary>
    public const float ZoneSize = 10.0f;

    /// <summary>한 행에 놓이는 존 개수. 서버 <c>kZonesPerRow</c>와 같아야 한다.</summary>
    public const int ZonesPerRow = 2;

    /// <summary>격자의 행 개수. 서버 <c>kZoneRows</c>와 같아야 한다.</summary>
    public const int ZoneRows = 2;

    /// <summary>
    /// 첫 zoneId. <b>존 id는 1부터 시작하고 0은 "존 없음/미배정" 예약값이다</b> — 서버
    /// <c>ParseZoneList</c>가 0을 거부하고, <see cref="Model.WorldModel.MyZoneId"/>도 입장 전
    /// 0을 들고 있다. 0을 유효한 존으로 쓰면 "0번 존에 있다"와 "아직 아무 존에도 없다"가 같은
    /// 값이 되어 구분할 수 없다.
    /// </summary>
    public const uint FirstZoneId = 1;

    /// <summary>
    /// 화면에 그리는 존 개수. <c>bat/start_server_all.bat</c>이 <c>ZoneServer.exe 1,2</c>와
    /// <c>ZoneServer.exe 3,4</c> <b>두 프로세스</b>로 존 4개를 띄우는 것과 맞췄다.
    ///
    /// <para>
    /// 프로세스 경계와 격자 경계가 다르다는 점이 중요하다. 존 1,2는 한 프로세스, 3,4는 다른
    /// 프로세스라 <b>가로 이동(1↔2, 3↔4)은 같은 프로세스 안의 BASIC 스레드 간 이동</b>이고,
    /// <b>세로 이동(1↔3, 2↔4)이 프로세스(TCP 링크)를 넘는 핸드오프</b>다. 화면에서는 구분되지
    /// 않지만 World가 다른 링크로 라우팅하므로 경로가 다르다 — 핸드오프를 확인할 때는 세로로
    /// 넘어보는 것이 의미가 있다.
    /// </para>
    /// </summary>
    public const int ZoneCount = ZonesPerRow * ZoneRows;

    /// <summary>마지막 zoneId(포함).</summary>
    public const uint LastZoneId = FirstZoneId + ZoneCount - 1;

    /// <summary>월드 전체 x 범위의 끝.</summary>
    public const float WorldMaxX = ZoneSize * ZonesPerRow;

    /// <summary>월드 전체 y 범위의 끝. 서버도 이제 y 경계를 검사한다(존이 격자가 되었으므로).</summary>
    public const float WorldMaxY = ZoneSize * ZoneRows;

    /// <summary>
    /// 좌표가 속한 zoneId. 서버가 핸드오프 대상을 담당 사각형으로 정하는 것과 같은 계산이다.
    /// 격자 밖 좌표는 가장자리 존으로 잘라낸다(월드 밖으로 나가지 못하게 클라이언트가 막으므로
    /// 실제로는 잘릴 일이 없지만, 화면 표시용 계산이라 항상 유효한 값을 돌려준다).
    /// </summary>
    public static uint ZoneIdOf(float x, float y)
    {
        var column = Math.Clamp((int)(x / ZoneSize), 0, ZonesPerRow - 1);
        // 행 0이 위쪽이라 y가 큰 쪽이 행 번호가 작다.
        var row = Math.Clamp(ZoneRows - 1 - (int)(y / ZoneSize), 0, ZoneRows - 1);
        return FirstZoneId + (uint)((row * ZonesPerRow) + column);
    }

    /// <summary>이 존이 격자에서 몇 번째 열/행인가(행 0이 위쪽).</summary>
    public static (int Column, int Row) CellOf(uint zoneId)
    {
        var index = (int)(zoneId - FirstZoneId);
        return (index % ZonesPerRow, index / ZonesPerRow);
    }

    public static float MinXOf(uint zoneId) => CellOf(zoneId).Column * ZoneSize;

    public static float MaxXOf(uint zoneId) => MinXOf(zoneId) + ZoneSize;

    public static float MaxYOf(uint zoneId) => (ZoneRows - CellOf(zoneId).Row) * ZoneSize;

    public static float MinYOf(uint zoneId) => MaxYOf(zoneId) - ZoneSize;
}
