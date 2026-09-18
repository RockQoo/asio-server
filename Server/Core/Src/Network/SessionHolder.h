#pragma once

#include "Server/Core/Src/Network/Session.h"

namespace Network
{
    // 나가는 연결(Connector 가 만든 것) **하나**를 들고 있는 스레드 세이프 홀더.
    //
    // 왜 락이 필요한가: 이 세션을 만지는 스레드가 여럿이다. 연결을 세우고 끊는 것은 그
    // 링크의 I/O 스레드이고, 보내는 쪽은 각 서버의 로직 레인이나 타이머 스레드다.
    // Session::SendPacket 자체는 어느 스레드에서 불러도 되지만, **여기 담긴 포인터를
    // 바꾸는 것과 읽는 것**이 겹치므로 그건 따로 막아야 한다.
    //
    // 값으로 돌려주는 이유: 참조를 주면 호출부가 락을 벗어난 뒤에도 들고 있을 수 있고,
    // 그 사이 Clear() 가 불리면 댕글링이다. SPtr 하나 복사는 싸다.
    class SessionHolder
    {
    public:
        void Set(const Session::SPtr& session)
        {
            std::unique_lock lock(mutex_);
            session_ = session;
        }

        void Clear()
        {
            std::unique_lock lock(mutex_);
            session_.reset();
        }

        [[nodiscard]] Session::SPtr Get() const
        {
            std::shared_lock lock(mutex_);
            return session_;
        }

    private:
        mutable std::shared_mutex mutex_;
        Session::SPtr session_;
    };
}
