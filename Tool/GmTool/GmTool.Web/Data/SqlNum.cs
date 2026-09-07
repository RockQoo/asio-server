namespace GmTool.Web.Data;

/// <summary>
/// 부호 없는 정수를 SQL Server 파라미터로 넘길 수 있는 타입으로 바꾼다.
///
/// <c>Microsoft.Data.SqlClient</c>는 <c>ushort</c>/<c>uint</c>/<c>ulong</c>을 파라미터 값으로
/// <b>거부한다</b>(T-SQL에 부호 없는 정수 타입이 없어 대응 <c>SqlDbType</c>이 없다).
/// 모델은 C++ 와이어 프로토콜과 맞추려고 부호 없는 타입을 유지하고, 변환은 저장 계층 경계인
/// 여기서만 한다.
/// </summary>
internal static class SqlNum
{
    /// <summary>
    /// <c>checked</c>인 이유: 컬럼이 <c>BIGINT</c>(부호 있음)라 조용히 음수로 접히면 읽을 때
    /// 터지거나 다른 행과 충돌한다. 넣는 시점에 실패하는 쪽이 낫다.
    /// </summary>
    public static long Of(ulong value) => checked((long)value);

    public static long Of(uint value) => value;

    public static int Of(ushort value) => value;
}
