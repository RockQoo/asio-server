namespace VisualClient.Protocol;

/// <summary>
/// <c>Z2CTaskResult</c>에 실려 오는 태스크의 카테고리. C++ <c>Protocol::ETaskCategory</c>
/// (<c>Shared/Protocol/Src/TaskKind.h</c>)와 같은 값이다.
/// </summary>
public enum TaskCategory : byte
{
    None = 0,
    Mail = 1,
}

/// <summary>
/// Mail 카테고리의 세부 동작. C++ <c>Protocol::EMailTask</c>와 같은 값이다.
/// </summary>
public enum MailTask : byte
{
    Added = 1,
    Removed = 2,
}

/// <summary>
/// taskKind(uint16) 인코딩: 상위 8비트가 카테고리, 하위 8비트가 그 안의 세부 동작.
///
/// <para>
/// 서버(Zone)가 자기 메모리를 바꾼 내용을 그대로 이 태스크 목록으로 내려보내고, 클라이언트는
/// 같은 목록을 자기 메모리에 적용해 동기화한다 — 응답 구조체를 콘텐츠마다 새로 만들지 않는
/// 대신 이 2단 분기(<c>switch(카테고리)</c> → <c>switch(세부동작)</c>)를 쓰는 구조다.
/// 서버 쪽 롤백(<c>Zone::ZoneUnitOfWork::OnRollback</c>)도 같은 모양의 분기를 쓴다.
/// </para>
/// </summary>
public static class TaskKind
{
    public static TaskCategory CategoryOf(ushort taskKind) => (TaskCategory)(byte)(taskKind >> 8);

    public static byte SubTaskOf(ushort taskKind) => (byte)(taskKind & 0xFF);
}
