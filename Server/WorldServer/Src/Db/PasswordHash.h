#pragma once

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

    // 자동 가입(계정이 없으면 그 자리에서 만든다)이 DB에 넣을 해시 문자열을 만든다.
    // 형식은 위와 같은 "반복횟수.솔트(base64).해시(base64)"라 VerifyPassword가 그대로 읽는다.
    //
    // 솔트는 호출마다 새 난수다 -- 시드 계정(Sql/seed.sql)이 솔트를 고정한 것은 T-SQL로 2만
    // 개를 찍어내야 해서 어쩔 수 없었던 것이고, 여기는 그 제약이 없다.
    //
    // 실패(CNG 호출 실패)하면 빈 문자열을 준다. 호출부는 **빈 값을 DB에 넣지 말고** 로그인
    // 실패로 끊어야 한다 -- 빈 해시가 저장되면 그 계정은 영원히 로그인할 수 없게 된다.
    [[nodiscard]] std::string HashPassword(const std::string& password);
}
