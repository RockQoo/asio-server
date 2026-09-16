#include "pch.h"
#include "Cli/DbCheck.h"

#include "Db/DbConnection.h"
#include "Db/PasswordHash.h"


// `WorldServer.exe --dbcheck` 로 실행하면 서버를 띄우지 않고 DB 연결만 확인하고 끝낸다.
//
// **왜 필요한가**: DB가 붙는 경로는 로그인 -> World 캐시 -> UnitOfWork 반영으로 이어져서,
// 뭔가 안 되면 "ODBC가 문제인지 / 드라이버 이름이 틀렸는지 / 비밀번호 해시 형식이 다른지"를
// 서버 로그에서 가려내기 어렵다. 그 세 가지만 따로 떼어 확인한다.
int32_t RunDbCheck(const std::string& connectionString)
{
    std::cout << "[dbcheck] 연결 문자열: " << connectionString << "\n";

    try
    {
        DbConnection dbConnection(connectionString);

        DbResult dbResult;
        dbConnection.Execute({DbCommand{"dbo.usp_players_select", {std::string("tester1")}}},
                           false, &dbResult);

        const auto* const row = FirstRow(dbResult);
        if (row == nullptr)
        {
            std::cout << "[dbcheck] FAIL: tester1 계정이 없습니다. bat\\setup_game_db.bat 을 먼저 실행하세요.\n";
            return EXIT_FAILURE;
        }

        const auto playerId = GetInt64(*row, 0);
        const auto storedHash = GetString(*row, 2);
        if (!playerId || !storedHash)
        {
            std::cout << "[dbcheck] FAIL: usp_players_select의 결과 컬럼 형태가 예상과 다릅니다.\n";
            return EXIT_FAILURE;
        }

        std::cout << "[dbcheck] 조회 OK  playerId=" << *playerId << "\n";

        // 시드의 해시는 PowerShell(.NET Rfc2898DeriveBytes)로 만들었다. 여기서 통과한다는 건
        // CNG 구현이 .NET과 같은 값을 낸다는 뜻이라, 두 구현의 교차 검증이기도 하다.
        const bool correct = VerifyPassword("0000", *storedHash);
        const bool wrong = VerifyPassword("wrong-password", *storedHash);

        std::cout << "[dbcheck] 비밀번호 정답 검증: " << (correct ? "PASS" : "FAIL") << "\n";
        std::cout << "[dbcheck] 오답 거부 검증:   " << (!wrong ? "PASS" : "FAIL") << "\n";

        return (correct && !wrong) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    catch (const DbException& ex)
    {
        std::cout << "[dbcheck] FAIL: " << ex.what() << " (SQLSTATE=" << ex.SqlState() << ")\n";
        return EXIT_FAILURE;
    }
}
