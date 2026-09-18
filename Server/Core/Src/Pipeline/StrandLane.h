#pragma once

#include <asio.hpp>

namespace Pipeline
{
    // 레인 하나. 스레드+큐+뮤텍스+조건변수로 만들던 소비자 자리를 strand 하나가 대신한다.
    //
    //   바뀌는 것   스레드와 레인의 1:1 대응이 사라진다. 한가한 스레드가 아무 레인이나
    //               집어간다(동시 실행만 안 될 뿐).
    //   안 바뀌는 것 같은 레인에 들어간 일은 넣은 순서대로, 겹치지 않고 처리된다.
    //               => 같은 ownerId의 처리 순서와 락 없는 상태 접근이 그대로 보장된다.
    using StrandLane = asio::strand<asio::io_context::executor_type>;
}
