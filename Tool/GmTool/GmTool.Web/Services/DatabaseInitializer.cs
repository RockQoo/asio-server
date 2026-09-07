using GmTool.Web.Data;
using GmTool.Web.Options;
using GmTool.Web.Repositories;
using Microsoft.Extensions.Options;

namespace GmTool.Web.Services;

/// <summary>
/// 기동 시 스키마를 적용하고 초기 운영자 계정을 만든다.
///
/// 마이그레이션 도구 대신 <c>Sql/schema.sql</c>을 그대로 실행하는 이유: 스키마가
/// OBJECT_ID 검사로 감싸져 있어 여러 번 실행해도 결과가 같고(멱등),
/// 운영툴은 스키마가 자주 바뀌는 성격이 아니다. 컬럼 변경이 잦아지면 그때 마이그레이션
/// 도구를 붙이는 게 맞다.
///
/// 시드 계정은 <b>계정이 하나도 없을 때만</b> 만든다. 매번 만들거나 비밀번호를 되돌리면
/// 운영 중에 바꾼 비밀번호가 재시작 때마다 초기화되는, 조용하고 위험한 동작이 된다.
/// </summary>
public sealed class DatabaseInitializer
{
    private readonly SqlServerConnectionFactory factory_;
    private readonly OperatorRepository operators_;
    private readonly DatabaseOptions databaseOptions_;
    private readonly AuthOptions authOptions_;
    private readonly ILogger<DatabaseInitializer> logger_;
    private readonly IHostEnvironment environment_;

    public DatabaseInitializer(
        SqlServerConnectionFactory factory,
        OperatorRepository operators,
        IOptions<DatabaseOptions> databaseOptions,
        IOptions<AuthOptions> authOptions,
        IHostEnvironment environment,
        ILogger<DatabaseInitializer> logger)
    {
        factory_ = factory;
        operators_ = operators;
        databaseOptions_ = databaseOptions.Value;
        authOptions_ = authOptions.Value;
        environment_ = environment;
        logger_ = logger;
    }

    public async Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        if (!databaseOptions_.ApplySchemaOnStartup)
        {
            logger_.LogInformation("Database:ApplySchemaOnStartup=false — 스키마 적용을 건너뜁니다.");
            return;
        }

        await ApplySchemaAsync(cancellationToken).ConfigureAwait(false);
        await SeedOperatorAsync(cancellationToken).ConfigureAwait(false);
    }

    private async Task ApplySchemaAsync(CancellationToken cancellationToken)
    {
        var schemaPath = Path.Combine(environment_.ContentRootPath, "..", "Sql", "schema.sql");
        schemaPath = Path.GetFullPath(schemaPath);

        if (!File.Exists(schemaPath))
        {
            logger_.LogWarning("스키마 파일을 찾지 못했습니다: {Path} — 스키마 적용을 건너뜁니다.", schemaPath);
            return;
        }

        var sql = await File.ReadAllTextAsync(schemaPath, cancellationToken).ConfigureAwait(false);

        await using var connection = await factory_.OpenConnectionAsync(cancellationToken).ConfigureAwait(false);

        // GO 단위로 쪼개서 하나씩 실행한다. GO는 T-SQL 문법이 아니라 sqlcmd/SSMS가 쓰는
        // 배치 구분자이므로 SqlClient에 그대로 넘기면 "Incorrect syntax near 'GO'"가 난다.
        // 그렇다고 세미콜론으로 쪼갤 수도 없다 -- CREATE TABLE 안에도 세미콜론이 나온다.
        var batchCount = 0;
        foreach (var batch in SplitBatches(sql))
        {
            await using var command = connection.CreateCommand();
            command.CommandText = batch;
            await command.ExecuteNonQueryAsync(cancellationToken).ConfigureAwait(false);
            ++batchCount;
        }

        logger_.LogInformation("스키마 적용 완료: {Path} (배치 {BatchCount}개)", schemaPath, batchCount);
    }

    /// <summary>
    /// 스키마 파일을 <c>GO</c> 배치 단위로 자른다. 줄 전체가 <c>GO</c>인 경우만 구분자로
    /// 보므로, 문자열이나 식별자 안에 들어 있는 "GO"는 건드리지 않는다.
    /// </summary>
    private static IEnumerable<string> SplitBatches(string sql)
    {
        var current = new List<string>();

        foreach (var line in sql.Split('\n'))
        {
            if (string.Equals(line.Trim(), "GO", StringComparison.OrdinalIgnoreCase))
            {
                var batch = string.Join('\n', current).Trim();
                if (batch.Length > 0)
                {
                    yield return batch;
                }

                current.Clear();
                continue;
            }

            current.Add(line);
        }

        // 파일이 GO로 끝나지 않아도 마지막 배치를 잃지 않게 한다.
        var tail = string.Join('\n', current).Trim();
        if (tail.Length > 0)
        {
            yield return tail;
        }
    }

    private async Task SeedOperatorAsync(CancellationToken cancellationToken)
    {
        var count = await operators_.CountAsync(cancellationToken).ConfigureAwait(false);
        if (count > 0)
        {
            return;
        }

        var hash = PasswordHasher.Hash(authOptions_.SeedPassword);
        var id = await operators_.CreateAsync(
            authOptions_.SeedLoginId, authOptions_.SeedDisplayName, hash, "Admin", cancellationToken)
            .ConfigureAwait(false);

        logger_.LogWarning(
            "운영자 계정이 없어 초기 계정을 생성했습니다. id={OperatorId}, 로그인={LoginId} — 첫 로그인 후 비밀번호를 변경하세요.",
            id, authOptions_.SeedLoginId);
    }
}
