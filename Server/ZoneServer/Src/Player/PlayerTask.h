#pragma once

#include "Shared/Common/Src/Enum.h"
#include "Shared/Common/Src/MailInfo.h"
#include "Shared/Common/Src/TaskKind.h"

#include "Shared/Core/Src/Task/ITask.h"
#include "Shared/Core/Src/Task/Paired.h"

// 플레이어의 모델들이 남기는 변경 기록. **콘텐츠마다 파일을 나누지 않고 여기 모은다** --
// 전부 데이터뿐이라 .cpp 가 없고, ZoneUnitOfWork 가 이 목록 전체를 한 switch 로 읽는다.
// 새 콘텐츠(아이템 등)가 붙으면 여기에 클래스가 하나 는다.
//
// 공통 규약: 태스크는 **데이터만** 갖는다. 직렬화도 롤백도 ZoneUnitOfWork 가 Kind() 로
// 분기해서 하고, 태스크 자신은 자기가 무슨 뜻인지 모른다(ITask 주석 참고).

// 우편 추가 기록.
//
// 우편은 통째로 들고 있는다. 삭제를 되돌릴 때 제목/본문/기간까지 그대로 복원해야 하고,
// 클라이언트도 같은 값을 받아 자기 우편함에 적용하기 때문이다.
//
// **추가의 Prev 는 빈 MailInfo 다** -- "그 우편이 없었다"가 직전 상태다.
class AddMailTask final : public Task::ITask
{
public:
    void Set(Common::MailInfo newInfo, Common::MailInfo prevInfo)
    {
        info_.Set(std::move(newInfo), std::move(prevInfo));
    }

    [[nodiscard]] uint16_t Kind() const noexcept override
    {
        return static_cast<uint16_t>(Common::ETaskType::MailAdd);
    }

    [[nodiscard]] const Task::Paired<Common::MailInfo>& Info() const noexcept { return info_; }

private:
    Task::Paired<Common::MailInfo> info_;
};

// 우편 삭제 기록. **삭제의 New 가 빈 MailInfo 이고 Prev 가 지워진 원본이다** -- 되돌리기는
// Prev 를 그대로 다시 넣는 것이고, mailId 까지 같아야 하므로 원본 전체가 필요하다.
class RemoveMailTask final : public Task::ITask
{
public:
    void Set(Common::MailInfo newInfo, Common::MailInfo prevInfo)
    {
        info_.Set(std::move(newInfo), std::move(prevInfo));
    }

    [[nodiscard]] uint16_t Kind() const noexcept override
    {
        return static_cast<uint16_t>(Common::ETaskType::MailDel);
    }

    [[nodiscard]] const Task::Paired<Common::MailInfo>& Info() const noexcept { return info_; }

private:
    Task::Paired<Common::MailInfo> info_;
};

// 재화 변경 기록 하나.
//
// **새 값과 이전 값을 둘 다** 싣는다:
//   - 클라이언트는 New 로 덮어쓴다. 증감량을 누적하는 방식이 아니라서 통지 하나가 유실돼도
//     다음 값에서 자동으로 맞춰진다.
//   - 롤백은 Prev 를 그대로 되돌린다. "차감의 반대인 증가"를 부르는 역연산 방식은 요청한
//     양과 실제 바뀐 양이 다를 때 틀리는데(상한/하한에 걸린 경우), 이전 값을 들고 있으면
//     그런 경우가 없다.
// 합쳐서 16바이트라 둘 다 싣는 게 싸다.
//
// **재화 종류는 짝이 아니다** -- 바뀌는 것은 값이고 종류는 "어느 값인가"를 가리키는
// 식별자라, New/Prev 로 나눌 대상이 아니다.
class CurrencyTask final : public Task::ITask
{
public:
    void Set(const int64_t newValue, const int64_t prevValue, const Common::ECurrencyType type)
    {
        value_.Set(newValue, prevValue);
        type_ = type;
    }

    [[nodiscard]] uint16_t Kind() const noexcept override
    {
        return static_cast<uint16_t>(Common::ETaskType::CurrencyUpdate);
    }

    [[nodiscard]] const Task::Paired<int64_t>& Value() const noexcept { return value_; }
    [[nodiscard]] Common::ECurrencyType Type() const noexcept { return type_; }

private:
    Task::Paired<int64_t> value_;
    Common::ECurrencyType type_{};
};
