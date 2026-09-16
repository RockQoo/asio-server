#pragma once

// SP 파라미터 / 조회 결과 값 하나.
//
// **문자열을 UTF-8 std::string으로 들고 다니는 이유**: 서버 전체가 UTF-8이고, SQL Server의
// NVARCHAR는 UTF-16이다. 여기서 UTF-16으로 들고 있으면 콘텐츠 코드가 전부 wstring을 만져야
// 하므로, 변환은 바인딩 직전 한 곳(DbConnection)에서만 한다.
//
// uint8_t가 따로 있는 건 TINYINT(재화 종류) 때문이다 -- int64_t로 보내면 드라이버가
// 변환하긴 하지만, 컬럼 타입과 바인딩 타입을 맞춰두는 편이 실수를 줄인다.
using DbValue = std::variant<int64_t, uint8_t, std::string>;

// SP 호출 하나. `dbo.usp_mails_upsert` 처럼 스키마까지 적는다.
//
// 서버는 테이블에 직접 쿼리하지 않고 전부 SP를 거친다(.claude/rules/sql-patterns.md).
// 그래서 이 구조체에 임의 SQL 문자열을 넣는 통로를 두지 않는다.
struct DbCommand
{
    std::string procedure;
    std::vector<DbValue> params;
};

// 조회 결과. 컬럼 순서는 SP의 SELECT 절 순서 그대로다.
using DbRow = std::vector<DbValue>;

// 결과 집합 하나 = 행 목록. SELECT 한 번의 결과다.
using DbResultSet = std::vector<DbRow>;

// **SP 하나가 SELECT를 여러 번 하면 결과 집합도 여러 개다.** usp_players_load가 우편과
// 재화를 한 번의 왕복으로 가져오는 것이 그 경우라, 결과를 집합 단위로 담는다.
// Execute에 커맨드를 여러 개 넘기면 그것들이 낸 집합이 **실행 순서대로 이어 붙는다.**
using DbResult = std::vector<DbResultSet>;

// 결과 집합 하나를 꺼낸다. 없으면 비어 있는 것을 돌려주므로 호출부가 인덱스 검사를
// 반복하지 않아도 된다 -- SP를 고쳐 집합이 줄어들면 "빈 결과"로 흐르지 크래시하지 않는다.
[[nodiscard]] inline const DbResultSet& SetAt(const DbResult& dbResult, const size_t index)
{
    static const DbResultSet kEmpty;
    return index < dbResult.size() ? dbResult[index] : kEmpty;
}

// 결과 집합의 첫 행. 한 행만 나오는 조회(계정 조회 등)를 위한 편의 함수.
[[nodiscard]] inline const DbRow* FirstRow(const DbResult& dbResult, const size_t index = 0)
{
    const auto& set = SetAt(dbResult, index);
    return set.empty() ? nullptr : &set.front();
}

// 결과 읽기 헬퍼. 컬럼 개수/타입이 어긋나면 nullopt를 주고, 호출부가 그걸 "이 빌드가 아는
// SP 형태가 아니다"로 처리한다 -- 인덱스로 직접 꺼내면 SP를 고쳤을 때 조용히 깨진다.
[[nodiscard]] inline std::optional<int64_t> GetInt64(const DbRow& row, const size_t index)
{
    if (index >= row.size())
    {
        return std::nullopt;
    }

    if (const auto* value = std::get_if<int64_t>(&row[index]))
    {
        return *value;
    }

    if (const auto* value = std::get_if<uint8_t>(&row[index]))
    {
        return static_cast<int64_t>(*value);
    }

    return std::nullopt;
}

[[nodiscard]] inline std::optional<std::string> GetString(const DbRow& row, const size_t index)
{
    if (index >= row.size())
    {
        return std::nullopt;
    }

    if (const auto* value = std::get_if<std::string>(&row[index]))
    {
        return *value;
    }

    return std::nullopt;
}
