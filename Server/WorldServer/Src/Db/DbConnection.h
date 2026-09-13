#pragma once

#include "Db/DbCommand.h"

namespace World
{
    // ODBC 수준의 실패를 알리는 예외(연결 끊김, 문법 오류, 제약 위반 등).
    //
    // SP 자신이 코드로 알리는 실패는 아래 DbProcedureException 쪽이다. 둘을 나눈 이유는
    // **복구 방법이 다르기 때문**이다 -- ODBC 실패는 재연결로 살아날 수 있지만, SP가
    // 돌려준 에러 코드는 다시 시도해도 같은 결과다.
    //
    // 에러 종류는 문자열이 아니라 **SQLSTATE**(ODBC 표준 5자 코드)로 식별한다 -- 메시지 문자열은
    // 드라이버/로캘에 따라 달라져서 파싱 대상이 못 된다(cpp-patterns.md "에러는 문자열이 아니라
    // 에러 코드로" 절의 취지).
    class DbException : public std::runtime_error
    {
    public:
        DbException(std::string sqlState, const std::string& message)
            : std::runtime_error(message)
            , sqlState_(std::move(sqlState))
        {
        }

        [[nodiscard]] const std::string& SqlState() const noexcept { return sqlState_; }

    private:
        std::string sqlState_;
    };

    // SP가 RETURN으로 돌려준 0이 아닌 코드.
    //
    // 코드 대역은 .claude/rules/sql-patterns.md 에 있다 -- 상위 바이트가 분류(진입 가드 /
    // 예외 / 콘텐츠별 업무 거절)를 가른다. 여기서 코드 의미를 해석하지 않는 이유는 Core가
    // 콘텐츠를 모르는 것과 같다: 해석은 그 코드를 정한 콘텐츠 쪽 일이다.
    //
    // **예외로 만든 이유**: 던져야 Execute의 트랜잭션 경로가 롤백을 태운다. 반환값으로
    // 돌려주면 호출부가 검사를 빠뜨렸을 때 실패한 작업이 그대로 커밋된다.
    class DbProcedureException : public std::runtime_error
    {
    public:
        DbProcedureException(std::string procedure, const int32_t returnValue)
            : std::runtime_error("SP가 에러 코드를 반환했다(" + procedure + ")")
            , procedure_(std::move(procedure))
            , returnValue_(returnValue)
        {
        }

        [[nodiscard]] const std::string& Procedure() const noexcept { return procedure_; }
        [[nodiscard]] int32_t ReturnValue() const noexcept { return returnValue_; }

    private:
        std::string procedure_;
        int32_t returnValue_;
    };

    // ODBC 커넥션 하나. **DB 레인 스레드 하나가 이걸 하나씩 독점한다**(DbConnectionPool 참고).
    //
    // **왜 커넥션 풀이 아니라 스레드당 하나인가**: DB 큐 그룹의 소비자 수와 커넥션 수를 1:1로
    // 맞추는 것이 원칙이다 -- 스레드가 커넥션보다 많으면 커넥션을 기다리며 노는 스레드가 생기고,
    // 적으면 커넥션이 논다. 그래서 "빌리고 반납하는" 풀 자체가 필요 없고, 그 덕에 이 클래스에
    // 락이 하나도 없다.
    //
    // **왜 ODBC인가**: Windows SDK에 들어 있어 vcpkg 같은 패키지 관리자를 도입하지 않아도 되고
    // (`3rd/`에 벤더링할 것도 없다), 링크는 odbc32.lib 하나면 된다.
    class DbConnection
    {
    public:
        explicit DbConnection(std::string connectionString);
        ~DbConnection();

        DbConnection(const DbConnection&) = delete;
        DbConnection& operator=(const DbConnection&) = delete;

        // 커맨드 목록을 순서대로 실행한다.
        //
        // useTransaction이면 전체를 **하나의 트랜잭션**으로 묶는다 -- UnitOfWork 하나가 곧
        // 트랜잭션 하나라는 경계가 여기서 구현된다. 중간에 실패하면 앞의 것까지 전부 롤백된다.
        //
        // 결과 집합이 있으면 outResult에 담는다(nullptr이면 버린다). **SP 하나가 SELECT를
        // 여러 번 하면 집합도 여러 개**이고, 커맨드가 여러 개면 그것들이 낸 집합이 실행 순서대로
        // 이어 붙는다. 호출부는 DbCommand.h의 SetAt/FirstRow로 꺼낸다.
        //
        // 실패 시 DbException을 던진다. 커넥션이 끊긴 상태였다면 먼저 재연결을 시도한다.
        void Execute(const std::vector<DbCommand>& commands, const bool useTransaction,
                     DbResult* outResult);

        // 같은 SP를 int64 파라미터 하나씩 바꿔가며 **한 번의 왕복으로 여러 번** 실행한다
        // (ODBC 파라미터 배열). 수백만 건을 넣어야 하는 검증 도구용 경로다 -- 한 건씩 보내면
        // 네트워크 왕복이 건수만큼 생겨서 측정하려는 대상(삽입 성능)이 왕복 지연에 묻힌다.
        //
        // 테이블에 직접 INSERT하지 않고 SP를 그대로 쓴다(.claude/rules/sql-patterns.md).
        // 중복 키가 있으면 그 배치가 통째로 실패하는데, 검증 도구 입장에서는 그게 곧 결과다.
        void ExecuteMany(const std::string& procedure, const std::span<const int64_t> values);

        [[nodiscard]] bool IsOpen() const noexcept { return connection_ != nullptr; }

    private:
        void Connect();
        void Disconnect() noexcept;

        // 커맨드 하나를 실행하고, 결과 집합이 있으면 읽어 반환한다.
        //
        // isTransOutside는 SP의 @is_trans_outside로 그대로 넘어간다 -- 이 커넥션이 이미
        // 트랜잭션 안이면(useTransaction 경로) SP가 자기 트랜잭션을 열지 않아야 한다.
        void ExecuteOne(const DbCommand& command, const bool isTransOutside, DbResult* outResult);

        void SetAutoCommit(const bool enabled);

        const std::string connectionString_;

        // SQLHENV / SQLHDBC. 헤더에 <sql.h>를 끌어들이지 않으려고 void*로 들고 있다 --
        // Windows 헤더가 헤더 체인 전체로 퍼지면 다른 번역 단위의 컴파일 시간이 늘고
        // 매크로 충돌 위험이 생긴다(다른 .cpp들이 windows.h를 각자 가드해서 넣는 것과 같은 이유).
        void* environment_{nullptr};
        void* connection_{nullptr};
    };

    // DB 레인 스레드마다 커넥션 하나씩을 쥐여주는 곳.
    //
    // **왜 thread_local인가**: DB 큐 그룹은 ownerId 어피니티로 스레드를 고정하므로, "이 작업이
    // 어느 스레드에서 도는가"가 곧 "어느 커넥션을 쓰는가"다. 그래서 빌리고 반납하는 풀 대신
    // 스레드에 커넥션을 붙여두면 되고, 그 덕에 이 클래스에도 락이 없다.
    //
    // **주의**: thread_local 저장소는 인스턴스가 아니라 함수에 붙는다. 즉 이 클래스를 두 개
    // 만들면 같은 커넥션을 공유해버린다 -- App이 하나만 소유한다는 전제에 기대고 있다.
    class DbConnectionPool
    {
    public:
        explicit DbConnectionPool(std::string connectionString);

        // 이 스레드 전용 커넥션. 처음 호출될 때 만들어지고, 이후로는 같은 것을 돌려준다.
        // 실제 연결은 첫 Execute에서 일어난다(생성 시점에 DB가 안 떠 있어도 기동은 되게).
        [[nodiscard]] DbConnection& ForCurrentThread();

    private:
        const std::string connectionString_;
    };
}
