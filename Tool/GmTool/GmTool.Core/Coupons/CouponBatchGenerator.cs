using System.Diagnostics;

namespace GmTool.Core.Coupons;

public sealed record CouponBatchRequest
{
    /// <summary>전역 유일성이 이미 확보된 5자리 캠페인 코드(네임스페이스).</summary>
    public required string CampaignCode { get; init; }

    public required long RequestedCount { get; init; }

    /// <summary>
    /// 청크 하나의 크기(= DB 벌크 적재 한 번의 건수 = 스풀 fsync 주기).
    ///
    /// 너무 크면 트랜잭션 로그가 커지고 실패 시 전부 롤백되며, 너무 작으면 왕복과 fsync 횟수가
    /// 늘어난다. 1만 건 안팎이 무난한 지점이다.
    /// </summary>
    public int ChunkSize { get; init; } = 10_000;

    public required string SpoolDirectory { get; init; }

    /// <summary>스풀 파일 이름에 붙일 태그(보통 배치 id나 타임스탬프).</summary>
    public required string BatchTag { get; init; }
}

public sealed record CouponBatchResult
{
    public required long GeneratedCount { get; init; }

    /// <summary>
    /// 생성 중 로컬 메모리 중복 검사에 걸려 버린 횟수. 32^18 공간에서 수백만 건을 뽑으므로
    /// 정상적으로는 0에 가깝다 — 이 값이 크다면 난수원이나 자릿수 설계를 의심해야 한다.
    /// </summary>
    public required long DuplicateRejectedCount { get; init; }

    public required long ChunkCount { get; init; }
    public required string SpoolPath { get; init; }
    public required long ElapsedMilliseconds { get; init; }
}

public sealed record CouponBatchProgress
{
    public required long GeneratedCount { get; init; }
    public required long RequestedCount { get; init; }
    public required long ChunkSeq { get; init; }
    public required long ElapsedMilliseconds { get; init; }
}

/// <summary>
/// 대량 쿠폰 발급의 본체. 생성과 중복 검사를 이 프로세스 메모리에서 끝내고 DB는 청크 단위
/// 벌크 적재만 받는다 — 전략과 근거는 <c>Tool/GmTool/README.md</c>의 "쿠폰" 절 참고.
///
/// <b>스레드 세이프하지 않다.</b> 한 배치를 한 흐름에서만 돌린다. 캠페인이 곧 작업 단위이므로
/// 캠페인마다 인스턴스를 따로 쓰면 여러 운영자의 발급이 서로 간섭하지 않는다.
/// </summary>
public sealed class CouponBatchGenerator
{
    /// <summary>
    /// 청크 하나를 받아 처리하는 콜백(보통 DB 벌크 적재 + World 전송).
    /// 여기서 예외가 나면 발급 전체가 중단된다 — 스풀 파일과 오프셋이 남으므로 어디까지
    /// 적재됐는지는 확인할 수 있다.
    /// </summary>
    public delegate Task ChunkHandler(IReadOnlyList<string> chunk, long chunkSeq, CancellationToken cancellationToken);

    public async Task<CouponBatchResult> GenerateAsync(
        CouponBatchRequest request,
        ChunkHandler chunkHandler,
        IProgress<CouponBatchProgress>? progress = null,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(chunkHandler);

        if (!CouponCodeFormat.IsValidCampaignCode(request.CampaignCode))
        {
            throw new ArgumentException($"캠페인 코드 형식이 올바르지 않습니다: '{request.CampaignCode}'", nameof(request));
        }

        if (request.RequestedCount <= 0)
        {
            throw new ArgumentException("발급 개수는 1 이상이어야 합니다.", nameof(request));
        }

        if (request.ChunkSize <= 0)
        {
            throw new ArgumentException("청크 크기는 1 이상이어야 합니다.", nameof(request));
        }

        var stopwatch = Stopwatch.StartNew();

        // 이 집합이 곧 "중복 검사 범위"다 -- 캠페인 하나의 유일성만 보장하면 되기 때문.
        // 용량을 미리 잡아 리해싱을 없앤다 -- 100만 건에서 리해싱은 눈에 보이는 비용이다.
        var capacity = (int)Math.Min(request.RequestedCount, int.MaxValue / 2);
        var uniqueCodes = new HashSet<string>(capacity, StringComparer.Ordinal);

        var chunkBuffer = new List<string>(request.ChunkSize);
        long duplicateRejected = 0;
        long chunkSeq = 0;
        long dbCommittedCount = 0;

        await using var spool = CouponSpoolWriter.Create(
            request.SpoolDirectory, request.CampaignCode, request.BatchTag);

        while (uniqueCodes.Count < request.RequestedCount)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var code = CouponCodeGenerator.Generate(request.CampaignCode);
            if (!uniqueCodes.Add(code))
            {
                ++duplicateRejected;
                continue;
            }

            // 순서가 중요하다: 스풀에 먼저 쓰고(유실 대비), 그 다음 청크 버퍼에 담고,
            // 청크가 차면 DB로 넘긴 뒤에 fsync + 오프셋 기록으로 확정한다.
            spool.Append(code);
            chunkBuffer.Add(code);

            if (chunkBuffer.Count < request.ChunkSize)
            {
                continue;
            }

            ++chunkSeq;
            await chunkHandler(chunkBuffer, chunkSeq, cancellationToken).ConfigureAwait(false);
            dbCommittedCount += chunkBuffer.Count;
            await spool.CommitToDisk(dbCommittedCount, cancellationToken).ConfigureAwait(false);
            chunkBuffer.Clear();

            progress?.Report(new CouponBatchProgress
            {
                GeneratedCount = uniqueCodes.Count,
                RequestedCount = request.RequestedCount,
                ChunkSeq = chunkSeq,
                ElapsedMilliseconds = stopwatch.ElapsedMilliseconds,
            });
        }

        if (chunkBuffer.Count > 0)
        {
            ++chunkSeq;
            await chunkHandler(chunkBuffer, chunkSeq, cancellationToken).ConfigureAwait(false);
            dbCommittedCount += chunkBuffer.Count;
            await spool.CommitToDisk(dbCommittedCount, cancellationToken).ConfigureAwait(false);
            chunkBuffer.Clear();

            progress?.Report(new CouponBatchProgress
            {
                GeneratedCount = uniqueCodes.Count,
                RequestedCount = request.RequestedCount,
                ChunkSeq = chunkSeq,
                ElapsedMilliseconds = stopwatch.ElapsedMilliseconds,
            });
        }

        stopwatch.Stop();

        return new CouponBatchResult
        {
            GeneratedCount = uniqueCodes.Count,
            DuplicateRejectedCount = duplicateRejected,
            ChunkCount = chunkSeq,
            SpoolPath = spool.SpoolPath,
            ElapsedMilliseconds = stopwatch.ElapsedMilliseconds,
        };
    }
}
