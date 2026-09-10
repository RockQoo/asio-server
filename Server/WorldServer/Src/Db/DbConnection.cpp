#include "Server/WorldServer/Src/pch.h"

#include "Server/WorldServer/Src/Db/DbConnection.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sql.h>
#include <sqlext.h>

namespace World
{
    namespace
    {
        // UTF-8 <-> UTF-16 변환. NVARCHAR 컬럼에 한글이 들어가므로 ODBC의 W 계열 API만 쓴다
        // (A 계열은 프로세스 ANSI 코드페이지를 타서 CP949 환경에서 한글이 깨진다).
        [[nodiscard]] std::wstring Utf8ToWide(const std::string& text)
        {
            if (text.empty())
            {
                return {};
            }

            const int32_t length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                                       static_cast<int>(text.size()), nullptr, 0);
            if (length <= 0)
            {
                return {};
            }

            std::wstring result(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                result.data(), length);
            return result;
        }

        [[nodiscard]] std::string WideToUtf8(const wchar_t* text, const size_t textLength)
        {
            if (text == nullptr || textLength == 0)
            {
                return {};
            }

            const int32_t length = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(textLength),
                                                       nullptr, 0, nullptr, nullptr);
            if (length <= 0)
            {
                return {};
            }

            std::string result(static_cast<size_t>(length), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(textLength),
                                result.data(), length, nullptr, nullptr);
            return result;
        }

        // 드라이버가 남긴 진단 레코드에서 SQLSTATE와 메시지를 꺼낸다. 여러 개면 첫 번째만 쓴다 --
        // 뒤따르는 것들은 대개 같은 원인의 부가 설명이라 로그를 길게만 만든다.
        [[nodiscard]] DbException MakeException(const int16_t handleType, void* handle,
                                                const std::string& context)
        {
            SQLWCHAR stateBuffer[6]{};
            SQLWCHAR messageBuffer[1024]{};
            SQLINTEGER nativeError = 0;
            SQLSMALLINT messageLength = 0;

            const SQLRETURN ret = SQLGetDiagRecW(handleType, handle, 1, stateBuffer, &nativeError,
                                                 messageBuffer, 1024, &messageLength);
            if (!SQL_SUCCEEDED(ret))
            {
                return DbException("HY000", context + ": 진단 레코드를 읽을 수 없음");
            }

            const auto state = WideToUtf8(reinterpret_cast<const wchar_t*>(stateBuffer), 5);
            const auto message = WideToUtf8(reinterpret_cast<const wchar_t*>(messageBuffer),
                                            static_cast<size_t>(messageLength));
            return DbException(state, context + ": " + message + " (native=" +
                               std::to_string(nativeError) + ")");
        }

        void ThrowIfFailed(const SQLRETURN ret, const int16_t handleType, void* handle,
                           const std::string& context)
        {
            if (!SQL_SUCCEEDED(ret))
            {
                throw MakeException(handleType, handle, context);
            }
        }

        // "{CALL dbo.mail_insert(?,?,?)}" 를 만든다. ODBC 표준 escape 문법이라 드라이버가
        // 자기 방언(T-SQL의 EXEC)으로 바꿔준다.
        [[nodiscard]] std::wstring MakeCallStatement(const DbCommand& command)
        {
            std::string statement = "{CALL " + command.procedure + "(";
            for (size_t index = 0; index < command.params.size(); ++index)
            {
                statement += (index == 0) ? "?" : ",?";
            }
            statement += ")}";
            return Utf8ToWide(statement);
        }
    }

    DbConnection::DbConnection(std::string connectionString)
        : connectionString_(std::move(connectionString))
    {
    }

    DbConnection::~DbConnection()
    {
        Disconnect();
    }

    void DbConnection::Connect()
    {
        Disconnect();

        SQLHENV environment = nullptr;
        ThrowIfFailed(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &environment),
                      SQL_HANDLE_ENV, nullptr, "ODBC 환경 핸들 할당 실패");

        // ODBC3를 선언하지 않으면 드라이버가 2.x 호환 모드로 동작해 W 계열 API 일부가 막힌다.
        ThrowIfFailed(SQLSetEnvAttr(environment, SQL_ATTR_ODBC_VERSION,
                                    reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0),
                      SQL_HANDLE_ENV, environment, "ODBC 버전 설정 실패");

        SQLHDBC connection = nullptr;
        ThrowIfFailed(SQLAllocHandle(SQL_HANDLE_DBC, environment, &connection),
                      SQL_HANDLE_ENV, environment, "ODBC 커넥션 핸들 할당 실패");

        auto wideConnectionString = Utf8ToWide(connectionString_);
        const SQLRETURN ret = SQLDriverConnectW(connection, nullptr,
                                                reinterpret_cast<SQLWCHAR*>(wideConnectionString.data()),
                                                SQL_NTS, nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT);
        if (!SQL_SUCCEEDED(ret))
        {
            auto failure = MakeException(SQL_HANDLE_DBC, connection, "DB 연결 실패");
            SQLFreeHandle(SQL_HANDLE_DBC, connection);
            SQLFreeHandle(SQL_HANDLE_ENV, environment);
            throw failure;
        }

        environment_ = environment;
        connection_ = connection;
    }

    void DbConnection::Disconnect() noexcept
    {
        if (connection_ != nullptr)
        {
            SQLDisconnect(connection_);
            SQLFreeHandle(SQL_HANDLE_DBC, connection_);
            connection_ = nullptr;
        }

        if (environment_ != nullptr)
        {
            SQLFreeHandle(SQL_HANDLE_ENV, environment_);
            environment_ = nullptr;
        }
    }

    void DbConnection::SetAutoCommit(const bool enabled)
    {
        const auto value = enabled ? SQL_AUTOCOMMIT_ON : SQL_AUTOCOMMIT_OFF;
        ThrowIfFailed(SQLSetConnectAttr(connection_, SQL_ATTR_AUTOCOMMIT,
                                        reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(value)), 0),
                      SQL_HANDLE_DBC, connection_, "자동 커밋 설정 실패");
    }

    void DbConnection::Execute(const std::vector<DbCommand>& commands, const bool useTransaction,
                               DbResult* outResult)
    {
        if (commands.empty())
        {
            return;
        }

        // 커넥션이 없거나 이전 작업에서 끊긴 상태면 여기서 다시 연결한다. 재연결 전용 스레드를
        // 두지 않는 이유는 이 레인이 어차피 자기 커넥션만 쓰기 때문이다 -- 다음 작업이 오면
        // 그 작업이 재연결하고, 실패하면 그 작업만 버려진다(Fire-and-Forget).
        if (connection_ == nullptr)
        {
            Connect();
        }

        if (!useTransaction)
        {
            for (const auto& command : commands)
            {
                ExecuteOne(command, outResult);
            }
            return;
        }

        SetAutoCommit(false);
        try
        {
            for (const auto& command : commands)
            {
                ExecuteOne(command, outResult);
            }

            ThrowIfFailed(SQLEndTran(SQL_HANDLE_DBC, connection_, SQL_COMMIT),
                          SQL_HANDLE_DBC, connection_, "커밋 실패");
        }
        catch (...)
        {
            // 롤백 자체가 실패하는 상황(커넥션이 이미 끊김 등)에서는 할 수 있는 일이 없다.
            // 여기서 새 예외를 던지면 원래 실패 원인이 가려지므로 삼킨다.
            SQLEndTran(SQL_HANDLE_DBC, connection_, SQL_ROLLBACK);
            SetAutoCommit(true);
            throw;
        }

        SetAutoCommit(true);
    }

    void DbConnection::ExecuteOne(const DbCommand& command, DbResult* outResult)
    {
        SQLHSTMT statement = nullptr;
        ThrowIfFailed(SQLAllocHandle(SQL_HANDLE_STMT, connection_, &statement),
                      SQL_HANDLE_DBC, connection_, "구문 핸들 할당 실패");

        // 핸들은 예외 경로에서도 반드시 해제돼야 한다. 짧은 지역 가드로 처리한다.
        struct StatementGuard
        {
            SQLHSTMT handle;
            ~StatementGuard() { SQLFreeHandle(SQL_HANDLE_STMT, handle); }
        } guard{statement};

        // 바인딩은 실행 시점까지 **버퍼 주소가 살아 있어야** 한다. reserve로 재할당을 막아
        // push_back 후에도 앞서 바인딩한 주소가 유효하게 유지한다(vector가 커지면 주소가 바뀐다).
        const size_t paramCount = command.params.size();
        std::vector<SQLBIGINT> intBuffers;
        std::vector<SQLCHAR> tinyBuffers;
        std::vector<std::wstring> textBuffers;
        std::vector<SQLLEN> indicators(paramCount, 0);
        intBuffers.reserve(paramCount);
        tinyBuffers.reserve(paramCount);
        textBuffers.reserve(paramCount);

        for (size_t index = 0; index < paramCount; ++index)
        {
            const auto position = static_cast<SQLUSMALLINT>(index + 1);
            const auto& param = command.params[index];

            if (const auto* intValue = std::get_if<int64_t>(&param))
            {
                intBuffers.push_back(static_cast<SQLBIGINT>(*intValue));
                indicators[index] = 0;
                ThrowIfFailed(SQLBindParameter(statement, position, SQL_PARAM_INPUT,
                                               SQL_C_SBIGINT, SQL_BIGINT, 0, 0,
                                               &intBuffers.back(), 0, &indicators[index]),
                              SQL_HANDLE_STMT, statement, "BIGINT 파라미터 바인딩 실패");
            }
            else if (const auto* tinyValue = std::get_if<uint8_t>(&param))
            {
                tinyBuffers.push_back(static_cast<SQLCHAR>(*tinyValue));
                indicators[index] = 0;
                ThrowIfFailed(SQLBindParameter(statement, position, SQL_PARAM_INPUT,
                                               SQL_C_UTINYINT, SQL_TINYINT, 0, 0,
                                               &tinyBuffers.back(), 0, &indicators[index]),
                              SQL_HANDLE_STMT, statement, "TINYINT 파라미터 바인딩 실패");
            }
            else
            {
                textBuffers.push_back(Utf8ToWide(std::get<std::string>(param)));
                auto& text = textBuffers.back();
                indicators[index] = SQL_NTS;
                ThrowIfFailed(SQLBindParameter(statement, position, SQL_PARAM_INPUT,
                                               SQL_C_WCHAR, SQL_WVARCHAR,
                                               text.empty() ? 1 : text.size(), 0,
                                               text.data(),
                                               static_cast<SQLLEN>((text.size() + 1) * sizeof(wchar_t)),
                                               &indicators[index]),
                              SQL_HANDLE_STMT, statement, "NVARCHAR 파라미터 바인딩 실패");
            }
        }

        auto callStatement = MakeCallStatement(command);
        const SQLRETURN executeResult = SQLExecDirectW(
            statement, reinterpret_cast<SQLWCHAR*>(callStatement.data()), SQL_NTS);

        // SQL_NO_DATA는 "영향받은 행이 0"이라는 뜻이라 실패가 아니다(이미 지워진 우편을 다시
        // 지우는 경우 등). 그 판정은 콘텐츠가 아니라 여기서 흡수한다.
        if (executeResult != SQL_NO_DATA)
        {
            ThrowIfFailed(executeResult, SQL_HANDLE_STMT, statement,
                          "SP 실행 실패(" + command.procedure + ")");
        }

        if (outResult == nullptr)
        {
            return;
        }

        SQLSMALLINT columnCount = 0;
        ThrowIfFailed(SQLNumResultCols(statement, &columnCount),
                      SQL_HANDLE_STMT, statement, "결과 컬럼 수 조회 실패");
        if (columnCount <= 0)
        {
            return;
        }

        // 컬럼 타입을 미리 한 번만 확인한다. 행마다 확인하면 조회 1건에 컬럼 수만큼 왕복이 늘어난다.
        std::vector<bool> isTextColumn(static_cast<size_t>(columnCount), false);
        for (SQLSMALLINT column = 1; column <= columnCount; ++column)
        {
            SQLSMALLINT dataType = 0;
            ThrowIfFailed(SQLDescribeColW(statement, column, nullptr, 0, nullptr,
                                          &dataType, nullptr, nullptr, nullptr),
                          SQL_HANDLE_STMT, statement, "결과 컬럼 정보 조회 실패");

            isTextColumn[static_cast<size_t>(column - 1)] =
                (dataType != SQL_BIGINT && dataType != SQL_INTEGER &&
                 dataType != SQL_SMALLINT && dataType != SQL_TINYINT);
        }

        DbResult rows;
        while (SQLFetch(statement) != SQL_NO_DATA)
        {
            DbRow row;
            row.reserve(static_cast<size_t>(columnCount));

            for (SQLSMALLINT column = 1; column <= columnCount; ++column)
            {
                SQLLEN indicator = 0;
                if (isTextColumn[static_cast<size_t>(column - 1)])
                {
                    wchar_t buffer[1024]{};
                    ThrowIfFailed(SQLGetData(statement, column, SQL_C_WCHAR, buffer,
                                             sizeof(buffer), &indicator),
                                  SQL_HANDLE_STMT, statement, "문자열 컬럼 읽기 실패");
                    row.push_back(indicator == SQL_NULL_DATA
                                      ? std::string{}
                                      : WideToUtf8(buffer, std::wcslen(buffer)));
                }
                else
                {
                    SQLBIGINT value = 0;
                    ThrowIfFailed(SQLGetData(statement, column, SQL_C_SBIGINT, &value,
                                             sizeof(value), &indicator),
                                  SQL_HANDLE_STMT, statement, "정수 컬럼 읽기 실패");
                    row.push_back(indicator == SQL_NULL_DATA
                                      ? int64_t{0}
                                      : static_cast<int64_t>(value));
                }
            }

            rows.push_back(std::move(row));
        }

        *outResult = std::move(rows);
    }

    DbConnectionPool::DbConnectionPool(std::string connectionString)
        : connectionString_(std::move(connectionString))
    {
    }

    DbConnection& DbConnectionPool::ForCurrentThread()
    {
        // 스레드가 끝날 때 소멸자가 돌아 커넥션이 닫힌다. ProcessorGroup::Stop()이 워커
        // 스레드를 조인한 뒤에 앱이 내려가므로, 커넥션이 앱보다 오래 살아남는 경우는 없다.
        thread_local std::unique_ptr<DbConnection> connection;
        if (connection == nullptr)
        {
            connection = std::make_unique<DbConnection>(connectionString_);
        }
        return *connection;
    }
}
