using System.Data;

namespace GmTool.Web.Data;

/// <summary>
/// 문자열 리스트 하나를 "한 컬럼 테이블"로 보여주는 최소 <see cref="IDataReader"/>.
/// <c>SqlBulkCopy.WriteToServerAsync</c>에 넘길 소스로만 쓴다.
///
/// <see cref="DataTable"/>을 쓰지 않는 이유: 1만 행 청크마다 <c>DataRow</c> 1만 개와 박싱이
/// 생긴다. 이 리더는 리스트를 인덱스로 훑기만 해서 청크당 추가 할당이 사실상 없다.
///
/// 구현하지 않은 멤버는 예외를 던진다 — 기본값을 돌려주면 잘못된 데이터가 조용히 적재된다.
/// </summary>
internal sealed class StringColumnDataReader : IDataReader
{
    private readonly IReadOnlyList<string> values_;
    private readonly string columnName_;
    private int index_ = -1;

    public StringColumnDataReader(IReadOnlyList<string> values, string columnName)
    {
        values_ = values;
        columnName_ = columnName;
    }

    public int FieldCount => 1;

    public bool Read()
    {
        // 마지막 행을 넘긴 뒤에도 계속 증가시키면 오버플로 위험이 있어 경계에서 멈춘다.
        if (index_ + 1 >= values_.Count)
        {
            index_ = values_.Count;
            return false;
        }

        ++index_;
        return true;
    }

    public object GetValue(int i) => values_[index_];

    public string GetName(int i) => columnName_;

    public int GetOrdinal(string name) =>
        string.Equals(name, columnName_, StringComparison.OrdinalIgnoreCase)
            ? 0
            : throw new IndexOutOfRangeException(name);

    public Type GetFieldType(int i) => typeof(string);

    public string GetDataTypeName(int i) => "char";

    public bool IsDBNull(int i) => false;

    public string GetString(int i) => values_[index_];

    public object this[int i] => GetValue(i);

    public object this[string name] => GetValue(GetOrdinal(name));

    public bool IsClosed => index_ >= values_.Count;

    public int Depth => 0;

    // 벌크 적재 소스에는 "영향받은 행 수"라는 개념이 없다. SqlBulkCopy도 이 값을 쓰지 않는다.
    public int RecordsAffected => -1;

    public void Close() => index_ = values_.Count;

    public void Dispose() => Close();

    public bool NextResult() => false;

    // 아래는 이 리더의 사용 범위(문자열 한 컬럼)를 벗어난 접근이다.

    public DataTable GetSchemaTable() => throw new NotSupportedException();

    public bool GetBoolean(int i) => throw new NotSupportedException();

    public byte GetByte(int i) => throw new NotSupportedException();

    public long GetBytes(int i, long fieldOffset, byte[]? buffer, int bufferOffset, int length) =>
        throw new NotSupportedException();

    public char GetChar(int i) => throw new NotSupportedException();

    public long GetChars(int i, long fieldOffset, char[]? buffer, int bufferOffset, int length) =>
        throw new NotSupportedException();

    public IDataReader GetData(int i) => throw new NotSupportedException();

    public DateTime GetDateTime(int i) => throw new NotSupportedException();

    public decimal GetDecimal(int i) => throw new NotSupportedException();

    public double GetDouble(int i) => throw new NotSupportedException();

    public float GetFloat(int i) => throw new NotSupportedException();

    public Guid GetGuid(int i) => throw new NotSupportedException();

    public short GetInt16(int i) => throw new NotSupportedException();

    public int GetInt32(int i) => throw new NotSupportedException();

    public long GetInt64(int i) => throw new NotSupportedException();

    public int GetValues(object[] values) => throw new NotSupportedException();
}
