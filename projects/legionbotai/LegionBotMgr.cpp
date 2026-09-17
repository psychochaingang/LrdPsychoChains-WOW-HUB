/*
 * LegionBotMgr.cpp
 * LegionBotAI command manager + companion creature bots for LegionCore (7.3.5)
 *
 * Spawns a full dungeon team of companion bots that follow the owner,
 * fight, tank (taunt), heal, and can be dismissed.
 *
 * Commands:
 *   .lbot                  - spawn the default tank companion
 *   .lbot tank             - spawn a tank
 *   .lbot healer           - spawn a healer
 *   .lbot dps              - spawn a damage dealer
 *   .lbot team             - spawn a full 4-bot team (tank + healer + 2 dps)
 *   .lbot <entry>          - spawn a custom-entry companion (tank AI)
 *   .lbot dismiss          - dismiss all your companions
 *   .lbot <player> [entry] - (console/SOAP) spawn for a named player
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "MotionMaster.h"
#include "Player.h"
#include "TemporarySummon.h"
#include "WorldSession.h"

#include <map>
#include <mutex>
#include <sstream>
#include <vector>

// LegionBotAI core (LegionBotAI.cpp) - real characters driven server-side
void LegionBot_Spawn(Player* owner, std::string const& charName, ChatHandler* handler, uint8 role = 255);
void LegionBot_DismissAll(Player* owner, ChatHandler* handler);
std::vector<ObjectGuid> LegionBot_GetBotsOf(ObjectGuid ownerGuid);
void LegionBot_DebugInfo(Player* owner, ChatHandler* handler);
bool LegionBot_ToggleSelfAI(Player* player);
void LegionBot_LevelCommand(Player* owner, std::string const& arg, ChatHandler* handler);
void LegionBot_AutogearTeam(Player* owner, ChatHandler* handler);
void LegionBot_TankCommand(Player* owner, std::string const& arg, ChatHandler* handler);

namespace
{
    // --- Default creature entries per role (Horde / goblin-flavoured) ---
    uint32 const COMPANION_BOT_TANK_ENTRY   = 68825;  // Bilgewater Bruiser
    uint32 const COMPANION_BOT_HEALER_ENTRY = 113035; // Darkspear Witch Doctor
    uint32 const COMPANION_BOT_DPS_ENTRY    = 67929;  // Bilgewater Sapper

    // --- Spells ---
    uint32 const SPELL_BOT_TAUNT     = 355;    // Taunt
    uint32 const SPELL_BOT_HEAL      = 19750;  // Flash of Light
    uint32 const SPELL_BOT_ATTACK    = 35395;  // Crusader Strike
    uint32 const SPELL_BOT_JUDGEMENT = 20271;  // Judgement
    uint32 const SPELL_BOT_SMITE     = 403;    // Lightning Bolt
    uint32 const SPELL_BOT_STRIKE    = 12294;  // Mortal Strike

    float  const FOLLOW_DISTANCE     = 2.0f;
    float  const TELEPORT_DISTANCE   = 60.0f;
    uint32 const TAUNT_COOLDOWN      = 6000;
    uint32 const HEAL_COOLDOWN       = 2200;
    uint32 const ATTACK_COOLDOWN     = 3500;
    uint32 const STRIKE_COOLDOWN     = 5000;

    enum CompanionBotRole : uint8
    {
        ROLE_TANK   = 0,
        ROLE_HEALER = 1,
        ROLE_DPS    = 2
    };

    // owner guid -> bot guids
    std::multimap<ObjectGuid, ObjectGuid> g_companionBots;
    std::mutex g_companionBotsMutex;

    void RegisterBot(ObjectGuid ownerGuid, ObjectGuid botGuid)
    {
        std::lock_guard<std::mutex> lock(g_companionBotsMutex);
        g_companionBots.insert(std::make_pair(ownerGuid, botGuid));
    }

    void UnregisterBot(ObjectGuid ownerGuid, ObjectGuid botGuid)
    {
        std::lock_guard<std::mutex> lock(g_companionBotsMutex);
        auto range = g_companionBots.equal_range(ownerGuid);
        for (auto itr = range.first; itr != range.second; ++itr)
        {
            if (itr->second == botGuid)
            {
                g_companionBots.erase(itr);
                return;
            }
        }
    }

    std::vector<Creature*> GetOwnerBots(Player* owner)
    {
        std::vector<Creature*> result;
        if (!owner)
            return result;

        std::lock_guard<std::mutex> lock(g_companionBotsMutex);
        auto range = g_companionBots.equal_range(owner->GetGUID());
        for (auto itr = range.first; itr != range.second; ++itr)
        {
            if (Creature* bot = ObjectAccessor::GetCreature(*owner, itr->second))
                result.push_back(bot);
        }
        return result;
    }
}

class CompanionBotAI : public CreatureAI
{
public:
    CompanionBotAI(Creature* creature, CompanionBotRole role) : CreatureAI(creature),
        _role(role), _ownerGuid(ObjectGuid::Empty), _followTimer(0), _tauntTimer(0), _healTimer(0),
        _attackTimer(0), _strikeTimer(0)
    {
        if (TempSummon* summon = creature->ToTempSummon())
            if (Unit* summoner = summon->GetSummoner())
                _ownerGuid = summoner->GetGUID();
    }

    void IsSummonedBy(Unit* summoner) override
    {
        _ownerGuid = summoner->GetGUID();
    }

    void EnterEvadeMode() override
    {
        CreatureAI::EnterEvadeMode();
        if (Unit* owner = GetBotOwner())
            me->GetMotionMaster()->MoveFollow(owner, FOLLOW_DISTANCE, 0.0f);
    }

    void JustDied(Unit* /*killer*/) override
    {
        UnregisterBot(_ownerGuid, me->GetGUID());
    }

    void UpdateAI(uint32 diff) override
    {
        Unit* owner = GetBotOwner();
        if (!owner || !owner->IsInWorld())
        {
            UnregisterBot(_ownerGuid, me->GetGUID());
            me->DespawnOrUnsummon();
            return;
        }

        // Teleport to owner if left behind
        if (me->GetDistance(owner) > TELEPORT_DISTANCE)
        {
            me->NearTeleportTo(owner->GetPosition());
            me->GetMotionMaster()->MoveFollow(owner, FOLLOW_DISTANCE, 0.0f);
            return;
        }

        if (!me->isInCombat())
        {
            // Keep following the owner when idle
            _followTimer += diff;
            if (_followTimer >= 1000)
            {
                _followTimer = 0;
                if (me->GetDistance(owner) > 4.0f)
                    me->GetMotionMaster()->MoveFollow(owner, FOLLOW_DISTANCE, 0.0f);
            }

            _tauntTimer = 0;
            _attackTimer = 0;
            _strikeTimer = 0;

            // Assist: attack whatever is attacking the owner (or their target)
            Unit* assistTarget = owner->getAttackerForHelper();
            if (!assistTarget && owner->getVictim())
                assistTarget = owner->getVictim();

            if (assistTarget && assistTarget->isAlive() && me->IsValidAttackTarget(assistTarget))
                AttackStart(assistTarget);

            return;
        }

        // --- In combat ---
        Unit* victim = me->getVictim();
        if (!victim || !victim->isAlive())
        {
            Unit* newTarget = owner->getAttackerForHelper();
            if (!newTarget && owner->getVictim())
                newTarget = owner->getVictim();
            if (newTarget && newTarget->isAlive() && me->IsValidAttackTarget(newTarget))
                AttackStart(newTarget);
            else
                return;
        }

        // Healer behaviour
        if (_role == ROLE_HEALER)
        {
            if (_healTimer <= diff)
            {
                _healTimer = HEAL_COOLDOWN;
                Unit* healTarget = nullptr;
                float lowestPct = 80.0f;

                // owner first
                if (owner->isAlive() && owner->GetHealthPct() < lowestPct)
                {
                    lowestPct = owner->GetHealthPct();
                    healTarget = owner;
                }

                // then the rest of the team
                for (Creature* bot : GetOwnerBots(owner->ToPlayer()))
                {
                    if (bot == me || !bot->isAlive())
                        continue;
                    if (bot->GetHealthPct() < lowestPct)
                    {
                        lowestPct = bot->GetHealthPct();
                        healTarget = bot;
                    }
                }

                // finally self
                if (!healTarget && me->GetHealthPct() < 50.0f)
                    healTarget = me;

                if (healTarget)
                    me->CastSpell(healTarget, SPELL_BOT_HEAL, false);
            }
            else
                _healTimer -= diff;

            if (_attackTimer <= diff)
            {
                _attackTimer = ATTACK_COOLDOWN;
                if (Unit* target = me->getVictim())
                    if (target->isAlive())
                        me->CastSpell(target, SPELL_BOT_SMITE, false);
            }
            else
                _attackTimer -= diff;

            DoMeleeAttackIfReady();
            return;
        }

        // Tank behaviour: taunt anything hitting the owner so the bot tanks it
        if (_role == ROLE_TANK)
        {
            if (_tauntTimer <= diff)
            {
                _tauntTimer = TAUNT_COOLDOWN;
                if (Unit* ownerAttacker = owner->getAttackerForHelper())
                    if (ownerAttacker->isAlive() && ownerAttacker != me->getVictim() && me->IsValidAttackTarget(ownerAttacker))
                        me->CastSpell(ownerAttacker, SPELL_BOT_TAUNT, false);
            }
            else
                _tauntTimer -= diff;
        }

        // Tank + DPS keep themselves up a bit
        if (_role != ROLE_HEALER)
        {
            if (_healTimer <= diff)
            {
                _healTimer = HEAL_COOLDOWN * 2;
                if (owner->isAlive() && owner->GetHealthPct() < 45.0f)
                    me->CastSpell(owner, SPELL_BOT_HEAL, false);
                else if (me->GetHealthPct() < 40.0f)
                    me->CastSpell(me, SPELL_BOT_HEAL, false);
            }
            else
                _healTimer -= diff;
        }

        // Damage abilities
        if (_attackTimer <= diff)
        {
            _attackTimer = ATTACK_COOLDOWN;
            if (Unit* target = me->getVictim())
                if (target->isAlive())
                    me->CastSpell(target, (_role == ROLE_DPS) ? SPELL_BOT_STRIKE : SPELL_BOT_ATTACK, false);
        }
        else
            _attackTimer -= diff;

        if (_role == ROLE_DPS)
        {
            if (_strikeTimer <= diff)
            {
                _strikeTimer = STRIKE_COOLDOWN;
                if (Unit* target = me->getVictim())
                    if (target->isAlive())
                        me->CastSpell(target, SPELL_BOT_JUDGEMENT, false);
            }
            else
                _strikeTimer -= diff;
        }

        DoMeleeAttackIfReady();
    }

private:
    Unit* GetBotOwner() const
    {
        if (_ownerGuid.IsEmpty())
            return nullptr;
        return ObjectAccessor::GetUnit(*me, _ownerGuid);
    }

    CompanionBotRole _role;
    ObjectGuid _ownerGuid;
    uint32 _followTimer;
    uint32 _tauntTimer;
    uint32 _healTimer;
    uint32 _attackTimer;
    uint32 _strikeTimer;
};

// ---------------------------------------------------------------------------
// Spawn / dismiss helpers
// ---------------------------------------------------------------------------

static void DespawnCompanions(Player* owner)
{
    if (!owner)
        return;

    for (Creature* bot : GetOwnerBots(owner))
        if (bot)
            bot->DespawnOrUnsummon();

    std::lock_guard<std::mutex> lock(g_companionBotsMutex);
    g_companionBots.erase(owner->GetGUID());
}

static void SpawnCompanion(Player* owner, uint32 entry, CompanionBotRole role, ChatHandler* handler)
{
    if (!owner)
        return;

    CreatureTemplate const* cinfo = sObjectMgr->GetCreatureTemplate(entry);
    if (!cinfo)
    {
        if (handler)
        {
            handler->PSendSysMessage("|cffff4444Companion bot:|r creature entry %u does not exist.", entry);
            handler->SetSentErrorMessage(true);
        }
        return;
    }

    TempSummon* summon = owner->SummonCreature(entry, TEMPSUMMON_MANUAL_DESPAWN);
    if (!summon)
    {
        if (handler)
        {
            handler->PSendSysMessage("|cffff4444Companion bot:|r failed to summon entry %u.", entry);
            handler->SetSentErrorMessage(true);
        }
        return;
    }

    Creature* bot = summon->ToCreature();

    bot->setFaction(owner->getFaction());
    bot->SelectLevel(bot->GetCreatureTemplate());
    bot->SetLevel(owner->getLevel());

    // Role based durability (scaled to the owner's level)
    float healthMultiplier = (role == ROLE_TANK) ? 4.0f : ((role == ROLE_HEALER) ? 2.0f : 2.5f);

    CreatureTemplate const* botTemplate = bot->GetCreatureTemplate();
    uint64 health = bot->GetMaxHealth();
    if (CreatureBaseStats const* stats = sObjectMgr->GetCreatureBaseStats(owner->getLevel(), botTemplate->unit_class))
        health = uint64(stats->GenerateHealth(botTemplate, bot->GetCreatureDiffStat()));

    health = uint64(health * healthMultiplier);
    bot->SetCreateHealth(health);
    bot->SetMaxHealth(health);
    bot->SetHealth(health);
    bot->UpdateDamagePhysical(BASE_ATTACK);

    bot->SetReactState(REACT_DEFENSIVE);
    bot->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED);

    bot->AIM_Initialize(new CompanionBotAI(bot, role));

    RegisterBot(owner->GetGUID(), bot->GetGUID());

    bot->GetMotionMaster()->MoveFollow(owner, FOLLOW_DISTANCE, 0.0f);

    if (handler)
    {
        char const* roleName = (role == ROLE_TANK) ? "tank" : ((role == ROLE_HEALER) ? "healer" : "damage");
        handler->PSendSysMessage("|cff33ff99Companion bot|r (%s, entry %u) summoned. Team: %u bot(s).",
            roleName, entry, uint32(GetOwnerBots(owner).size()));
    }
}

// ---------------------------------------------------------------------------
// Command:  .lbot [tank|healer|dps|team|<entry>|dismiss]
// ---------------------------------------------------------------------------

class legionbot_commandscript : public CommandScript
{
public:
    legionbot_commandscript() : CommandScript("legionbot_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> addCommandTable =
        {
            { "playerbot", SEC_PLAYER, true, &HandlePlayerbotCommand, "LegionBotAI: team | spawn <name> | self | dismiss | info | level | autogear | aggro | creatures" }
        };

        static std::vector<ChatCommand> CommandTable =
        {
            { "lbot", SEC_PLAYER, true, &HandlePlayerbotCommand, "LegionBotAI: team | spawn <name> | self | dismiss | info | level | autogear | aggro | creatures" },
            { "add", SEC_PLAYER, true, nullptr, "", addCommandTable }
        };

        return CommandTable;
    }

    static bool HandlePlayerbotCommand(ChatHandler* handler, char const* args)
    {
        Player* target = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;

        std::string first;
        std::string second;
        std::string third;
        if (args && *args)
        {
            std::istringstream iss(args);
            iss >> first;
            iss >> second;
            iss >> third;
        }

        // ".lbot dismiss" - removes companion creatures + playerbots
        if (first == "dismiss")
        {
            if (!target)
            {
                handler->SendSysMessage("|cffff4444Companion bot:|r this command requires a player.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            DespawnCompanions(target);
            LegionBot_DismissAll(target, handler);
            handler->PSendSysMessage("|cff33ff99Companion bot|r team dismissed.");
            return true;
        }

        // Console/SOAP: ".lbot <playerName> <role|spawn> [charName]"
        bool const isRoleKeyword = (first == "spawn" || first == "team" || first == "creatures" || first == "tank" || first == "healer" || first == "dps" || first == "dismiss" || first == "self" || first == "rescue" || first == "level" || first == "autogear" || first == "aggro");
        if (!target && !first.empty() && !isRoleKeyword)
        {
            target = ObjectAccessor::FindPlayerByName(first);
            if (!target)
            {
                handler->PSendSysMessage("|cffff4444Companion bot:|r player '%s' not found or not online.", first.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }
            first = second;
            second = third;
        }

        // ".lbot <player> dismiss" - console form
        if (first == "dismiss")
        {
            if (!target)
            {
                handler->SendSysMessage("|cffff4444Companion bot:|r this command requires a player.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            DespawnCompanions(target);
            LegionBot_DismissAll(target, handler);
            handler->PSendSysMessage("|cff33ff99Companion bot|r team dismissed.");
            return true;
        }

        // ".lbot spawn <charName>" - spawn a real character as a bot
        if (first == "spawn")
        {
            if (second.empty())
            {
                handler->SendSysMessage("|cffff4444Playerbot:|r usage: .lbot spawn <characterName>");
                handler->SetSentErrorMessage(true);
                return false;
            }
            LegionBot_Spawn(target, second, handler);
            return true;
        }

        // ".lbot info" - diagnostic: where are my bots?
        if (first == "info")
        {
            if (!target)
            {
                handler->SendSysMessage("|cffff4444Playerbot:|r this command requires a player.");
                handler->SetSentErrorMessage(true);
                return false;
            }

            std::vector<ObjectGuid> botGuids = LegionBot_GetBotsOf(target->GetGUID());
            if (botGuids.empty())
            {
                handler->SendSysMessage("|cff33ff99Playerbot:|r no bots registered for you.");
                return true;
            }

            LegionBot_DebugInfo(target, handler);

            for (ObjectGuid botGuid : botGuids)
            {
                if (Player* bot = ObjectAccessor::FindPlayer(botGuid))
                {
                    handler->PSendSysMessage("|cff33ff99Bot|r %s: map %u inst %u pos (%.1f %.1f %.1f) | you: map %u inst %u pos (%.1f %.1f %.1f) | dist %.1f",
                        bot->GetName(), bot->GetMapId(), bot->GetInstanceId(),
                        bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                        target->GetMapId(), target->GetInstanceId(),
                        target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(),
                        bot->GetDistance(target));

                    handler->PSendSysMessage("|cff33ff99flags|r inWorld=%d changeMap=%d deleted=%d canContact=%d loading=%d logout=%d session=%d",
                        bot->IsInWorld() ? 1 : 0, bot->IsChangeMap() ? 1 : 0, bot->IsDelete() ? 1 : 0, bot->CanContact() ? 1 : 0,
                        bot->GetSession()->PlayerLoading() ? 1 : 0, bot->GetSession()->PlayerLogout() ? 1 : 0, bot->GetSession() ? 1 : 0);
                }
                else
                    handler->SendSysMessage("|cff33ff99Playerbot:|r a registered bot is not in the world.");
            }
            return true;
        }

        // ".lbot level sync|max|<n>" - how the team levels (playerbot-style)
        if (first == "level")
        {
            if (!target)
            {
                handler->SendSysMessage("|cffff4444LegionBot:|r this command requires a player.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            LegionBot_LevelCommand(target, second, handler);
            return true;
        }

        // ".lbot autogear" - re-equip the team with level-appropriate gear
        if (first == "autogear")
        {
            if (!target)
            {
                handler->SendSysMessage("|cffff4444LegionBot:|r this command requires a player.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            LegionBot_AutogearTeam(target, handler);
            return true;
        }

        // ".lbot aggro me|bot" - who holds aggro (player tank mode)
        if (first == "aggro")
        {
            if (!target)
            {
                handler->SendSysMessage("|cffff4444LegionBot:|r this command requires a player.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            LegionBot_TankCommand(target, second, handler);
            return true;
        }

        // ".lbot self" - toggle self-AI: the bot AI fights for you
        if (first == "self")
        {
            if (!target)
            {
                handler->SendSysMessage("|cffff4444LegionBot:|r this command requires a player.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            if (LegionBot_ToggleSelfAI(target))
                handler->PSendSysMessage("|cff33ff99LegionBot:|r self-AI enabled - I'll attack and use abilities for you. Use '.lbot self' again to stop.");
            else
                handler->PSendSysMessage("|cff33ff99LegionBot:|r self-AI disabled.");
            return true;
        }

        // ".lbot rescue" - teleport the player home and revive (unstuck from under the map)
        if (first == "rescue")
        {
            if (!target)
            {
                handler->SendSysMessage("|cffff4444LegionBot:|r this command requires a player.");
                handler->SetSentErrorMessage(true);
                return false;
            }

            target->CombatStop(true);
            if (target->isDead())
            {
                target->ResurrectPlayer(1.0f, false);
                target->SpawnCorpseBones();
            }
            target->SetHealth(target->GetMaxHealth());
            target->TeleportTo(target->m_homebindMapId, target->m_homebindX, target->m_homebindY, target->m_homebindZ, target->GetOrientation());
            handler->PSendSysMessage("|cff33ff99LegionBot:|r rescued - revived and teleported home.");
            return true;
        }

        // Role handling
        if (first == "team")
        {
            // The real dungeon team: 4 playerbot characters with 7.3.5 meta classes.
            // The team is chosen by the owner's faction so Alliance characters get
            // Alliance bots (and vice versa).
            if (target)
            {
                DespawnCompanions(target);
                LegionBot_DismissAll(target, nullptr);
            }

            bool const alliance = target && (target->GetTeamId() == TEAM_ALLIANCE);
            if (alliance)
            {
                LegionBot_Spawn(target, "Aegis",     handler, 0);   // Human Blood DK tank
                LegionBot_Spawn(target, "Seraphine", handler, 1);   // Draenei Holy Paladin healer
                LegionBot_Spawn(target, "Rook",      handler, 2);   // Human Fury Warrior dps
                LegionBot_Spawn(target, "Elowen",    handler, 1);   // Night Elf Holy Priest healer
                handler->PSendSysMessage("|cff33ff99Alliance playerbot team ready!|r (Human DK tank, Draenei + Night Elf healers, Human Fury Warrior)");
            }
            else
            {
                LegionBot_Spawn(target, "Bulwark", handler, 0);   // Blood DK tank
                LegionBot_Spawn(target, "Lovley",  handler, 1);   // Holy Paladin healer
                LegionBot_Spawn(target, "Ember",   handler, 2);   // Fury Warrior dps
                LegionBot_Spawn(target, "Faith",   handler, 1);   // Holy Priest healer (heals only)
                handler->PSendSysMessage("|cff33ff99Full playerbot dungeon team ready!|r (Blood DK tank, Paladin + Priest healers, Fury Warrior damage)");
            }
            return true;
        }
        if (first == "creatures")
        {
            // The NPC companion team (creature bots)
            if (target)
                DespawnCompanions(target);
            SpawnCompanion(target, COMPANION_BOT_TANK_ENTRY,   ROLE_TANK,   handler);
            SpawnCompanion(target, COMPANION_BOT_HEALER_ENTRY, ROLE_HEALER, handler);
            SpawnCompanion(target, COMPANION_BOT_DPS_ENTRY,    ROLE_DPS,    handler);
            SpawnCompanion(target, COMPANION_BOT_DPS_ENTRY,    ROLE_DPS,    handler);
            handler->PSendSysMessage("|cff33ff99NPC companion team ready!|r (1 tank, 1 healer, 2 damage)");
            return true;
        }

        if (!target)
        {
            handler->SendSysMessage("|cffff4444Companion bot:|r this command requires a player (use .lbot <player> [role] from console).");
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (first == "tank")
        {
            SpawnCompanion(target, COMPANION_BOT_TANK_ENTRY, ROLE_TANK, handler);
            return true;
        }
        if (first == "healer")
        {
            SpawnCompanion(target, COMPANION_BOT_HEALER_ENTRY, ROLE_HEALER, handler);
            return true;
        }
        if (first == "dps")
        {
            SpawnCompanion(target, COMPANION_BOT_DPS_ENTRY, ROLE_DPS, handler);
            return true;
        }

        // Custom entry
        uint32 entry = COMPANION_BOT_TANK_ENTRY;
        if (!first.empty())
            entry = uint32(atoi(first.c_str()));

        SpawnCompanion(target, entry, ROLE_TANK, handler);
        return true;
    }
};

void AddSC_LegionBotMgr()
{
    new legionbot_commandscript();
}


