using System.Text;

namespace GmTool.Core.Coupons;

/// <summary>
/// 발급 중인 쿠폰 번호를 DB 적재 <b>전에</b> 로컬 디스크에 먼저 append 하는 스풀 파일. DB의
/// WAL과 같은 발상으로, 중간에 죽어도 어디까지 만들었는지가 남는다.
/// </summary>
public sealed class CouponSpoolWriter : IAsyncDisposable
{
    private readonly FileStream stream_;
    private readonly StreamWriter writer_;
    private readonly string offsetPath_;

    private CouponSpoolWriter(FileStream stream, StreamWriter writer, string spoolPath, string offsetPath)
    {
        stream_ = stream;
        writer_ = writer;
        SpoolPath = spoolPath;
        offsetPath_ = offsetPath;
    }

    public string SpoolPath { get; }

    public long WrittenCount { get; private set; }

    /// <summary>
    /// 캠페인별 스풀 파일을 새로 만든다(같은 이름이 있으면 덮어쓴다).
    /// 파일 이름에 캠페인 코드를 넣어 여러 운영자의 동시 발급이 서로 겹치지 않게 한다.
    /// </summary>
    public static CouponSpoolWriter Create(string spoolDirectory, string campaignCode, string batchTag)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(spoolDirectory);
        ArgumentException.ThrowIfNullOrWhiteSpace(campaignCode);

        Directory.CreateDirectory(spoolDirectory);

        var fileName = $"coupon_{campaignCode}_{batchTag}.spool";
        var spoolPath = Path.Combine(spoolDirectory, fileName);
        var offsetPath = spoolPath + ".offset";

        var stream = new FileStream(spoolPath, new FileStreamOptions
        {
            Mode = FileMode.Create,
            Access = FileAccess.Write,
            Share = FileShare.Read,
            Options = FileOptions.SequentialScan,
            BufferSize = 1 << 16,
        });

        // UTF8Encoding(false): BOM을 넣지 않는다. 이 파일은 그대로 제휴사에 넘기는 경우가
        // 있어서, BOM이 붙으면 첫 줄 첫 쿠폰 코드 앞에 보이지 않는 3바이트가 끼어든다.
        var writer = new StreamWriter(stream, new UTF8Encoding(false), 1 << 16, leaveOpen: true)
        {
            AutoFlush = false,
        };

        return new CouponSpoolWriter(stream, writer, spoolPath, offsetPath);
    }

    /// <summary>한 줄 추가. 물리 디스크 기록은 아직 확정되지 않는다.</summary>
    public void Append(string normalizedCode)
    {
        writer_.Write(normalizedCode);
        writer_.Write('\n');
        ++WrittenCount;
    }

    /// <summary>
    /// 지금까지 쓴 내용을 물리 디스크까지 확정하고, 확정된 줄 수를 오프셋 파일에 기록한다.
    /// 청크가 DB에 성공적으로 들어간 직후에 호출한다 — 그래야 오프셋 파일의 숫자가
    /// "여기까지는 DB에도 들어갔다"는 의미를 갖고 재개 지점으로 쓸 수 있다.
    /// </summary>
    public async Task CommitToDisk(long dbCommittedCount, CancellationToken cancellationToken = default)
    {
        await writer_.FlushAsync(cancellationToken).ConfigureAwait(false);

        // flushToDisk: true 가 실제 fsync다. 이 인자를 빼면 OS 캐시까지만 내려가고,
        // 정전 시 데이터가 사라질 수 있다.
        stream_.Flush(flushToDisk: true);

        await File.WriteAllTextAsync(
            offsetPath_,
            $"{WrittenCount}\n{dbCommittedCount}\n",
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask DisposeAsync()
    {
        await writer_.FlushAsync().ConfigureAwait(false);
        await writer_.DisposeAsync().ConfigureAwait(false);
        await stream_.DisposeAsync().ConfigureAwait(false);
    }
}
