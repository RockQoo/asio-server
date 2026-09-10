#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace World
{
    // SP 파라미터 / 조회 결과 값 하나.
    //
    // **문자열을 UTF-8 std::string으로 들고 다니는 이유**: 서버 전체가 UTF-8이고, SQL Server의
    // NVARCHAR는 UTF-16이다. 여기서 UTF-16으로 들고 있으면 콘텐츠 코드가 전부 wstring을 만져야
    // 하므로, 변환은 바인딩 직전 한 곳(DbConnection)에서만 한다.
    //
    // uint8_t가 따로 있는 건 TINYINT(재화 종류) 때문이다 -- int64_t로 보내면 드라이버가
    // 변환하긴 하지만, 컬럼 타입과 바인딩 타입을 맞춰두는 편이 실수를 줄인다.
    using DbValue = std::variant<int64_t, uint8_t, std::string>;

    // SP 호출 하나. `dbo.mail_insert` 처럼 스키마까지 적는다.
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
    using DbResult = std::vector<DbRow>;

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
}
