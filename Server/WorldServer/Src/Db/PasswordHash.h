#pragma once

#include <string>

namespace World
{
    // 비밀번호 검증. DB의 players.password_hash와 클라이언트가 보낸 평문을 비교한다.
    //
    // 저장 형식은 "반복횟수.솔트(base64).해시(base64)" 한 문자열이다(Sql/players.sql).
    // **반복 횟수를 값 안에 같이 넣어두는 이유**: 나중에 횟수를 올려도 기존 계정을 로그인
    // 시점에 하나씩 재해싱할 수 있다(전체 마이그레이션 없이). GmTool의 운영자 계정도 같은 형식이다.
    //
    // 알고리즘은 PBKDF2-HMAC-SHA256이고 Windows CNG(bcrypt.lib)를 쓴다 -- 외부 암호 라이브러리를
    // `3rd/`에 벤더링하지 않기 위해서다. .NET의 Rfc2898DeriveBytes와 같은 값을 낸다(시드 스크립트가
    // PowerShell로 만든 해시를 이 코드가 검증할 수 있어야 한다).
    //
    // **전송은 평문이다.** 프레임에 암호화 플래그가 없어서, 실서비스라면 TLS가 전제다.
    // 지금은 범위 밖이라는 것을 알고 두는 것이다.
    [[nodiscard]] bool VerifyPassword(const std::string& password, const std::string& storedHash);
}
