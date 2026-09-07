using GmTool.Core.Coupons;

namespace GmTool.Tests;

/// <summary>
/// 대량 발급 흐름 검증: 정확히 요청한 개수만큼 유일한 코드가 나오는지, 청크가 제대로 잘려
/// 넘어가는지, 스풀 파일과 오프셋 파일이 남는지.
///
/// 스풀/오프셋을 테스트하는 이유는 이게 유실 대비 장치인데 실패해도 조용하기 때문이다 —
/// 평소 흐름에서는 아무 증상이 없고, 정작 필요한 순간(중간에 죽었을 때)에야 없다는 걸 안다.
/// </summary>
public class CouponBatchGeneratorTests : IDisposable
{
    private readonly string spoolDirectory_;

    public CouponBatchGeneratorTests()
    {
        spoolDirectory_ = Path.Combine(Path.GetTempPath(), "gmtool-tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(spoolDirectory_);
    }

    public void Dispose()
    {
        try
        {
            Directory.Delete(spoolDirectory_, recursive: true);
        }
        catch (IOException)
        {
            // 테스트 정리 실패는 무시한다(임시 폴더).
        }

        GC.SuppressFinalize(this);
    }

    private CouponBatchRequest Request(long count, int chunkSize) => new()
    {
        CampaignCode = "X7Q4M",
        RequestedCount = count,
        ChunkSize = chunkSize,
        SpoolDirectory = spoolDirectory_,
        BatchTag = "test",
    };

    [Fact]
    public async Task 요청한_개수만큼_정확히_생성한다()
    {
        var generator = new CouponBatchGenerator();
        var received = new List<string>();

        var result = await generator.GenerateAsync(
            Request(2500, 1000),
            (chunk, _, _) =>
            {
                received.AddRange(chunk);
                return Task.CompletedTask;
            });

        Assert.Equal(2500, result.GeneratedCount);
        Assert.Equal(2500, received.Count);
        Assert.Equal(2500, received.Distinct(StringComparer.Ordinal).Count());
    }

    [Fact]
    public async Task 청크가_설정한_크기로_잘려_넘어가고_마지막_잔여도_처리된다()
    {
        var generator = new CouponBatchGenerator();
        var chunkSizes = new List<int>();

        var result = await generator.GenerateAsync(
            Request(2500, 1000),
            (chunk, _, _) =>
            {
                chunkSizes.Add(chunk.Count);
                return Task.CompletedTask;
            });

        // 1000 + 1000 + 500(잔여)
        Assert.Equal(new[] { 1000, 1000, 500 }, chunkSizes);
        Assert.Equal(3, result.ChunkCount);
    }

    [Fact]
    public async Task 청크_번호는_1부터_순서대로_올라간다()
    {
        var generator = new CouponBatchGenerator();
        var seqs = new List<long>();

        await generator.GenerateAsync(
            Request(300, 100),
            (_, seq, _) =>
            {
                seqs.Add(seq);
                return Task.CompletedTask;
            });

        Assert.Equal(new long[] { 1, 2, 3 }, seqs);
    }

    [Fact]
    public async Task 모든_코드가_캠페인_네임스페이스와_체크_문자를_만족한다()
    {
        var generator = new CouponBatchGenerator();
        var all = new List<string>();

        await generator.GenerateAsync(
            Request(1000, 250),
            (chunk, _, _) =>
            {
                all.AddRange(chunk);
                return Task.CompletedTask;
            });

        Assert.All(all, code =>
        {
            Assert.Equal(CouponCodeFormat.TotalLength, code.Length);
            Assert.Equal("X7Q4M", CouponCodeFormat.ExtractCampaignCode(code));
            Assert.True(CouponCheckDigit.Verify(code));
        });
    }

    [Fact]
    public async Task 스풀_파일에_생성한_전부가_기록되고_오프셋_파일이_남는다()
    {
        var generator = new CouponBatchGenerator();

        var result = await generator.GenerateAsync(
            Request(1500, 500),
            (_, _, _) => Task.CompletedTask);

        Assert.True(File.Exists(result.SpoolPath), $"스풀 파일이 없다: {result.SpoolPath}");

        var lines = await File.ReadAllLinesAsync(result.SpoolPath);
        var codes = lines.Where(l => !string.IsNullOrWhiteSpace(l)).ToList();
        Assert.Equal(1500, codes.Count);
        Assert.Equal(1500, codes.Distinct(StringComparer.Ordinal).Count());

        var offsetPath = result.SpoolPath + ".offset";
        Assert.True(File.Exists(offsetPath), "오프셋 파일이 없다 — 재개 지점을 알 수 없게 된다");

        // 오프셋 파일에는 [스풀에 쓴 줄 수] / [DB까지 확정된 건수] 두 줄이 들어간다.
        var offsetLines = await File.ReadAllLinesAsync(offsetPath);
        Assert.Equal("1500", offsetLines[0]);
        Assert.Equal("1500", offsetLines[1]);
    }

    [Fact]
    public async Task 청크_처리에서_예외가_나면_발급이_중단되고_스풀은_남는다()
    {
        var generator = new CouponBatchGenerator();

        var failure = await Assert.ThrowsAsync<InvalidOperationException>(() =>
            generator.GenerateAsync(
                Request(1000, 100),
                (_, seq, _) => seq == 3
                    ? throw new InvalidOperationException("DB 적재 실패 시뮬레이션")
                    : Task.CompletedTask));

        Assert.Equal("DB 적재 실패 시뮬레이션", failure.Message);

        // 실패한 청크 직전까지의 오프셋이 남아 있어야 재개 지점을 알 수 있다.
        var spoolFiles = Directory.GetFiles(spoolDirectory_, "*.offset");
        Assert.Single(spoolFiles);

        var offsetLines = await File.ReadAllLinesAsync(spoolFiles[0]);
        Assert.Equal("200", offsetLines[1]);  // 청크 1, 2 = 200건까지 DB 확정
    }

    [Fact]
    public async Task 진행_보고가_청크마다_올라온다()
    {
        var generator = new CouponBatchGenerator();
        var reports = new List<CouponBatchProgress>();
        var progress = new Progress<CouponBatchProgress>(reports.Add);

        await generator.GenerateAsync(
            Request(500, 100),
            (_, _, _) => Task.CompletedTask,
            progress);

        // Progress<T>는 비동기로 콜백을 던지므로 잠깐 기다려야 전부 도착한다.
        for (var i = 0; i < 50 && reports.Count < 5; ++i)
        {
            await Task.Delay(20);
        }

        Assert.NotEmpty(reports);
        Assert.Equal(500, reports[^1].GeneratedCount);
    }

    [Fact]
    public async Task 취소하면_OperationCanceledException이_난다()
    {
        var generator = new CouponBatchGenerator();
        using var cts = new CancellationTokenSource();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
            generator.GenerateAsync(
                Request(100_000, 1000),
                (_, seq, _) =>
                {
                    if (seq == 2)
                    {
                        cts.Cancel();
                    }

                    return Task.CompletedTask;
                },
                progress: null,
                cts.Token));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(-1)]
    public async Task 발급_개수가_0_이하면_예외(long count)
    {
        var generator = new CouponBatchGenerator();
        await Assert.ThrowsAsync<ArgumentException>(() =>
            generator.GenerateAsync(Request(count, 100), (_, _, _) => Task.CompletedTask));
    }

    [Fact]
    public async Task 캠페인_코드_형식이_틀리면_예외()
    {
        var generator = new CouponBatchGenerator();
        var request = Request(10, 10) with { CampaignCode = "BAD" };

        await Assert.ThrowsAsync<ArgumentException>(() =>
            generator.GenerateAsync(request, (_, _, _) => Task.CompletedTask));
    }
}
