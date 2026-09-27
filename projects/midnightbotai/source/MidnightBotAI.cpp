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

#include "Chat.h"
#include "ChatCommand.h"
#include "DB2Stores.h"
#include "Log.h"
#include "MidnightBotMgr.h"
#include "Player.h"
#include "RaceMask.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Util.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

using namespace Trinity::ChatCommands;

namespace
{
    constexpr char const* MidnightBotAIVersion = "4.8";

    std::string MidnightBotZoneName(uint32 zoneId, LocaleConstant locale)
    {
        if (zoneId)
            if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(zoneId))
                return area->AreaName[locale];

        return "unknown";
    }

    bool ParseBotRole(std::string text, BotRole& role)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });

        if (text == "none")
            role = BotRole::None;
        else if (text == "tank")
            role = BotRole::Tank;
        else if (text == "healer")
            role = BotRole::Healer;
        else if (text == "dps")
            role = BotRole::Dps;
        else
            return false;

        return true;
    }

    bool TokenIsAll(std::string const& token)
    {
        if (token.empty())
            return true;

        if (token.size() != 3)
            return false;

        return std::tolower(static_cast<unsigned char>(token[0])) == 'a'
            && std::tolower(static_cast<unsigned char>(token[1])) == 'l'
            && std::tolower(static_cast<unsigned char>(token[2])) == 'l';
    }

    // Runs an action for one named bot, or for every active bot when the name is omitted/"all".
    // Reply is kept to a single short line per requirement.
    template <typename Action>
    bool ApplyToActiveBots(ChatHandler* handler, Optional<std::string> const& name, std::string const& label, Action&& action)
    {
        if (name && !TokenIsAll(*name))
        {
            std::string err;
            if (!action(*name, err))
                handler->PSendSysMessage("%s: %s failed - %s", name->c_str(), label.c_str(), err.c_str());
            else
                handler->PSendSysMessage("%s: %s", name->c_str(), label.c_str());

            return true;
        }

        std::vector<std::string> names = sMidnightBotMgr->ActiveBotNames();
        if (names.empty())
        {
            handler->PSendSysMessage("all: %s - no active bots", label.c_str());
            return true;
        }

        uint32 ok = 0;
        uint32 failed = 0;
        for (std::string const& botName : names)
        {
            std::string err;
            if (action(botName, err))
                ++ok;
            else
                ++failed;
        }

        if (failed)
            handler->PSendSysMessage("all: %s (%u ok, %u failed)", label.c_str(), ok, failed);
        else
            handler->PSendSysMessage("all: %s (%u bots)", label.c_str(), ok);

        return true;
    }

    void PrintBotInfo(ChatHandler* handler, MidnightBotMgr::ActiveBotInfo const& info)
    {
        char const* ownerName = info.ownerName.empty() ? "<none>" : info.ownerName.c_str();
        char const* followState = info.follow ? "on" : "off";
        char const* assistState = info.assist ? "on" : "off";
        char const* inWorldState = info.inWorld ? "yes" : "no";
        char const* roleState = info.roleName.empty() ? "none" : info.roleName.c_str();

        if (!info.resolved)
        {
            handler->PSendSysMessage("  %s (guid %u): inWorld %s, still loading, owner: %s, follow: %s, assist: %s, role: %s",
                info.name.c_str(), info.guid, inWorldState, ownerName, followState, assistState, roleState);
            return;
        }

        std::string zoneName = MidnightBotZoneName(info.zoneId, handler->GetSessionDbcLocale());
        char const* phaseSee = info.canSeeOwner ? "yes" : "no";

        if (info.distanceToViewer >= 0.0f)
            handler->PSendSysMessage("  %s (guid %u): inWorld %s, map %u, zone %s, pos %.1f %.1f %.1f, dist %.1f yd, owner: %s, follow: %s, assist: %s, role: %s, motion: %s, canSeeOwner(phase): %s, phases: [%s], owner phases: [%s]",
                info.name.c_str(), info.guid, inWorldState, info.mapId, zoneName.c_str(), info.x, info.y, info.z, info.distanceToViewer,
                ownerName, followState, assistState, roleState, info.motion.c_str(), phaseSee, info.phases.c_str(), info.ownerPhases.c_str());
        else
            handler->PSendSysMessage("  %s (guid %u): inWorld %s, map %u, zone %s, pos %.1f %.1f %.1f, dist n/a, owner: %s, follow: %s, assist: %s, role: %s, motion: %s, canSeeOwner(phase): %s, phases: [%s], owner phases: [%s]",
                info.name.c_str(), info.guid, inWorldState, info.mapId, zoneName.c_str(), info.x, info.y, info.z,
                ownerName, followState, assistState, roleState, info.motion.c_str(), phaseSee, info.phases.c_str(), info.ownerPhases.c_str());
    }
}

class MidnightBotAIWorldScript : public WorldScript
{
public:
    MidnightBotAIWorldScript() : WorldScript("MidnightBotAIWorldScript") { }

    void OnStartup() override
    {
        sMidnightBotMgr->LoadConfig();
        sMidnightBotMgr->LoadRoster();
        sMidnightBotMgr->SetVersion(MidnightBotAIVersion);
        sMidnightBotMgr->BootstrapRoster();

        TC_LOG_INFO("server.loading", "MidnightBotAI loaded (v{})", MidnightBotAIVersion);
        TC_LOG_INFO("server.loading", "MidnightBotAI config: enabled={}, spawning={}, autospawn={}, summonOnSpawn={}, autoParty={}, MaxBots={}, AccountPrefix={}, bootstrap='{}'",
            sMidnightBotMgr->IsEnabled(), sMidnightBotMgr->SpawningEnabled(), sMidnightBotMgr->AutoSpawnEnabled(),
            sMidnightBotMgr->SummonOnSpawnEnabled(), sMidnightBotMgr->AutoPartyEnabled(),
            sMidnightBotMgr->MaxBots(), sMidnightBotMgr->AccountPrefix(), sMidnightBotMgr->BootstrapList());

        if (!sMidnightBotMgr->IsEnabled())
            TC_LOG_INFO("server.loading", "MidnightBotAI is disabled by MidnightBotAI.Enable = 0");
        else if (!sMidnightBotMgr->SpawningEnabled())
            TC_LOG_INFO("server.loading", "MidnightBotAI spawning is disabled by MidnightBotAI.Spawning = 0");
        else if (sMidnightBotMgr->AutoSpawnEnabled())
            TC_LOG_INFO("server.loading", "MidnightBotAI will auto-spawn {} roster bot(s) 15 seconds after startup", sMidnightBotMgr->Count());
    }

    void OnUpdate(uint32 diff) override
    {
        sMidnightBotMgr->Update(diff);
    }
};

class MidnightBotAIUnitScript : public UnitScript
{
public:
    MidnightBotAIUnitScript() : UnitScript("MidnightBotAIUnitScript") { }

    // Damage-dealt hook (ScriptMgr.h:428, dispatched from Unit::DealDamage at Unit.cpp:847).
    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        sMidnightBotMgr->NotifyBotDamage(attacker, victim, damage);
    }
};

class MidnightBotAICommandScript : public CommandScript
{
public:
    MidnightBotAICommandScript() : CommandScript("MidnightBotAICommandScript") { }

    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable mbotCommandTable =
        {
        { "stats",   HandlembotStats,   rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "loot",    HandlembotLoot,    rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "status",  HandlembotStatus,  rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "st",      HandlembotStatus,  rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "enable",  HandlembotEnable,  rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "disable", HandlembotDisable, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "create",  HandlembotCreate,  rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "cr",      HandlembotCreate,  rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "add",     HandlembotAdd,     rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "ad",      HandlembotAdd,     rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "remove",  HandlembotRemove,  rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "rm",      HandlembotRemove,  rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "list",    HandlembotList,    rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "ls",      HandlembotList,    rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "spawn",   HandlembotSpawn,   rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "sp",      HandlembotSpawn,   rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "spawnall", HandlembotSpawnAll, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "spa",     HandlembotSpawnAll, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "despawn", HandlembotDespawn, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "summon",  HandlembotSummon,  rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "su",      HandlembotSummon,  rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "summonall", HandlembotSummonAll, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "sua",     HandlembotSummonAll, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "goto",    HandlembotGoto,    rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "g",       HandlembotGoto,    rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "where",   HandlembotWhere,   rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "w",       HandlembotWhere,   rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "vis",     HandlembotVis,     rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "v",       HandlembotVis,     rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "party",   HandlembotParty,   rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "p",       HandlembotParty,   rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "partyall", HandlembotPartyAll, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "pa",      HandlembotPartyAll, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "lead",    HandlembotLead,    rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "follow",  HandlembotFollow,  rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "f",       HandlembotFollow,  rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "stay",    HandlembotStay,    rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "s",       HandlembotStay,    rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "come",    HandlembotCome,    rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "c",       HandlembotCome,    rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "assist",  HandlembotAssist,  rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "a",       HandlembotAssist,  rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "unassist", HandlembotUnassist, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "u",       HandlembotUnassist, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "defend",  HandlembotAssist,  rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "attack",  HandlembotAttack,  rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "atk",     HandlembotAttack,  rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "role",    HandlembotRole,    rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "r",       HandlembotRole,    rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "resurrect", HandlembotResurrect, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "res",     HandlembotResurrect, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "learn",   HandlembotLearn,   rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "boost",   HandlembotBoost,   rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "boostall", HandlembotBoostAll, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "debug",   HandlembotDebug,   rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        { "d",       HandlembotDebug,   rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        };

        static ChatCommandTable commandTable =
        {
            { "mbot", mbotCommandTable },
            { "mb", mbotCommandTable },
        };

        return commandTable;
    }

    static bool HandlembotStats(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            handler->SendSysMessage("need an in-game player");
            return true;
        }

        handler->SendSysMessage("[MC] --- Bot damage (60s) ---");

        std::vector<std::string> lines = sMidnightBotMgr->DamageReportLines();
        if (lines.empty())
        {
            handler->SendSysMessage("[MC] no active bots");
            return true;
        }

        for (std::string const& line : lines)
            handler->SendSysMessage(line);

        return true;
    }

    static bool HandlembotLoot(ChatHandler* handler, Optional<std::string> arg1, Optional<std::string> arg2)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            handler->SendSysMessage("need an in-game player");
            return true;
        }

        if (arg1)
        {
            std::string first = *arg1;
            std::transform(first.begin(), first.end(), first.begin(), [](unsigned char c) { return char(std::tolower(c)); });

            std::string err;

            if (first == "threshold")
            {
                if (!arg2)
                {
                    handler->SendSysMessage("usage: .mb loot threshold <poor|common|uncommon|rare|epic|legendary>");
                    return true;
                }

                if (!sMidnightBotMgr->LootSetThreshold(player, *arg2, err))
                    handler->PSendSysMessage("loot: %s", err.c_str());
                else
                    handler->PSendSysMessage("loot threshold set to %s.", arg2->c_str());

                return true;
            }

            if (!sMidnightBotMgr->LootSetMethod(player, first, err))
                handler->PSendSysMessage("loot: %s", err.c_str());
            else
                handler->PSendSysMessage("loot method set to %s.", first.c_str());

            return true;
        }

        std::string out;
        std::string err;
        if (!sMidnightBotMgr->LootShow(player, out, err))
        {
            handler->PSendSysMessage("loot: %s", err.c_str());
            return true;
        }

        handler->PSendSysMessage("%s", out.c_str());
        return true;
    }

    static bool HandlembotStatus(ChatHandler* handler)
    {
        handler->PSendSysMessage("MidnightBotAI v%s - enabled: %s, spawning: %s, autospawn: %s, summon-on-spawn: %s, auto-party: %s, roster: %u/%u, active bots: %u",
            MidnightBotAIVersion, sMidnightBotMgr->IsEnabled() ? "yes" : "no",
            sMidnightBotMgr->SpawningEnabled() ? "on" : "off", sMidnightBotMgr->AutoSpawnEnabled() ? "on" : "off",
            sMidnightBotMgr->SummonOnSpawnEnabled() ? "on" : "off", sMidnightBotMgr->AutoPartyEnabled() ? "on" : "off",
            uint32(sMidnightBotMgr->Count()), sMidnightBotMgr->MaxBots(), uint32(sMidnightBotMgr->ActiveCount()));

        std::vector<MidnightBotMgr::ActiveBotInfo> infos = sMidnightBotMgr->ListActiveBotInfo(handler->GetPlayer());
        if (infos.empty())
        {
            handler->SendSysMessage("  no active bots.");
        }
        else
        {
            for (MidnightBotMgr::ActiveBotInfo const& info : infos)
                PrintBotInfo(handler, info);
        }

        handler->SendSysMessage("cmds: f s c a u atk su g p pa sp spa sua w st d ad rm ls cr res learn boost boostall lead vis stats loot (long names work too, name optional or 'all')");
        return true;
    }

    static bool HandlembotEnable(ChatHandler* handler)
    {
        sMidnightBotMgr->SetRuntimeEnabled(true);
        handler->SendSysMessage("MidnightBotAI enabled.");
        return true;
    }

    static bool HandlembotDisable(ChatHandler* handler)
    {
        sMidnightBotMgr->SetRuntimeEnabled(false);
        handler->SendSysMessage("MidnightBotAI disabled.");
        return true;
    }

    static bool HandlembotCreate(ChatHandler* handler, std::string name, Optional<uint8> race, Optional<uint8> classId)
    {
        uint8 const raceId = race.value_or(RACE_HUMAN);
        uint8 const classValue = classId.value_or(CLASS_WARRIOR);

        std::string err;
        uint32 guid = 0;
        if (!sMidnightBotMgr->CreateBot(name, raceId, classValue, err, guid))
        {
            handler->PSendSysMessage("MidnightBotAI: could not create '%s': %s.", name.c_str(), err.c_str());
            return true;
        }

        std::string accountName = sMidnightBotMgr->AccountPrefix() + "_" + name;
        Utf8ToUpperOnlyLatin(accountName);

        handler->PSendSysMessage("MidnightBotAI: created '%s' (guid %u) on account '%s' (password '%s', race %u, class %u).",
            name.c_str(), guid, accountName.c_str(), name.c_str(), uint32(raceId), uint32(classValue));
        handler->PSendSysMessage("MidnightBotAI: use '.mbot add %s' then '.mbot spawn %s' to log it in.", name.c_str(), name.c_str());
        return true;
    }

    static bool HandlembotAdd(ChatHandler* handler, std::string name)
    {
        std::string err;
        if (!sMidnightBotMgr->AddBotByName(name, err))
        {
            handler->PSendSysMessage("MidnightBotAI: could not add '%s': %s.", name.c_str(), err.c_str());
            return true;
        }

        handler->PSendSysMessage("MidnightBotAI: added '%s' to roster (%u/%u).", name.c_str(),
            uint32(sMidnightBotMgr->Count()), sMidnightBotMgr->MaxBots());
        return true;
    }

    static bool HandlembotRemove(ChatHandler* handler, std::string name)
    {
        if (!sMidnightBotMgr->RemoveBotByName(name))
        {
            handler->PSendSysMessage("MidnightBotAI: '%s' is not in the roster.", name.c_str());
            return true;
        }

        handler->PSendSysMessage("MidnightBotAI: removed '%s' from roster (%u left).", name.c_str(), uint32(sMidnightBotMgr->Count()));
        return true;
    }

    static bool HandlembotList(ChatHandler* handler)
    {
        std::vector<std::string> bots = sMidnightBotMgr->ListBots();
        if (bots.empty())
        {
            handler->SendSysMessage("MidnightBotAI: roster is empty.");
            return true;
        }

        handler->PSendSysMessage("MidnightBotAI roster (%u/%u):", uint32(bots.size()), sMidnightBotMgr->MaxBots());
        uint32 index = 1;
        for (std::string const& name : bots)
            handler->PSendSysMessage("  %u. %s", index++, name.c_str());

        return true;
    }

    static bool HandlembotSpawn(ChatHandler* handler, std::string name)
    {
        Player* player = handler->GetPlayer();
        ObjectGuid ownerGuid = player ? player->GetGUID() : ObjectGuid::Empty;

        std::string err;
        if (!sMidnightBotMgr->SpawnBot(name, err, ownerGuid))
        {
            handler->PSendSysMessage("MidnightBotAI: could not spawn '%s': %s.", name.c_str(), err.c_str());
            return true;
        }

        handler->PSendSysMessage("MidnightBotAI: spawned '%s' (active bots: %u).", name.c_str(), uint32(sMidnightBotMgr->ActiveCount()));

        if (!ownerGuid.IsEmpty())
        {
            if (sMidnightBotMgr->SummonOnSpawnEnabled())
                handler->PSendSysMessage("MidnightBotAI: SummonOnSpawn is enabled; '%s' has been moved to your position.", name.c_str());
            if (sMidnightBotMgr->AutoPartyEnabled())
                handler->PSendSysMessage("MidnightBotAI: AutoParty is enabled; '%s' has been added to your party.", name.c_str());
        }

        return true;
    }

    static bool HandlembotSpawnAll(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        ObjectGuid ownerGuid = player ? player->GetGUID() : ObjectGuid::Empty;

        std::vector<std::string> roster = sMidnightBotMgr->ListBots();
        if (roster.empty())
        {
            handler->SendSysMessage("MidnightBotAI: roster is empty.");
            return true;
        }

        uint32 spawned = 0;
        uint32 skipped = 0;
        uint32 failed = 0;

        for (std::string const& name : roster)
        {
            if (sMidnightBotMgr->IsBotActive(name))
            {
                ++skipped;
                continue;
            }

            std::string err;
            if (sMidnightBotMgr->SpawnBot(name, err, ownerGuid))
            {
                ++spawned;
                handler->PSendSysMessage("MidnightBotAI: spawned '%s'.", name.c_str());
            }
            else
            {
                ++failed;
                handler->PSendSysMessage("MidnightBotAI: failed '%s': %s.", name.c_str(), err.c_str());
            }
        }

        handler->PSendSysMessage("MidnightBotAI: spawnall finished - spawned %u, skipped %u (already active), failed %u.",
            spawned, skipped, failed);
        return true;
    }

    static bool HandlembotDespawn(ChatHandler* handler, std::string name)
    {
        std::string err;
        if (!sMidnightBotMgr->DespawnBot(name, err))
        {
            handler->PSendSysMessage("MidnightBotAI: could not despawn '%s': %s.", name.c_str(), err.c_str());
            return true;
        }

        handler->PSendSysMessage("MidnightBotAI: despawned '%s' (active bots: %u).", name.c_str(), uint32(sMidnightBotMgr->ActiveCount()));
        return true;
    }

    static bool HandlembotSummon(ChatHandler* handler, Optional<std::string> name)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            handler->SendSysMessage("need an in-game player");
            return true;
        }

        ApplyToActiveBots(handler, name, "summon", [&](std::string const& botName, std::string& err)
        {
            return sMidnightBotMgr->SummonBot(botName, player, err);
        });
        return true;
    }

    static bool HandlembotSummonAll(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            handler->SendSysMessage("'.mbot summonall' needs an in-game player to summon to.");
            return true;
        }

        uint32 moved = 0;
        uint32 failed = 0;
        if (!sMidnightBotMgr->SummonAllBots(player, moved, failed))
        {
            handler->SendSysMessage("'.mbot summonall' needs an in-game player to summon to.");
            return true;
        }

        handler->PSendSysMessage("MidnightBotAI: summonall finished - moved %u, failed %u.", moved, failed);
        return true;
    }

    static bool HandlembotGoto(ChatHandler* handler, std::string name)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            handler->SendSysMessage("MidnightBotAI: '.mbot goto' needs an in-game player to teleport.");
            return true;
        }

        MidnightBotMgr::ActiveBotInfo info;
        std::string err;
        if (!sMidnightBotMgr->GetActiveBotInfo(name, player, info, err))
        {
            handler->PSendSysMessage("MidnightBotAI: could not find active bot '%s': %s.", name.c_str(), err.c_str());
            return true;
        }

        if (!sMidnightBotMgr->GotoBot(name, player, err))
        {
            handler->PSendSysMessage("MidnightBotAI: could not teleport to '%s': %s.", name.c_str(), err.c_str());
            return true;
        }

        if (info.distanceToViewer >= 0.0f)
            handler->PSendSysMessage("MidnightBotAI: teleporting you to '%s' (map %u, distance was %.1f yd).", name.c_str(), info.mapId, info.distanceToViewer);
        else
            handler->PSendSysMessage("MidnightBotAI: teleporting you to '%s' (map %u).", name.c_str(), info.mapId);

        return true;
    }

    static bool HandlembotWhere(ChatHandler* handler, Optional<std::string> name)
    {
        Player* viewer = handler->GetPlayer();

        if (name && !TokenIsAll(*name))
        {
            MidnightBotMgr::ActiveBotInfo info;
            std::string err;
            if (!sMidnightBotMgr->GetActiveBotInfo(*name, viewer, info, err))
            {
                handler->PSendSysMessage("%s: where failed - %s", name->c_str(), err.c_str());
                return true;
            }

            PrintBotInfo(handler, info);
            return true;
        }

        std::vector<MidnightBotMgr::ActiveBotInfo> infos = sMidnightBotMgr->ListActiveBotInfo(viewer);
        if (infos.empty())
        {
            handler->SendSysMessage("all: where - no active bots");
            return true;
        }

        for (MidnightBotMgr::ActiveBotInfo const& info : infos)
            PrintBotInfo(handler, info);

        return true;
    }

    static bool HandlembotVis(ChatHandler* handler, Optional<std::string> name)
    {
        std::vector<std::string> names;
        if (name && !TokenIsAll(*name))
            names.push_back(*name);
        else
            names = sMidnightBotMgr->ActiveBotNames();

        if (names.empty())
        {
            handler->SendSysMessage("vis: no active bots");
            return true;
        }

        for (std::string const& botName : names)
        {
            std::string err;
            std::vector<std::string> lines = sMidnightBotMgr->VisibilityReport(botName, handler->GetPlayer(), err);
            if (lines.empty())
            {
                handler->PSendSysMessage("%s: vis failed - %s", botName.c_str(), err.c_str());
                continue;
            }

            handler->PSendSysMessage("%s: visibility report (one line per check):", botName.c_str());
            for (std::string const& line : lines)
                handler->SendSysMessage(line);
        }

        return true;
    }

    static bool HandlembotParty(ChatHandler* handler, std::string name)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            handler->SendSysMessage("MidnightBotAI: '.mbot party' needs an in-game player.");
            return true;
        }

        std::string err;
        if (!sMidnightBotMgr->AddBotToGroup(name, player, err))
        {
            handler->PSendSysMessage("MidnightBotAI: could not add '%s' to your party: %s.", name.c_str(), err.c_str());
            return true;
        }

        handler->PSendSysMessage("MidnightBotAI: added '%s' to your party.", name.c_str());
        return true;
    }

    static bool HandlembotPartyAll(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            handler->SendSysMessage("MidnightBotAI: '.mbot partyall' needs an in-game player.");
            return true;
        }

        uint32 added = 0;
        uint32 alreadyGrouped = 0;
        uint32 failed = 0;
        if (!sMidnightBotMgr->PartyAllBots(player, added, alreadyGrouped, failed))
        {
            handler->SendSysMessage("MidnightBotAI: '.mbot partyall' needs an in-game player.");
            return true;
        }

        handler->PSendSysMessage("MidnightBotAI: partyall finished - added %u, already grouped %u, failed %u.", added, alreadyGrouped, failed);
        return true;
    }

    static bool HandlembotFollow(ChatHandler* handler, Optional<std::string> name)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            handler->SendSysMessage("need an in-game player");
            return true;
        }

        ApplyToActiveBots(handler, name, "follow on", [&](std::string const& botName, std::string& err)
        {
            return sMidnightBotMgr->SetBotFollow(botName, true, player, err);
        });
        return true;
    }

    static bool HandlembotStay(ChatHandler* handler, Optional<std::string> name)
    {
        Player* player = handler->GetPlayer();

        ApplyToActiveBots(handler, name, "stay", [&](std::string const& botName, std::string& err)
        {
            return sMidnightBotMgr->SetBotFollow(botName, false, player, err);
        });
        return true;
    }

    static bool HandlembotCome(ChatHandler* handler, Optional<std::string> name)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            handler->SendSysMessage("need an in-game player");
            return true;
        }

        ApplyToActiveBots(handler, name, "come", [&](std::string const& botName, std::string& err)
        {
            return sMidnightBotMgr->ComeBot(botName, player, err);
        });
        return true;
    }

    static bool HandlembotAttack(ChatHandler* handler, Optional<std::string> name)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            handler->SendSysMessage("need an in-game player");
            return true;
        }

        Unit* target = player->GetSelectedUnit();
        if (!target)
        {
            handler->SendSysMessage("no target selected");
            return true;
        }

        std::string label = std::string("attack '") + target->GetName() + "'";
        ApplyToActiveBots(handler, name, label, [&](std::string const& botName, std::string& err)
        {
            return sMidnightBotMgr->BotAttackTarget(botName, player, target, err);
        });
        return true;
    }

    static bool HandlembotAssist(ChatHandler* handler, Optional<std::string> name, Optional<bool> on)
    {
        Optional<std::string> targetName = name;
        bool enable = on.value_or(true);

        // Allow '.mb a off' / '.mb a on' without a bot name (the first optional string eats the token).
        if (!on && name && !TokenIsAll(*name))
        {
            std::string token = *name;
            std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) { return char(std::tolower(c)); });

            if (token == "on")
            {
                enable = true;
                targetName.reset();
            }
            else if (token == "off")
            {
                enable = false;
                targetName.reset();
            }
        }

        ApplyToActiveBots(handler, targetName, enable ? "assist on" : "assist off", [&](std::string const& botName, std::string& err)
        {
            return sMidnightBotMgr->SetBotAssist(botName, enable, handler->GetPlayer(), err);
        });
        return true;
    }

    static bool HandlembotUnassist(ChatHandler* handler, Optional<std::string> name)
    {
        ApplyToActiveBots(handler, name, "assist off", [&](std::string const& botName, std::string& err)
        {
            return sMidnightBotMgr->SetBotAssist(botName, false, handler->GetPlayer(), err);
        });
        return true;
    }

    static bool HandlembotRole(ChatHandler* handler, std::string first, Optional<std::string> second)
    {
        Optional<std::string> targetName;
        std::string roleText;

        if (second)
        {
            targetName = first;     // '.mb r <name> <role>'
            roleText = *second;
        }
        else
        {
            roleText = first;       // '.mb r <role>' -> all active bots
        }

        BotRole role;
        if (!ParseBotRole(roleText, role))
        {
            handler->PSendSysMessage("invalid role '%s' (tank/healer/dps/none)", roleText.c_str());
            return true;
        }

        ApplyToActiveBots(handler, targetName, "role " + roleText, [&](std::string const& botName, std::string& err)
        {
            return sMidnightBotMgr->SetBotRole(botName, role, err);
        });
        return true;
    }

    static bool HandlembotLead(ChatHandler* handler, Optional<std::string> name)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            handler->SendSysMessage("need an in-game player");
            return true;
        }

        std::vector<std::string> names;
        if (name && !TokenIsAll(*name))
            names.push_back(*name);
        else
            names = sMidnightBotMgr->ActiveBotNames();

        if (names.empty())
        {
            handler->SendSysMessage("lead failed - no active bots");
            return true;
        }

        uint32 botsUnder = 0;
        uint32 failed = 0;
        std::string firstErr;

        for (std::string const& botName : names)
        {
            std::string err;
            uint32 count = 0;
            if (sMidnightBotMgr->TakeGroupLead(botName, player, count, err))
                botsUnder = std::max(botsUnder, count);
            else
            {
                ++failed;
                if (firstErr.empty())
                    firstErr = err;
            }
        }

        if (botsUnder)
            handler->PSendSysMessage("you are now the group leader (%u bots)", botsUnder);
        else if (failed)
            handler->PSendSysMessage("lead failed - %s", firstErr.c_str());
        else
            handler->SendSysMessage("you are already the group leader");

        return true;
    }

    static bool HandlembotResurrect(ChatHandler* handler, Optional<std::string> name)
    {
        ApplyToActiveBots(handler, name, "resurrected", [&](std::string const& botName, std::string& err)
        {
            return sMidnightBotMgr->ResurrectBot(botName, err);
        });
        return true;
    }

    static bool HandlembotLearn(ChatHandler* handler, Optional<std::string> name)
    {
        if (name && !TokenIsAll(*name))
        {
            std::string err;
            uint32 learned = sMidnightBotMgr->LearnBotSpells(*name, err);
            if (!err.empty())
                handler->PSendSysMessage("%s: learn failed - %s", name->c_str(), err.c_str());
            else
                handler->PSendSysMessage("%s: learned %u spell(s)", name->c_str(), learned);

            return true;
        }

        std::vector<std::string> names = sMidnightBotMgr->ActiveBotNames();
        if (names.empty())
        {
            handler->SendSysMessage("all: learned 0 spell(s) - no active bots");
            return true;
        }

        uint32 total = 0;
        for (std::string const& botName : names)
        {
            std::string err;
            total += sMidnightBotMgr->LearnBotSpells(botName, err);
        }

        handler->PSendSysMessage("all: learned %u spell(s)", total);
        return true;
    }

    static bool HandlembotBoost(ChatHandler* handler, Optional<std::string> name)
    {
        Player* player = handler->GetPlayer();
        uint8 targetLevel = 10; // fallback when there is no owner/GM
        if (player && player->GetLevel() >= 1)
            targetLevel = player->GetLevel();

        // No argument or 'all' -> every active bot; otherwise the single named bot.
        std::vector<std::string> names;
        if (name && !TokenIsAll(*name))
            names.push_back(*name);
        else
            names = sMidnightBotMgr->ActiveBotNames();

        if (names.empty())
        {
            handler->SendSysMessage("boost: no active bots");
            return true;
        }

        uint32 ok = 0;
        uint32 failed = 0;
        std::string firstErr;

        for (std::string const& botName : names)
        {
            std::string err;
            if (sMidnightBotMgr->BoostBot(botName, player, targetLevel, err))
                ++ok;
            else
            {
                ++failed;
                if (firstErr.empty())
                    firstErr = err;
            }
        }

        if (names.size() == 1 && !failed)
            handler->PSendSysMessage("%s: boosted to level %u", names.front().c_str(), uint32(targetLevel));
        else if (failed)
            handler->PSendSysMessage("boost: %u ok, %u failed - %s", ok, failed, firstErr.c_str());
        else
            handler->PSendSysMessage("all: boosted to level %u (%u bots)", uint32(targetLevel), ok);

        return true;
    }

    static bool HandlembotBoostAll(ChatHandler* handler)
    {
        return HandlembotBoost(handler, std::nullopt);
    }

    static bool HandlembotDebug(ChatHandler* handler, Optional<std::string> name)
    {
        Player* player = handler->GetPlayer();
        std::string const issuer = player ? player->GetName() : std::string("Console");

        std::string onlyBot;
        if (name && !TokenIsAll(*name))
            onlyBot = *name;

        std::string outPath;
        std::string err;
        if (!sMidnightBotMgr->DumpDebug(MidnightBotAIVersion, issuer, onlyBot, outPath, err))
        {
            handler->PSendSysMessage("could not write debug dump: %s", err.c_str());
            return true;
        }

        handler->PSendSysMessage("debug written to '%s'", outPath.c_str());
        return true;
    }
};

void AddSC_midnight_bot_ai()
{
    new MidnightBotAIWorldScript();
    new MidnightBotAIUnitScript();
    new MidnightBotAICommandScript();
}
