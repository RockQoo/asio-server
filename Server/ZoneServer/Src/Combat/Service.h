#pragma once

#include "Combat/AttackDef.h"
#include "Combat/UnitRegistry.h"
#include "Game/Def.h"
#include "Packet/ZonePackets.h"

#include "Shared/Protocol/Src/ErrorCode.h"
#include "Shared/Protocol/Src/PacketId.h"

namespace Combat
{
    // 존 하나의 전투. **여기 있는 메서드는 전부 그 존의 존 레인(owner = zoneId)에서만 돈다.**
    //
    // 이 프로젝트에서 전투가 갖는 의미는 밸런스가 아니라 **"틱과 패킷 핸들러가 같은 상태를
    // 만지는 문제"를 락이 아니라 strand 배정으로 푼 사례**라는 것이다. 이동/우편/재화는 전부
    // 요청-응답이라 주인이 세션 하나로 끝났는데, 전투는 아무도 요청하지 않아도 서버가 혼자
    // 시간을 굴리고(리젠·리스폰·AI) 그 시간이 만지는 데이터를 패킷도 만진다.
    //
    // 같은 문제를 **유닛 단위 락**으로 푸는 길도 있다(존 하나에 수백 명이 들어오면 직렬화로는
    // 못 버틴다). 여기서는 반대로 골랐고, 갈아탈 지점도 정해져 있다 -- **존 레인의 큐 대기
    // 시간이 틱 주기를 넘기 시작하면** 그때가 한계다(Processor::Group의 레인 통계).
    // 비교표와 근거: docs/design/combat-lane.md
    class Service final
    {
    public:
        // Service가 World 링크도 브로드캐스트 레인도 모르게 하는 통로. 전투 코드가 전송 경로를
        // 알기 시작하면 "존은 DB도 World도 모른다"는 경계가 이 파일에서부터 새기 시작한다.
        class ISender
        {
        public:
            virtual ~ISender() = default;

            ISender(const ISender&) = delete;
            ISender& operator=(const ISender&) = delete;

            virtual void SendToClient(const Network::SessionId clientSessionId, const PacketId innerPacketId,
                                      const std::span<const byte> payload) = 0;

            // excludeClientSessionId가 0이면 아무도 빼지 않는다.
            virtual void BroadcastToZone(const PacketId innerPacketId, const std::span<const byte> payload,
                                         const Network::SessionId excludeClientSessionId) = 0;

        protected:
            ISender() = default;
        };

        Service(const Protocol::ZoneId zoneId, ISender& sender);

        // 담당 구간 안에 더미 몬스터를 세운다. 존이 만들어질 때 한 번.
        void SpawnMonsters(const Zone::Def& zoneDef);

        void OnPlayerEnter(const Network::SessionId clientSessionId, const float x, const float y);
        void OnPlayerLeave(const Network::SessionId clientSessionId);

        // 틱마다 로스터의 확정 좌표를 유닛에 옮긴다. 위치의 권위는 MoveModel이고 전투는
        // 사거리 판정에 쓰기만 한다 -- 두 곳에서 좌표를 고치면 어느 쪽이 맞는지 알 수 없다.
        void UpdatePlayerPosition(const Network::SessionId clientSessionId, const float x, const float y);

        void HandleAttack(const Network::SessionId clientSessionId, const Zone::AttackPacket& packet);

        // 리젠 -> 리스폰 -> 몬스터 AI -> 변경분 일괄 전송. **밀린 틱 건너뛰기는 여기가 아니라
        // Timer::RepeatingTimer가 이미 한다**(bypassCount로 관측 가능).
        void Tick(const std::chrono::steady_clock::time_point now, const float deltaSeconds);

        [[nodiscard]] size_t UnitCount() const noexcept { return units_.Count(); }

    private:
        struct HitResult
        {
            int32_t damage;
            int32_t targetHp;
            bool dead;
        };

        // **포인터가 아니라 id를 받는다.** 즉발 전투에서는 대상이 사라질 틈이 없어서 아래
        // 재조회가 한 번도 참이 되지 않지만, 나중에 투사체 비행 시간을 넣으면 그 한 줄이
        // 그대로 "날아가는 중에 대상이 죽었다/존을 나갔다"의 답이 된다.
        HitResult ApplyHit(const Protocol::UnitId attackerId, const Protocol::UnitId targetId,
                           const AttackDef& attackDef, const std::chrono::steady_clock::time_point now);

        void OnUnitDead(Unit& unit, const Protocol::UnitId killerId,
                        const std::chrono::steady_clock::time_point now);

        void UpdateRegen(const float deltaSeconds);
        void UpdateRespawn(const std::chrono::steady_clock::time_point now);
        void UpdateMonsterAi(const std::chrono::steady_clock::time_point now);
        void FlushDirtyUnits();

        void ReplyAttackResult(const Network::SessionId clientSessionId, const EErrorCode errorCode,
                               const Zone::AttackPacket& packet, const int32_t damage,
                               const int32_t targetHp) const;
        void BroadcastAttack(const Unit& attacker, const Protocol::UnitId targetId,
                             const EAttackKind attackKind, const int32_t damage) const;
        void BroadcastSpawn(const Unit& unit, const Network::SessionId excludeClientSessionId) const;
        void SendSnapshotTo(const Network::SessionId clientSessionId) const;

        // 항목 목록을 kMaxUnitsPerPacket 단위로 끊어 여러 장으로 보낸다. 자르지 않고 쪼개는
        // 이유는 ContentLimit.h 주석 참고(잘라내면 그 유닛의 HP가 영영 갱신되지 않는다).
        template <typename TEntry, typename TSend>
        static void SendChunked(const std::vector<TEntry>& entries, TSend&& send);

        Protocol::ZoneId zoneId_;
        ISender& sender_;
        UnitRegistry units_;

        // Tick에서 재사용하는 버퍼. 매 틱 할당하지 않으려고 멤버로 둔다 --
        // **존 레인 전용이라 공유 문제가 없다.**
        std::vector<Zone::UnitStateEntry> dirtyBuffer_;
    };
}
