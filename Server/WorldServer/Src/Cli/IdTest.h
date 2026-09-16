#pragma once

// `WorldServer.exe --idtest <노드번호> <스레드수> <스레드당개수> [random]` 진입점.
//
// RuidGenerator 가 다중 스레드 경합에서도 중복 없는 id를 만드는지, 그리고 그 id가
// 클러스터 인덱스에 순차 삽입되는지 확인한다. 측정 결과는 docs/design/unique-id.md.
//
// **프로세스를 여러 개 띄워 쓴다.** 한 프로세스에서 노드 번호를 바꿔가며 흉내 내지 않는
// 이유는 생성기가 프로세스당 하나인 싱글턴이기 때문이고, 무엇보다 **실제 사고(노드 번호가
// 겹치는 배포 실수)는 프로세스 사이에서 나기 때문**이다.
[[nodiscard]] int32_t RunIdTest(const std::string& connectionString, const uint32_t nodeId,
                                 const size_t threadCount, const size_t perThread,
                                 const bool randomMode);
