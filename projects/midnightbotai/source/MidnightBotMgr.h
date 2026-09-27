/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _MIDNIGHTBOTMGR_H
#define _MIDNIGHTBOTMGR_H

#include "Define.h"
#include "ObjectGuid.h"
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class Player;
class Group;
class Unit;
class WorldSession;

enum class BotRole : uint8
{
    None = 0,
    Tank = 1,
    Healer = 2,
    Dps = 3
};

class MidnightBotMgr
{
public:
    struct PendingCastCheck
    {
        ObjectGuid targetGuid;
        uint64 healthBefore = 0;
        uint32 spellId = 0;
        uint32 dueMs = 0;
        bool offensive = false;
        uint32 damage = 0;          // direct-damage wind-up payload (applyDirect only)
        uint32 schoolMask = 0;
        bool applyDirect = false;
    };

    struct BotRef
    {
        uint32 guid = 0;
        std::string name;
        ObjectGuid playerGuid;
        uint32 accountId = 0;
        WorldSession* session = nullptr;
        bool inWorld = false;
        bool queued = false;
        ObjectGuid ownerGuid;
        bool follow = false;
        bool assist = false;
        BotRole role = BotRole::None;
        uint32 castSpellId = 0;             // cast tracking for interruption diagnostics
        std::string castTargetName;
        ObjectGuid castTargetGuid;
        uint64 castHealthBefore = 0;
        bool castOffensive = false;
        float castMaxRange = 0.0f;
        uint32 castStartMs = 0;
        uint32 castTimeMs = 0;
        ObjectGuid meleeTargetGuid;         // melee damage instrumentation
        std::string meleeTargetName;
        uint64 meleeHealthLast = 0;
        uint32 meleeAccum = 0;
        uint32 meleeFlushAt = 0;
        ObjectGuid victimGuid;              // current victim + acquisition time (stickiness grace)
        uint32 victimSinceMs = 0;
        ObjectGuid attackRejectedGuid;      // last target that Unit::Attack() refused
        ObjectGuid attackAcceptedGuid;      // last target Unit::Attack() engaged (logged once)
        float spacingAngle = 0.0f;          // personal parking/kite angle (radians, guid-derived)
        float spacingDist = 0.0f;           // personal follow distance variance (-0.5/0/+0.5 yd)
        uint32 threatLogAt = 0;             // next periodic threat/melee-geometry report
        uint64 healthLast = 0;              // damage-taken sampling
        uint32 lastManualSwingMs = 0;       // module-side swing pacing (core resets its own timer)
        uint32 combatChatAt = 0;            // rate limit for [MC] owner feedback
        uint32 dpsWaitLogAt = 0;            // rate limit for "dps waiting for tank threat"
        std::map<uint32, uint32> spellPace; // module-side per-spell pacing (spellId -> next ms)
        // damage/heal statistics (session totals + 60 s sample rings)
        uint64 dmgSession = 0;
        uint32 hitsSession = 0;
        uint64 healSession = 0;
        uint32 healHits = 0;
        std::deque<std::pair<uint32, uint32>> dmgRecent;
        std::deque<std::pair<uint32, uint32>> healRecent;
        std::deque<PendingCastCheck> pendingCasts; // delayed effect verification (1.5 s after cast)
        std::deque<std::string> aiLog;      // last 10 AI decisions/target rejections
        std::string lastAiRaw;              // dedupe guard for aiLog/TC_LOG
    };

    struct ActiveBotInfo
    {
        std::string name;
        std::string ownerName;
        std::string phases;
        std::string ownerPhases;
        std::string motion;
        std::string roleName;
        uint32 guid = 0;
        ObjectGuid playerGuid;
        ObjectGuid ownerGuid;
        bool follow = false;
        bool assist = false;
        bool inWorld = false;
        bool resolved = false;
        bool canSeeOwner = false;
        uint32 mapId = 0;
        uint32 zoneId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float distanceToViewer = -1.0f; // negative when unknown (no viewer or different map/instance)
    };

    static MidnightBotMgr* instance();

    void LoadConfig();
    void LoadRoster();
    void BootstrapRoster();
    void SetVersion(std::string const& value);

    bool IsEnabled() const;
    void SetRuntimeEnabled(bool enabled);
    bool SpawningEnabled() const;
    bool AutoSpawnEnabled() const;
    bool SummonOnSpawnEnabled() const;
    bool AutoPartyEnabled() const;
    bool CombatLogEnabled() const;
    bool StatsEnabled() const;
    bool LevelSyncEnabled() const;
    bool AutoTalentsEnabled() const;
    std::string LootRollMode() const;
    uint32 MaxBots() const;
    std::string AccountPrefix() const;
    std::string BootstrapList() const;
    std::string DebugFile() const;

    bool CreateBot(std::string const& name, uint8 race, uint8 classId, std::string& err, uint32& outGuid);
    bool AddBotByName(std::string const& name, std::string& err);
    bool RemoveBotByName(std::string const& name);
    std::vector<std::string> ListBots() const;
    size_t Count() const;
    bool IsBotActive(std::string const& name) const;
    bool IsBotInRoster(std::string const& name) const;
    std::vector<std::string> ActiveBotNames() const;

    bool SpawnBot(std::string const& name, std::string& err, ObjectGuid ownerGuid = ObjectGuid::Empty);
    bool DespawnBot(std::string const& name, std::string& err);
    size_t ActiveCount() const;
    void Update(uint32 diff);

    // M2.3/M3.1 helpers. All of them require the bot to be active and in the world.
    bool SummonBot(std::string const& name, Player* target, std::string& err);
    bool GotoBot(std::string const& name, Player* target, std::string& err);
    bool SummonAllBots(Player* target, uint32& moved, uint32& failed);
    bool AddBotToGroup(std::string const& name, Player* owner, std::string& err);
    bool PartyAllBots(Player* owner, uint32& added, uint32& alreadyGrouped, uint32& failed);
    bool SetBotFollow(std::string const& name, bool follow, Player* issuer, std::string& err);
    bool SetBotAssist(std::string const& name, bool assist, Player* issuer, std::string& err);
    bool SetBotRole(std::string const& name, BotRole role, std::string& err);
    bool ResurrectBot(std::string const& name, std::string& err);
    uint32 LearnBotSpells(std::string const& name, std::string& err);
    bool BoostBot(std::string const& name, Player* issuer, uint8 targetLevel, std::string& err);
    bool TakeGroupLead(std::string const& name, Player* issuer, uint32& botsUnder, std::string& err);
    bool BotAttackTarget(std::string const& name, Player* owner, Unit* target, std::string& err);
    bool ComeBot(std::string const& name, Player* issuer, std::string& err);
    bool GetActiveBotInfo(std::string const& name, Player* viewer, ActiveBotInfo& info, std::string& err) const;
    std::vector<ActiveBotInfo> ListActiveBotInfo(Player* viewer) const;
    bool DumpDebug(std::string const& version, std::string const& issuer, std::string const& onlyBot, std::string& outPath, std::string& err);
    void NotifyBotDamage(Unit* attacker, Unit* victim, uint32 damage);
    std::vector<std::string> VisibilityReport(std::string const& botName, Player* viewer, std::string& err);
    std::vector<std::string> DamageReportLines();
    bool LootShow(Player* owner, std::string& out, std::string& err);
    bool LootSetMethod(Player* owner, std::string const& method, std::string& err);
    bool LootSetThreshold(Player* owner, std::string const& quality, std::string& err);
    void ApplyDefaultLootRules(Group* group);
    // Persists the owner binding; must be called with rosterMutex held.
    void SaveBotOwnerLocked(uint32 botGuid, ObjectGuid ownerGuid);

private:
    MidnightBotMgr() = default;
    MidnightBotMgr(MidnightBotMgr const&) = delete;
    MidnightBotMgr& operator=(MidnightBotMgr const&) = delete;

    Player* ResolveActiveBotPlayer(std::string const& name, std::string& err, uint32* outIndex = nullptr) const;
    void LogAi(ObjectGuid botGuid, std::string const& message);
    void NoteBotCastStarted(ObjectGuid botGuid, uint32 spellId, Unit* target, uint32 castTimeMs, bool offensive, float maxRange);
    void CheckBotCastState(ObjectGuid botGuid, Player* bot);
    void TrackBotMeleeDamage(ObjectGuid botGuid, Player* bot);
    void TrackBotVictim(ObjectGuid botGuid, Player* bot);
    void TrackBotDamageTaken(ObjectGuid botGuid, Player* bot);
    void CheckPendingCasts(ObjectGuid botGuid, Player* bot);
    bool TryBotMeleeSwing(ObjectGuid botGuid, Player* bot);
    bool TryBotSpellPace(ObjectGuid botGuid, uint32 spellId, uint32 cooldownMs);
    bool DpsWaitLogDue(ObjectGuid botGuid);
    void SendCombatChat(ObjectGuid botGuid, std::string const& message);
    void AddBotDamage(ObjectGuid botGuid, uint32 damage);
    void AddBotHeal(ObjectGuid botGuid, uint32 heal);
    void SendStatsToOwners();
    Unit* FindTankBotVictim(Player* self, Player* owner);
    Player* FindTankBot(Player* self) const;
    std::vector<ObjectGuid> GetTankBotGuids() const;
    uint32 LearnAutoTalents(Player* bot);
    SpellCastResult BotCastAndReport(ObjectGuid botGuid, Player* bot, Unit* target, uint32 spellId, Player* owner, bool offensive);
    bool BotVictimInGrace(ObjectGuid botGuid, uint32 graceMs) const;
    void NoteAttackResult(ObjectGuid botGuid, Unit* target, bool success);

    bool enabled = true;
    bool spawning = false;
    bool autoSpawn = false;
    bool summonOnSpawn = false;
    bool autoParty = false;
    bool combatLog = true;
    bool combatChat = true;
    bool stats = true;
    bool levelSync = true;
    bool autoTalents = true;
    bool debugAuto = true;
    bool autoSpawnAttempted = false;
    uint32 uptime = 0;
    uint32 followTimer = 0;
    uint32 debugAutoTimer = 0;
    uint32 statsTimer = 0;
    uint32 levelSyncTimer = 0;
    uint32 talentTimer = 0;
    uint32 spacingTimer = 0;
    uint32 maxBots = 8;
    std::string accountPrefix = "mbot";
    std::string bootstrap;
    std::string debugFile = "mbot-debug.txt";
    std::string versionString = "unknown";
    std::string lootMethod = "group";
    std::string lootThreshold = "uncommon";
    std::string lootRoll = "pass";
    std::vector<std::pair<uint32, std::string>> roster;
    std::map<uint32, ObjectGuid> rosterOwners; // persisted owner bindings (bot guid -> owner guid)
    std::vector<BotRef> activeBots;
    std::map<std::string, BotRole> bootstrapRoles;
    mutable std::mutex rosterMutex;
};

#define sMidnightBotMgr MidnightBotMgr::instance()

#endif
