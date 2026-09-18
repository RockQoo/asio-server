#pragma once

#include "Server/Core/Src/Pipeline/Types.h"

// AddProcessor()가 돌려준 ProcessorId를 보관하는 자리.
// **"누구한테 보낼지"를 알아야 PushMsg를 할 수 있다.**
//
// 프로세서 id가 컴파일 타임 상수가 아니라 **등록 순서로 정해지는 런타임 인덱스**라
// (MessageProducer::AddProcessor가 돌려준다) 어딘가 보관해야 하고, 보내는 쪽은 이 구조체만
// 본다. 보내는 쪽이 받는 쪽 객체를 몰라도 되는 것이 이 구조의 요점이다.
//
// **스레드 규약**: 기동 때 WorldApp이 한 번 채우고, 이후 레인 스레드들이 읽기만 한다.
// 채우기 전에 PushMsg를 하면 id가 무효(-1)라 MessageProducer가 경고를 남기고 버린다.
struct WorldProcessorIds final
{
    Pipeline::ProcessorId network{};     // NETWORK 레인 -- 송신 전담
    Pipeline::ProcessorId main{};        // BASIC -- 게이트웨이 수신구 + 플레이어 콘텐츠
    Pipeline::ProcessorId login{};       // BASIC -- 인증/자동 가입
    Pipeline::ProcessorId zoneStream{};  // BASIC -- 존 커넥션 수신구
    Pipeline::ProcessorId tool{};        // BASIC -- 운영툴 접속구
    Pipeline::ProcessorId db{};          // DB
    Pipeline::ProcessorId timer{};       // TIMER
};

// 이 프로세스의 프로세서 id 묶음. 전역이지만 값만 들고 있고 아무것도 소유하지 않는다.
[[nodiscard]] WorldProcessorIds& Ids();
