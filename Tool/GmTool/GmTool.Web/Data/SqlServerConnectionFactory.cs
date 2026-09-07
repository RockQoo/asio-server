using GmTool.Web.Options;
using Microsoft.Data.SqlClient;
using Microsoft.Extensions.Options;
using SqlKata.Compilers;
using SqlKata.Execution;

namespace GmTool.Web.Data;

/// <summary>
/// SQL Server 연결과 SqlKata <see cref="QueryFactory"/>를 만들어주는 팩토리.
///
/// <see cref="QueryFactory"/>를 <b>싱글턴으로 등록하지 않는</b> 이유: 안에 든
/// <see cref="SqlConnection"/>이 스레드 세이프하지 않은데 Blazor Server는 요청/회로마다 별개
/// 흐름이 동시에 돈다. 공유하면 "connection is already in use"가 산발적으로 난다. 매번 새로
/// 만들어도 SqlClient 커넥션 풀이 재사용하므로 TCP 핸드셰이크가 매번 일어나지는 않는다.
/// </summary>
public sealed class SqlServerConnectionFactory
{
    private readonly string connectionString_;

    // UseLegacyPagination=false: OFFSET/FETCH를 쓴다. true면 ROW_NUMBER() 서브쿼리로 감싸는
    // 구식 페이징이 나온다. 컴파일러는 상태가 없어 인스턴스 공유가 안전하다.
    private readonly SqlServerCompiler compiler_ = new() { UseLegacyPagination = false };

    public SqlServerConnectionFactory(IOptions<DatabaseOptions> options)
    {
        connectionString_ = options.Value.ConnectionString;
        if (string.IsNullOrWhiteSpace(connectionString_))
        {
            throw new InvalidOperationException(
                "Database:ConnectionString이 비어 있습니다. appsettings.json 또는 환경 변수를 확인하세요.");
        }
    }

    /// <summary>열린 상태의 새 연결을 만든다. 호출자가 dispose 책임을 진다.</summary>
    public async Task<SqlConnection> OpenConnectionAsync(CancellationToken cancellationToken = default)
    {
        var connection = new SqlConnection(connectionString_);
        await connection.OpenAsync(cancellationToken).ConfigureAwait(false);
        return connection;
    }

    /// <summary>
    /// 열린 연결 위에 SqlKata 쿼리 팩토리를 얹는다. <see cref="QueryFactory"/>를 dispose하면
    /// 그 연결도 함께 닫힌다.
    /// </summary>
    public async Task<QueryFactory> CreateQueryFactoryAsync(CancellationToken cancellationToken = default)
    {
        var connection = await OpenConnectionAsync(cancellationToken).ConfigureAwait(false);
        return new QueryFactory(connection, compiler_);
    }
}
