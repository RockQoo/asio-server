#pragma once

#include <cstdint>
#include <string>

#include "Shared/Common/Src/Ids.h"

namespace Common
{
    // 우편 한 통. **World와 Zone이 같은 타입을 쓴다.**
    //
    // 예전에는 World가 `World::MailInfo`, Zone이 `Mail::Info`로 따로 선언하고 "필드는 같지만
    // 하는 일이 달라 일부러 나눴다"는 주석을 달아뒀었다. 그런데 이 값은 DB에서 읽어 World가
    // 캐시하고, W2ZEnterZone에 실려 Zone으로 가고, UnitOfWork 스트림으로 다시 World에 돌아온다
    // -- **세 경로가 같은 필드를 같은 순서로 읽고 쓴다.** 선언이 둘이면 한쪽에 필드를 추가했을
    // 때 다른 쪽이 조용히 어긋나고, 그게 와이어에서 드러난다.
    //
    // 나눠야 할 것은 **데이터가 아니라 행위**다. Zone의 Mail::Model(추가/삭제/만료/롤백)과
    // World의 캐시는 여전히 별개 클래스이고, 공유하는 것은 이 구조체 하나뿐이다.
    struct MailInfo
    {
        // **전역 유일한 RUID다.** 예전에는 우편함마다 1부터 세는 지역 카운터였는데, 그러면
        // 플레이어끼리 같은 값이 나오고 프로세스를 넘는 핸드오프에서 1부터 다시 시작한다 --
        // DB의 mail_id가 단독 PK(Sql/mails.sql)라 그대로는 쓸 수 없었다.
        MailId mailId{};
        std::string title;
        std::string body;
        int64_t sendUt{};
        int64_t endUt{};

        // 기본 생성 = "값 없음". Task의 Prev 자리에 "이전 상태가 없었다"를 넣을 때 쓴다
        // (우편 추가의 Prev는 빈 MailInfo다).
        [[nodiscard]] bool IsValid() const noexcept { return mailId.IsValid(); }
    };
}
