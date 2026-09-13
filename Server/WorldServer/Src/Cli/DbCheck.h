#pragma once

namespace World
{
    // `WorldServer.exe --dbcheck` 진입점. 서버를 띄우지 않고 DB 연결만 확인하고 끝낸다.
    //
    // **왜 필요한가**: DB가 붙는 경로는 로그인 -> World 캐시 -> UnitOfWork 반영으로 이어져서,
    // 뭔가 안 되면 "ODBC가 문제인지 / 드라이버 이름이 틀렸는지 / 비밀번호 해시 형식이 다른지"를
    // 서버 로그에서 가려내기 어렵다. 그 세 가지만 따로 떼어 확인한다.
    [[nodiscard]] int32_t RunDbCheck(const std::string& connectionString);
}
