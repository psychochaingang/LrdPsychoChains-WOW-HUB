/*
 * LegionBotAI.cpp
 * LegionBotAI - real player-character bots for LegionCore (7.3.5)
 *
 * Spawns real characters (Player objects with a session) into the world,
 * driven server-side without a network client.
 *
 * Chunk 1: spawn a character from the DB as a bot + dismiss it.
 * (Group integration and AI come in later chunks.)
 */

#include "ScriptMgr.h"
#include "CellImpl.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "Group.h"
#include "GroupMgr.h"
#include "InstanceSaveMgr.h"
#include "LoginQueryHolder.h"
#include "LFGMgr.h"
#include "Log.h"
#include "LootMgr.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellMgr.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    enum LegionBotRole : uint8
    {
        LB_ROLE_TANK   = 0,
        LB_ROLE_HEALER = 1,
        LB_ROLE_DPS    = 2
    };

    std::map<ObjectGuid, std::vector<ObjectGuid>> g_legionBots;
    std::map<ObjectGuid, WorldSessionPtr> g_legionBotSessions;   // keeps bot sessions alive without registering them in the world session loop
    std::map<ObjectGuid, uint8> g_legionBotRoles;                // bot guid -> role
    std::map<ObjectGuid, ObjectGuid> g_legionBotLastTarget;      // bot guid -> last attacked creature (for looting)
    std::map<ObjectGuid, uint8> g_legionBotLfgState;             // bot guid -> last seen LFG state (debug)
    std::map<ObjectGuid, uint32> g_legionBotPotionTimer;         // bot guid -> last potion use (ms)
    std::map<ObjectGuid, uint32> g_legionBotLastCast;            // bot guid -> last attempted spell
    std::map<ObjectGuid, uint32> g_legionBotLastCastTime;        // bot guid -> ms of last cast attempt
    std::map<ObjectGuid, uint8> g_legionBotSlot;                 // bot guid -> formation slot (0..3)
    std::map<ObjectGuid, std::map<uint32, uint32>> g_legionBotSpellCooldowns; // bot guid -> spell -> next allowed (ms)
std::set<ObjectGuid> g_legionPlayerAi;                       // players with self-AI enabled
std::mutex g_legionBotsMutex;

// --- Level / autogear state (playerbot-style leveling) ---
std::map<ObjectGuid, uint32> g_legionBotLastLevelCheck;      // owner guid -> ms of last level sync
std::map<uint32, std::vector<uint32>> g_legionBotGearCache;  // race*100000+class*1000+level -> item entries
std::map<ObjectGuid, std::pair<ObjectGuid, uint32>> g_legionBotTargetSince; // bot guid -> (target guid, ms engaged)

// Bots wait this long after picking a new target before attacking, so the
// owner always gets the first swing in (playerbot-style "let the player pull").
uint32 const LB_ENGAGE_DELAY_MS = 1200;

// Bots only engage targets this close to the owner - they fight WITH you,
// never run across the map after something you tagged from range.
float const LB_MAX_ENGAGE_DISTANCE = 30.0f;

    // Dungeon consumables (Mists of Pandaria)
    uint32 const LB_HEALTH_POTION_ID = 76097;   // Master Healing Potion
    uint32 const LB_MANA_POTION_ID   = 76098;   // Master Mana Potion

    void UnregisterBotSession(ObjectGuid botGuid)
    {
        std::lock_guard<std::mutex> lock(g_legionBotsMutex);
        g_legionBotSessions.erase(botGuid);
        g_legionBotRoles.erase(botGuid);
        g_legionBotLastTarget.erase(botGuid);
        g_legionBotPotionTimer.erase(botGuid);
        g_legionBotLastCast.erase(botGuid);
        g_legionBotLastCastTime.erase(botGuid);
        g_legionBotSlot.erase(botGuid);
        g_legionBotSpellCooldowns.erase(botGuid);
    }

    uint8 GetBotRole(ObjectGuid botGuid)
    {
        std::lock_guard<std::mutex> lock(g_legionBotsMutex);
        auto itr = g_legionBotRoles.find(botGuid);
        return itr != g_legionBotRoles.end() ? itr->second : 255;
    }

    // Formation offsets: distance behind the owner + lateral spread (radians).
    // roleIndex = this bot's index among bots with the same role (spreads duplicates apart).
    void GetFormationOffset(uint8 role, uint8 slot, uint8 roleIndex, float& dist, float& lateral)
    {
        switch (role)
        {
            case LB_ROLE_HEALER:
                dist = 9.0f;
                lateral = roleIndex ? 0.7f : 0.0f;
                break;
            case LB_ROLE_DPS:
                dist = 4.0f;
                lateral = (roleIndex % 2) ? -0.8f : 0.8f;
                break;
            default: // tank
                dist = 2.5f;
                lateral = roleIndex ? 0.8f : 0.0f;
                break;
        }
    }

    // World position of a bot's formation slot (behind the owner, spread out).
    // Snaps to the ground (vmaps/mmaps now work) so bots don't float on ledges.
    Position GetFormationPosition(Player* owner, uint8 role, uint8 slot, uint8 roleIndex)
    {
        float dist = 2.5f;
        float lateral = 0.0f;
        GetFormationOffset(role, slot, roleIndex, dist, lateral);

        Position pos;
        owner->GetPosition(&pos);

        float const angle = owner->GetOrientation() + float(M_PI) + lateral;
        pos.m_positionX += dist * std::cos(angle);
        pos.m_positionY += dist * std::sin(angle);

        float groundZ = owner->GetPositionZ();
        owner->UpdateGroundPositionZ(pos.m_positionX, pos.m_positionY, groundZ);
        pos.m_positionZ = groundZ;
        return pos;
    }

    // Class gear sets (Mists of Pandaria crafted, ilvl 384, level 85+)
    std::vector<uint32> GetBotGear(uint8 cls)
    {
        switch (cls)
        {
            case CLASS_PALADIN:
                // Intellect plate + 1H hammer + shield
                return { 82911, 82912, 82913, 82914, 82915, 82916, 82917, 82918, 82965, 82961 };
            case CLASS_ROGUE:
                // Agility leather (Stormscale) + dagger
                return { 85846, 85848, 85844, 85845, 85847, 85843, 85842, 85841, 82967 };
            case CLASS_PRIEST:
                // Intellect cloth (Windwool) + intellect 1H mace
                return { 82397, 82398, 82399, 82400, 82401, 82402, 82403, 82404, 82965 };
            default:
                // Strength plate (Ghost-Forged) + 2H sword: DK tank/dps, warrior
                return { 82903, 82904, 82905, 82906, 82907, 82908, 82909, 82910, 82964 };
        }
    }

    // Legion 7.3.5 ability kits per class
    std::vector<uint32> GetBotSpells(uint8 cls)
    {
        switch (cls)
        {
            case CLASS_DEATH_KNIGHT: return { 49998, 195182, 49143, 49020, 56222, 50842, 49576, 206930, 55233, 49028, 48792, 48707, 205223, 48263, 48266, 57330, 49184 }; // + passives, Horn of Winter, Howling Blast
            case CLASS_PALADIN:      return { 19750, 20473, 35395, 82326, 223306, 20271, 31821, 633, 20217, 19740 };  // + Lay on Hands, Blessing of Kings/Might
            case CLASS_ROGUE:        return { 53, 196819 };                         // Backstab, Eviscerate
            case CLASS_WARRIOR:      return { 23881, 85288, 184367, 100, 355, 1719, 184364, 280735, 118000, 190411, 6673, 1160 }; // + Battle Shout, Demoralizing Shout
            case CLASS_PRIEST:       return { 2061, 139, 17, 2060, 585, 2050, 33076, 47788, 14914, 21562, 589 }; // + Power Word: Fortitude, Shadow Word: Pain
            default:                 return {};
        }
    }

    void LearnBotSpells(Player* bot)
    {
        for (uint32 spellId : GetBotSpells(bot->getClass()))
            if (!bot->HasSpell(spellId))
                bot->addSpell(spellId, true, false, false, false);
    }

    // ------------------------------------------------------------------
    // Level / autogear (playerbot-style: bots level with the player and
    // auto-equip gear that fits their level - basic starting gear at 1)
    // ------------------------------------------------------------------

    enum LegionBotLevelMode : uint8
    {
        LB_LEVEL_SYNC  = 0,   // bots follow the owner's level
        LB_LEVEL_MAX   = 1,   // bots at max level
        LB_LEVEL_FIXED = 2    // bots at a specific level
    };

    enum LegionBotAssistMode : uint8
    {
        LB_ASSIST_FULL   = 0, // bots fight everything you fight (default)
        LB_ASSIST_DEFEND = 1, // bots only fight mobs that attack you
        LB_ASSIST_CHILL  = 2  // bots never attack (follow/heal/buff only)
    };

    struct LegionBotSettings
    {
        uint8 levelMode  = LB_LEVEL_SYNC;
        uint8 fixedLevel = 1;
        bool  playerTank = false;   // true = the PLAYER holds aggro (bots don't taunt/boost threat)
        uint8 assistMode = LB_ASSIST_FULL;
        bool  loaded     = false;
    };

    std::map<ObjectGuid, LegionBotSettings> g_legionBotSettings;

    void EnsureLegionBotSettingsTable()
    {
        static bool created = false;
        if (created)
            return;
        created = true;
        // DirectExecute = synchronous: the table must exist before the first SELECT below
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `character_legionbot_settings` ("
            "`guid` INT UNSIGNED NOT NULL,"
            "`level_mode` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "`fixed_level` TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "`player_tank` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "`assist_mode` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "PRIMARY KEY (`guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
        // Upgrade existing installs (columns added after the table was first created)
        CharacterDatabase.DirectExecute(
            "ALTER TABLE `character_legionbot_settings` ADD COLUMN IF NOT EXISTS "
            "`player_tank` TINYINT UNSIGNED NOT NULL DEFAULT 0");
        CharacterDatabase.DirectExecute(
            "ALTER TABLE `character_legionbot_settings` ADD COLUMN IF NOT EXISTS "
            "`assist_mode` TINYINT UNSIGNED NOT NULL DEFAULT 0");
    }

    LegionBotSettings& GetLegionBotSettings(Player* owner)
    {
        LegionBotSettings& s = g_legionBotSettings[owner->GetGUID()];
        if (!s.loaded)
        {
            s.loaded = true;
            EnsureLegionBotSettingsTable();
            if (QueryResult r = CharacterDatabase.PQuery(
                    "SELECT level_mode, fixed_level, player_tank, assist_mode FROM character_legionbot_settings WHERE guid = %u",
                    owner->GetGUID().GetCounter()))
            {
                Field* f = r->Fetch();
                s.levelMode = f[0].GetUInt8();
                s.fixedLevel = f[1].GetUInt8();
                s.playerTank = f[2].GetUInt8() != 0;
                s.assistMode = f[3].GetUInt8();
                if (s.levelMode > LB_LEVEL_FIXED)
                    s.levelMode = LB_LEVEL_SYNC;
                if (s.fixedLevel < 1)
                    s.fixedLevel = 1;
                if (s.assistMode > LB_ASSIST_CHILL)
                    s.assistMode = LB_ASSIST_FULL;
            }
        }
        return s;
    }

    void SaveLegionBotSettings(Player* owner)
    {
        LegionBotSettings const& s = GetLegionBotSettings(owner);
        EnsureLegionBotSettingsTable();
        CharacterDatabase.PExecute(
            "INSERT INTO character_legionbot_settings (guid, level_mode, fixed_level, player_tank, assist_mode) VALUES (%u, %u, %u, %u, %u) "
            "ON DUPLICATE KEY UPDATE level_mode = VALUES(level_mode), fixed_level = VALUES(fixed_level), player_tank = VALUES(player_tank), assist_mode = VALUES(assist_mode)",
            owner->GetGUID().GetCounter(), uint32(s.levelMode), uint32(s.fixedLevel), uint32(s.playerTank ? 1 : 0), uint32(s.assistMode));
    }

    uint8 GetLegionBotTargetLevel(Player* owner)
    {
        LegionBotSettings& s = GetLegionBotSettings(owner);
        switch (s.levelMode)
        {
            case LB_LEVEL_MAX:   return uint8(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
            case LB_LEVEL_FIXED: return s.fixedLevel;
            default:             return owner->getLevel();
        }
    }

    // Natural quality ceiling while leveling (white -> green -> blue)
    uint8 QualityCapForLevel(uint8 level)
    {
        if (level < 10)
            return 1;
        if (level < 25)
            return 2;
        return 3;
    }

    // Best item for one slot at a level, picked from the client DB2 item data
    // (the core's real item source - world.item_template is not what the core loads)
    uint32 PickItemForSlot(Player* bot, uint8 level, uint8 itemClass, uint8 armorSubclass,
                           uint32 weaponSubclassMask, uint8 invType, uint8 /*primaryStat*/,
                           uint8 qualityCap, std::vector<uint32> const& exclude)
    {
        uint32 bestEntry = 0;
        uint16 bestIlvl = 0;
        uint8  bestQuality = 0;

        uint64 const classMask = (bot->getClass() > 0 && bot->getClass() < 64) ? (uint64(1) << (bot->getClass() - 1)) : 0;
        uint64 const raceMask  = (bot->getRace() > 0 && bot->getRace() < 64) ? (uint64(1) << (bot->getRace() - 1)) : 0;

        for (ItemSparseEntry const* sparse : sItemSparseStore)
        {
            ItemEntry const* db2 = sItemStore.LookupEntry(sparse->ID);
            if (!db2)
                continue;

            if (db2->ClassID != itemClass)
                continue;
            if (itemClass == ITEM_CLASS_WEAPON)
            {
                if (db2->SubclassID >= 32 || !(weaponSubclassMask & (1u << db2->SubclassID)))
                    continue;
            }
            else if (db2->SubclassID != armorSubclass)
                continue;
            if (db2->InventoryType != invType)
                continue;

            if (sparse->RequiredLevel > level)
                continue;
            if (sparse->ItemLevel < 1)
                continue;
            if (sparse->OverallQualityID > qualityCap)
                continue;
            if (sparse->OverallQualityID == 7)          // heirloom quality
                continue;
            if (sparse->ScalingStatDistributionID != 0) // scaling items (heirlooms etc.)
                continue;
            if (sparse->MaxCount != 0)
                continue;

            if (sparse->AllowableClass && sparse->AllowableClass != uint16(-1) &&
                !(uint64(sparse->AllowableClass) & classMask))
                continue;
            if (sparse->AllowableRace && sparse->AllowableRace != int64(-1) &&
                !(uint64(sparse->AllowableRace) & raceMask))
                continue;

            if (std::find(exclude.begin(), exclude.end(), sparse->ID) != exclude.end())
                continue;

            if (sparse->ItemLevel > bestIlvl || (sparse->ItemLevel == bestIlvl && sparse->OverallQualityID > bestQuality))
            {
                bestIlvl = sparse->ItemLevel;
                bestQuality = sparse->OverallQualityID;
                bestEntry = sparse->ID;
            }
        }
        return bestEntry;
    }

    // Basic starting items - the exact starting outfit a fresh character gets
    // (CharStartOutfit DB2, the same source the core uses in Player::Create)
    std::vector<uint32> GetStartingGear(Player* bot)
    {
        std::vector<uint32> items;
        for (CharStartOutfitEntry const* entry : sCharStartOutfitStore)
        {
            if (entry->RaceID == bot->getRace() && entry->ClassID == bot->getClass() && entry->SexID == bot->getGender())
            {
                for (int j = 0; j < MAX_OUTFIT_ITEMS; ++j)
                {
                    if (entry->ItemID[j] <= 0)
                        continue;
                    uint32 const itemId = uint32(entry->ItemID[j]);
                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                    if (!proto)
                        continue;
                    // gear only (skip bags, food, potions, ...)
                    if (proto->GetClass() != ITEM_CLASS_ARMOR && proto->GetClass() != ITEM_CLASS_WEAPON)
                        continue;
                    if (proto->GetInventoryType() == 0)
                        continue;
                    items.push_back(itemId);
                }
                break;
            }
        }
        return items;
    }

    // Level-appropriate gear picked from the item DB (class/armor/weapon aware)
    std::vector<uint32> PickGearForLevel(Player* bot, uint8 level)
    {
        uint32 const cacheKey = uint32(bot->getRace()) * 100000 + uint32(bot->getClass()) * 1000 + level;
        auto cached = g_legionBotGearCache.find(cacheKey);
        if (cached != g_legionBotGearCache.end())
            return cached->second;

        uint8 armorSubclass = ITEM_SUBCLASS_ARMOR_CLOTH;
        uint8 primaryStat   = ITEM_MOD_INTELLECT;
        uint32 weaponMask   = (1u << 10);   // staff
        uint8 weaponInvType = 17;
        bool  useShield     = false;

        switch (bot->getClass())
        {
            case CLASS_DEATH_KNIGHT:
            case CLASS_WARRIOR:
                armorSubclass = ITEM_SUBCLASS_ARMOR_PLATE;
                primaryStat   = ITEM_MOD_STRENGTH;
                weaponMask    = (1u << 1) | (1u << 5) | (1u << 8) | (1u << 6); // 2H axe/mace/sword, polearm
                weaponInvType = 17;
                break;
            case CLASS_PALADIN:
                armorSubclass = ITEM_SUBCLASS_ARMOR_PLATE;
                primaryStat   = ITEM_MOD_INTELLECT;
                weaponMask    = (1u << 4) | (1u << 7);   // 1H mace/sword
                weaponInvType = 13;
                useShield     = true;
                break;
            case CLASS_ROGUE:
                armorSubclass = ITEM_SUBCLASS_ARMOR_LEATHER;
                primaryStat   = ITEM_MOD_AGILITY;
                weaponMask    = (1u << 15) | (1u << 7);  // dagger, 1H sword
                weaponInvType = 13;
                break;
            default: // priest + anything else
                armorSubclass = ITEM_SUBCLASS_ARMOR_CLOTH;
                primaryStat   = ITEM_MOD_INTELLECT;
                weaponMask    = (1u << 10);              // staff
                weaponInvType = 17;
                break;
        }

        uint8 const cap = QualityCapForLevel(level);
        std::vector<uint32> items;

        // Armor slots: head, neck, shoulders, chest, waist, legs, feet, wrist, hands, back, ring, trinket
        uint8 const armorInvTypes[] = { 1, 2, 3, 5, 6, 7, 8, 9, 10, 16, 11, 12 };
        for (uint8 invType : armorInvTypes)
        {
            uint32 entry = 0;
            for (uint8 c = cap; c <= 4 && !entry; ++c)
            {
                entry = PickItemForSlot(bot, level, ITEM_CLASS_ARMOR, armorSubclass, 0, invType, primaryStat, c, items);
                if (!entry)
                    entry = PickItemForSlot(bot, level, ITEM_CLASS_ARMOR, armorSubclass, 0, invType, 0, c, items);
            }
            if (entry)
                items.push_back(entry);
        }

        // Second ring + second trinket (distinct entries where possible)
        uint8 const extraInvTypes[] = { 11, 12 };
        for (uint8 invType : extraInvTypes)
        {
            uint32 entry = 0;
            for (uint8 c = cap; c <= 4 && !entry; ++c)
                entry = PickItemForSlot(bot, level, ITEM_CLASS_ARMOR, armorSubclass, 0, invType, primaryStat, c, items);
            if (entry)
                items.push_back(entry);
        }

        // Weapon (falls back to no primary-stat filter if nothing matches)
        uint32 weapon = 0;
        for (uint8 c = cap; c <= 4 && !weapon; ++c)
        {
            weapon = PickItemForSlot(bot, level, ITEM_CLASS_WEAPON, 0, weaponMask, weaponInvType, primaryStat, c, items);
            if (!weapon)
                weapon = PickItemForSlot(bot, level, ITEM_CLASS_WEAPON, 0, weaponMask, weaponInvType, 0, c, items);
        }
        if (weapon)
            items.push_back(weapon);

        if (useShield)
        {
            uint32 shield = 0;
            for (uint8 c = cap; c <= 4 && !shield; ++c)
                shield = PickItemForSlot(bot, level, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_SHIELD, 0, 14, 0, c, items);
            if (shield)
                items.push_back(shield);
        }

        g_legionBotGearCache[cacheKey] = items;
        return items;
    }

    // Level-appropriate gear: starting kit at 1, picked by level, max set at cap
    std::vector<uint32> GetBotGearForLevel(Player* bot, uint8 level)
    {
        uint8 const maxLevel = uint8(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
        if (level >= maxLevel)
            return GetBotGear(bot->getClass());
        if (level <= 1)
        {
            // Starting outfit, then fill any gaps with level-1 pickable gear
            std::vector<uint32> items = GetStartingGear(bot);
            for (uint32 itemId : PickGearForLevel(bot, level))
                if (std::find(items.begin(), items.end(), itemId) == items.end())
                    items.push_back(itemId);
            return items;
        }
        return PickGearForLevel(bot, level);
    }

    // Equip the bot with gear that fits its level (replacing any old gear)
    void EquipBotGear(Player* bot, uint8 level)
    {
        // Strip all currently equipped items (handles class changes and stale gear)
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);

        for (uint32 itemId : GetBotGearForLevel(bot, level))
        {
            // Safety: never pass an unknown item id to the inventory code
            // (Item::CreateItem asserts on a missing template)
            if (!sObjectMgr->GetItemTemplate(itemId))
            {
                TC_LOG_ERROR(LOG_FILTER_GENERAL, "PlayerBot: skipping unknown gear item %u for %s", itemId, bot->GetName());
                continue;
            }

            uint16 dest = 0;
            InventoryResult result = bot->CanEquipNewItem(NULL_SLOT, dest, itemId, false, true);
            if (FILE* f = fopen("bot_gear_debug.log", "a"))
            {
                fprintf(f, "%s: item %u -> CanEquip result %u\n", bot->GetName(), itemId, uint32(result));
                fclose(f);
            }
            if (result == EQUIP_ERR_OK)
                bot->EquipNewItem(dest, itemId, true);
        }
    }

    void EquipBotGear(Player* bot)
    {
        EquipBotGear(bot, bot->getLevel());
    }

    // Guarded spell cast with self-managed cooldowns.
    // Uses triggered casts so the bots are not limited to one ability per global
    // cooldown (each ability fires on its own cooldown instead of the shared GCD).
    void BotCast(Player* bot, Unit* target, uint32 spellId)
    {
        if (!bot || !target || !target->isAlive())
            return;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return;

    // Level gate: bots only use abilities their level would have learned
    // (keeps them from firing endgame abilities while leveling)
    if (spellInfo->BaseLevel > bot->getLevel())
        return;

    uint32 now = getMSTime();

        {
            std::lock_guard<std::mutex> lock(g_legionBotsMutex);

            auto& spellMap = g_legionBotSpellCooldowns[bot->GetGUID()];
            auto itr = spellMap.find(spellId);
            if (itr != spellMap.end() && now < itr->second)
                return;

            // Cooldown = the spell's own cooldown, minimum 1.5s per ability
            uint32 cooldown = 1500;
            if (spellInfo->Cooldowns.RecoveryTime > 0)
                cooldown = spellInfo->Cooldowns.RecoveryTime;
            if (spellInfo->Cooldowns.CategoryRecoveryTime > 0 && spellInfo->Cooldowns.CategoryRecoveryTime > cooldown)
                cooldown = spellInfo->Cooldowns.CategoryRecoveryTime;
            spellMap[spellId] = now + cooldown;

            g_legionBotLastCast[bot->GetGUID()] = spellId;
            g_legionBotLastCastTime[bot->GetGUID()] = now;
        }

        // Face the target so melee abilities don't fail their facing check
        bot->SetFacingToObject(target);

        // Triggered cast: ignores the global cooldown and resource costs
        bot->CastSpell(target, spellInfo, true);
    }

    // Class healing ability (paladin / priest)
    void CastHealAbility(Player* healer, Unit* target)
    {
        if (healer->getClass() == CLASS_PRIEST)
        {
            // Emergency save
            if (target->GetHealthPct() < 25.0f && !healer->HasSpellCooldown(47788))
            {
                BotCast(healer, target, 47788);     // Guardian Spirit
                return;
            }
            if (!target->HasAura(6788) && !healer->HasSpellCooldown(17))
            {
                BotCast(healer, target, 17);        // Power Word: Shield
                return;
            }
            if (!healer->HasSpellCooldown(2050))
            {
                BotCast(healer, target, 2050);      // Holy Word: Serenity
                return;
            }
            if (!healer->HasSpellCooldown(33076))
            {
                BotCast(healer, target, 33076);     // Prayer of Mending
                return;
            }
            if (target->GetHealthPct() < 40.0f && !healer->HasSpellCooldown(2060))
                BotCast(healer, target, 2060);      // Heal (big, slower)
            else if (!healer->HasSpellCooldown(2061))
                BotCast(healer, target, 2061);      // Flash Heal (fast)
            else
                BotCast(healer, target, 139);       // Renew
        }
        else
        {
            // Emergency save
            if (target->GetHealthPct() < 25.0f && !healer->HasSpellCooldown(633))
            {
                BotCast(healer, target, 633);       // Lay on Hands
                return;
            }
            if (!healer->HasSpellCooldown(20473))
            {
                BotCast(healer, target, 20473);     // Holy Shock
                return;
            }
            if (!healer->HasSpellCooldown(223306))
            {
                BotCast(healer, target, 223306);    // Bestow Faith
                return;
            }
            if (target->GetHealthPct() < 40.0f && !healer->HasSpellCooldown(82326))
                BotCast(healer, target, 82326);     // Holy Light
            else
                BotCast(healer, target, 19750);     // Flash of Light
        }
    }

    // Keep the whole party buffed with this bot's class buffs (out of combat)
    void BuffParty(Player* owner, std::vector<ObjectGuid> const& botGuids, Player* bot)
    {
        if (!owner || !bot)
            return;
        if (bot->isInCombat() || owner->isInCombat())
            return;

        std::vector<Player*> party;
        party.push_back(owner);
        for (ObjectGuid guid : botGuids)
            if (Player* mate = ObjectAccessor::FindPlayer(guid))
                party.push_back(mate);

        switch (bot->getClass())
        {
            case CLASS_PALADIN:
                for (Player* member : party)
                {
                    if (!member->isAlive())
                        continue;
                    if (!member->HasAura(20217))
                        BotCast(bot, member, 20217);    // Blessing of Kings
                    else if (!member->HasAura(19740))
                        BotCast(bot, member, 19740);    // Blessing of Might
                }
                break;
            case CLASS_PRIEST:
                for (Player* member : party)
                {
                    if (!member->isAlive())
                        continue;
                    if (!member->HasAura(21562))
                        BotCast(bot, member, 21562);    // Power Word: Fortitude
                }
                break;
            case CLASS_WARRIOR:
                if (!bot->HasAura(6673))
                    BotCast(bot, bot, 6673);            // Battle Shout (party-wide)
                break;
            case CLASS_DEATH_KNIGHT:
                if (!bot->HasAura(57330))
                    BotCast(bot, bot, 57330);           // Horn of Winter (party-wide)
                break;
            default:
                break;
        }
    }

    // Class attack abilities for bots and the self-AI
    void CastClassAbilities(Player* caster, Unit* target, uint8 role, bool allowDamage = true)
    {
        switch (caster->getClass())
        {
            case CLASS_DEATH_KNIGHT:
                if (role == LB_ROLE_TANK)
                {
                    // Major defensive cooldowns
                    BotCast(caster, target, 55233);    // Vampiric Blood
                    BotCast(caster, target, 49028);    // Dancing Rune Weapon
                    BotCast(caster, target, 48792);    // Icebound Fortitude
                    BotCast(caster, target, 48707);    // Anti-Magic Shell
                    // Damage + threat rotation
                    BotCast(caster, target, 195182);   // Marrowrend
                    BotCast(caster, target, 206930);   // Heart Strike
                    BotCast(caster, target, 49998);    // Death Strike
                    BotCast(caster, target, 205223);   // Consumption
                    BotCast(caster, target, 50842);    // Blood Boil (AoE threat)
                }
                else
                {
                    BotCast(caster, target, 49020);    // Obliterate
                    BotCast(caster, target, 49143);    // Frost Strike
                }
                break;
            case CLASS_PALADIN:
                if (allowDamage)
                {
                    BotCast(caster, target, 35395);    // Crusader Strike
                    BotCast(caster, target, 20271);    // Judgment
                }
                break;
            case CLASS_ROGUE:
                BotCast(caster, target, 53);           // Backstab
                BotCast(caster, target, 196819);       // Eviscerate
                break;
            case CLASS_WARRIOR:
                BotCast(caster, target, 1719);         // Recklessness
                BotCast(caster, target, 184364);       // Enraged Regeneration
                BotCast(caster, target, 118000);       // Dragon Roar
                BotCast(caster, target, 184367);       // Rampage
                BotCast(caster, target, 23881);        // Bloodthirst
                BotCast(caster, target, 85288);        // Raging Blow
                if (target->GetHealthPct() < 20.0f)
                    BotCast(caster, target, 280735);   // Execute
                BotCast(caster, target, 190411);       // Whirlwind
                break;
            case CLASS_PRIEST:
                if (allowDamage)
                {
                    BotCast(caster, target, 14914);    // Holy Fire
                    BotCast(caster, target, 585);      // Smite
                }
                break;
            default:
                break;
        }
    }

    // Use a consumable from the bot's inventory (potion), with a personal cooldown
    void BotUsePotion(Player* bot, uint32 itemId)
    {
        if (!bot)
            return;

        Item* potion = bot->GetItemByEntry(itemId);
        if (!potion)
            return;

        {
            std::lock_guard<std::mutex> lock(g_legionBotsMutex);
            uint32 now = getMSTime();
            auto itr = g_legionBotPotionTimer.find(bot->GetGUID());
            if (itr != g_legionBotPotionTimer.end() && (now - itr->second) < 30000)
                return;
            g_legionBotPotionTimer[bot->GetGUID()] = now;
        }

        ItemTemplate const* proto = potion->GetTemplate();
        if (!proto)
            return;

        for (ItemEffectEntry const* effect : proto->Effects)
        {
            if (!effect || effect->SpellID <= 0 || effect->TriggerType != ITEM_SPELLTRIGGER_ON_USE)
                continue;

            uint32 spellId = uint32(effect->SpellID);
            if (bot->HasSpellCooldown(spellId))
                return;

            bot->CastSpell(bot, spellId, true);
            bot->DestroyItemCount(itemId, 1, true);
            return;
        }
    }

    // Lowest-HP ally of the bot's team (owner, other bots, self)
    Unit* FindLowestHpAlly(Player* owner, Player* bot)
    {
        Unit* best = nullptr;
        float lowest = 90.0f;   // heal aggressively so nobody drops
        Unit* tank = nullptr;
        float tankHp = 101.0f;

        if (owner->isAlive())
        {
            float hp = owner->GetHealthPct();
            if (hp < lowest)
            {
                best = owner;
                lowest = hp;
            }
        }

        std::vector<ObjectGuid> teammates;
        {
            std::lock_guard<std::mutex> lock(g_legionBotsMutex);
            auto itr = g_legionBots.find(owner->GetGUID());
            if (itr != g_legionBots.end())
                teammates = itr->second;
        }

        for (ObjectGuid botGuid : teammates)
        {
            if (botGuid == bot->GetGUID())
                continue;
            if (Player* mate = ObjectAccessor::FindPlayer(botGuid))
            {
                if (!mate->isAlive())
                    continue;

                float hp = mate->GetHealthPct();
                if (GetBotRole(botGuid) == LB_ROLE_TANK)
                {
                    tank = mate;
                    tankHp = hp;
                }
                if (hp < lowest)
                {
                    best = mate;
                    lowest = hp;
                }
            }
        }

        // Tank priority: keep the tank up first once they take real damage
        if (tank && tankHp < 75.0f)
            return tank;

        if (!best && bot->GetHealthPct() < 60.0f)
            best = bot;

        return best;
    }

    // Loot the bot's last kill
    void BotTryLoot(Player* bot)
    {
        if (!bot || bot->isInCombat())
            return;

        ObjectGuid corpseGuid;
        {
            std::lock_guard<std::mutex> lock(g_legionBotsMutex);
            auto itr = g_legionBotLastTarget.find(bot->GetGUID());
            if (itr != g_legionBotLastTarget.end())
                corpseGuid = itr->second;
        }

        if (corpseGuid.IsEmpty())
            return;

        Creature* corpse = ObjectAccessor::GetCreature(*bot, corpseGuid);
        if (!corpse || !corpse->isDead() || corpse->loot.isLooted() ||
            !corpse->HasFlag(OBJECT_FIELD_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE) ||
            !bot->IsWithinDistInMap(corpse, 15.0f))
            return;

        bot->SendLoot(corpse->GetGUID(), LOOT_CORPSE);

        Loot& loot = corpse->loot;
        for (uint8 i = 0; i < loot.items.size(); ++i)
            if (!loot.items[i].is_looted)
                bot->StoreLootItem(i, &loot);

        if (loot.gold)
        {
            bot->ModifyMoney(loot.gold);
            loot.gold = 0;
        }

        if (bot->GetSession())
            bot->GetSession()->DoLootRelease(corpse->GetGUID());

        std::lock_guard<std::mutex> lock(g_legionBotsMutex);
        g_legionBotLastTarget.erase(bot->GetGUID());
    }

    // Fully remove a bot player from the world without relying on session update ticks
    void DestroyBotPlayer(ObjectGuid botGuid, WorldSessionPtr session)
    {
        Player* bot = ObjectAccessor::FindPlayer(botGuid);
        if (!bot)
            return;

        if (Group* botGroup = bot->GetGroup())
            botGroup->RemoveMember(botGuid);

        bot->SaveToDB();

        if (session)
            session->SetPlayer(nullptr);

        TC_LOG_ERROR(LOG_FILTER_GENERAL, "PlayerBot: DestroyBotPlayer deleting bot %u", botGuid.GetCounter());

        // Mirror the core's logout teardown: cleanups, then RemovePlayerFromMap(true)
        // which detaches from the grid, removes from the object accessor and deletes the player.
        bot->CleanupsBeforeDelete();

        if (Map* map = bot->FindMap())
            map->RemovePlayerFromMap(bot, true);
        else
        {
            sObjectAccessor->RemoveObject(bot);
            delete bot;
        }
    }
}

bool LegionBot_IsBot(ObjectGuid guid)
{
    std::lock_guard<std::mutex> lock(g_legionBotsMutex);
    for (auto const& entry : g_legionBots)
        for (ObjectGuid botGuid : entry.second)
            if (botGuid == guid)
                return true;
    return false;
}

std::vector<ObjectGuid> LegionBot_GetBotsOf(ObjectGuid ownerGuid)
{
    std::lock_guard<std::mutex> lock(g_legionBotsMutex);
    auto itr = g_legionBots.find(ownerGuid);
    if (itr == g_legionBots.end())
        return std::vector<ObjectGuid>();
    return itr->second;
}

// Toggle the self-AI for a player (the bot AI fights for them). Returns the new state.
bool LegionBot_ToggleSelfAI(Player* player)
{
    if (!player)
        return false;

    bool enabled = false;
    {
        std::lock_guard<std::mutex> lock(g_legionBotsMutex);
        auto itr = g_legionPlayerAi.find(player->GetGUID());
        if (itr != g_legionPlayerAi.end())
        {
            g_legionPlayerAi.erase(itr);
            enabled = false;
        }
        else
        {
            g_legionPlayerAi.insert(player->GetGUID());
            enabled = true;
        }
    }

    if (enabled)
        LearnBotSpells(player);   // make sure the player has the class kit

    return enabled;
}

void LegionBot_DebugInfo(Player* owner, ChatHandler* handler)
{
    if (!owner || !handler)
        return;

    std::vector<ObjectGuid> bots = LegionBot_GetBotsOf(owner->GetGUID());
    uint32 now = getMSTime();

    LegionBotSettings const& settings = GetLegionBotSettings(owner);
    char const* modeName = settings.levelMode == LB_LEVEL_MAX ? "max" : (settings.levelMode == LB_LEVEL_FIXED ? "fixed" : "sync");
    char const* assistName = settings.assistMode == LB_ASSIST_DEFEND ? "defend" : (settings.assistMode == LB_ASSIST_CHILL ? "chill" : "full");
    handler->PSendSysMessage("|cff33ff99LegionBot:|r level |cffffff00%s|r (target %u) | assist |cffffff00%s|r | aggro |cffffff00%s|r | %u bot(s) online",
        modeName, uint32(GetLegionBotTargetLevel(owner)), assistName, settings.playerTank ? "you" : "bot", uint32(bots.size()));

    for (ObjectGuid botGuid : bots)
    {
        Player* bot = ObjectAccessor::FindPlayer(botGuid);
        if (!bot)
        {
            handler->PSendSysMessage("|cff33ff99Bot|r guid %u not in world", botGuid.GetCounter());
            continue;
        }

        uint8 role = 255;
        uint32 lastCast = 0;
        uint32 lastCastTime = 0;
        {
            std::lock_guard<std::mutex> lock(g_legionBotsMutex);
            auto rItr = g_legionBotRoles.find(botGuid);
            if (rItr != g_legionBotRoles.end())
                role = rItr->second;
            auto cItr = g_legionBotLastCast.find(botGuid);
            if (cItr != g_legionBotLastCast.end())
                lastCast = cItr->second;
            auto tItr = g_legionBotLastCastTime.find(botGuid);
            if (tItr != g_legionBotLastCastTime.end())
                lastCastTime = tItr->second;
        }

        Item* mainHand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
        uint32 mainHandEntry = mainHand ? mainHand->GetEntry() : 0;
        Unit* victim = bot->getVictim();

        handler->PSendSysMessage("|cff33ff99Bot|r %s (lvl %u) role %u | %s hp %.0f%% | combat %d victim %s (%.1f yd) | mainhand %u | spells %u | lastCast %u (%u s ago)",
            bot->GetName(), uint32(bot->getLevel()), role, bot->isDead() ? "DEAD" : "alive", bot->GetHealthPct(), bot->isInCombat() ? 1 : 0,
            victim ? victim->GetName() : "-", victim ? bot->GetDistance(victim) : -1.0f,
            mainHandEntry, uint32(bot->GetSpellMap().size()),
            lastCast, lastCastTime ? (now - lastCastTime) / 1000 : 0);
    }
}

// ---------------------------------------------------------------------------
// Level / autogear command API (used by the command script)
// ---------------------------------------------------------------------------

void LegionBot_LevelCommand(Player* owner, std::string const& arg, ChatHandler* handler)
{
    if (!owner || !handler)
        return;

    LegionBotSettings& s = GetLegionBotSettings(owner);
    uint8 const maxLevel = uint8(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));

    if (arg == "sync")
    {
        s.levelMode = LB_LEVEL_SYNC;
        SaveLegionBotSettings(owner);
        handler->PSendSysMessage("|cff33ff99LegionBot:|r level mode = |cffffff00sync|r (bots follow your level, you are %u).", owner->getLevel());
    }
    else if (arg == "max")
    {
        s.levelMode = LB_LEVEL_MAX;
        SaveLegionBotSettings(owner);
        handler->PSendSysMessage("|cff33ff99LegionBot:|r level mode = |cffffff00max|r (bots set to %u).", maxLevel);
    }
    else if (!arg.empty())
    {
        int requested = atoi(arg.c_str());
        if (requested < 1 || requested > maxLevel)
        {
            handler->PSendSysMessage("|cffff4444LegionBot:|r level must be between 1 and %u.", maxLevel);
            handler->SetSentErrorMessage(true);
            return;
        }
        s.levelMode = LB_LEVEL_FIXED;
        s.fixedLevel = uint8(requested);
        SaveLegionBotSettings(owner);
        handler->PSendSysMessage("|cff33ff99LegionBot:|r level mode = |cffffff00fixed %u|r.", requested);
    }
    else
    {
        handler->PSendSysMessage("|cff33ff99LegionBot:|r usage: .lbot level sync | max | <1-%u>", maxLevel);
        return;
    }

    // Apply immediately to the whole team
    uint8 const target = GetLegionBotTargetLevel(owner);
    for (ObjectGuid botGuid : LegionBot_GetBotsOf(owner->GetGUID()))
    {
        Player* bot = ObjectAccessor::FindPlayer(botGuid);
        if (!bot || !bot->IsInWorld())
            continue;
        if (bot->getLevel() != target)
        {
            bot->GiveLevel(target);
            LearnBotSpells(bot);
            EquipBotGear(bot, target);
        }
    }
}

void LegionBot_AutogearTeam(Player* owner, ChatHandler* handler)
{
    if (!owner || !handler)
        return;

    uint32 count = 0;
    for (ObjectGuid botGuid : LegionBot_GetBotsOf(owner->GetGUID()))
    {
        Player* bot = ObjectAccessor::FindPlayer(botGuid);
        if (!bot || !bot->IsInWorld())
            continue;
        EquipBotGear(bot, bot->getLevel());
        ++count;
    }

    if (count)
        handler->PSendSysMessage("|cff33ff99LegionBot:|r autogear applied to %u bot(s) at their current levels.", count);
    else
        handler->PSendSysMessage("|cffff4444LegionBot:|r no bots online - spawn the team first (.lbot team).");
}

void LegionBot_TankCommand(Player* owner, std::string const& arg, ChatHandler* handler)
{
    if (!owner || !handler)
        return;

    LegionBotSettings& s = GetLegionBotSettings(owner);

    if (arg == "me" || arg == "player")
    {
        s.playerTank = true;
        SaveLegionBotSettings(owner);
        handler->PSendSysMessage("|cff33ff99LegionBot:|r tank mode = |cffffff00you|r - the bots will not taunt or boost threat. You hold aggro.");
    }
    else if (arg == "bot")
    {
        s.playerTank = false;
        SaveLegionBotSettings(owner);
        handler->PSendSysMessage("|cff33ff99LegionBot:|r tank mode = |cffffff00bot|r - the tank bot holds aggro (default).");
    }
    else
    {
        handler->PSendSysMessage("|cff33ff99LegionBot:|r usage: .lbot aggro me | bot");
    }
}

void LegionBot_AssistCommand(Player* owner, std::string const& arg, ChatHandler* handler)
{
    if (!owner || !handler)
        return;

    LegionBotSettings& s = GetLegionBotSettings(owner);

    if (arg == "full")
    {
        s.assistMode = LB_ASSIST_FULL;
        SaveLegionBotSettings(owner);
        handler->PSendSysMessage("|cff33ff99LegionBot:|r assist = |cffffff00full|r - bots fight everything you fight (they wait a moment so you get the first hit).");
    }
    else if (arg == "defend")
    {
        s.assistMode = LB_ASSIST_DEFEND;
        SaveLegionBotSettings(owner);
        handler->PSendSysMessage("|cff33ff99LegionBot:|r assist = |cffffff00defend|r - bots only fight mobs that attack you.");
    }
    else if (arg == "chill")
    {
        s.assistMode = LB_ASSIST_CHILL;
        SaveLegionBotSettings(owner);
        handler->PSendSysMessage("|cff33ff99LegionBot:|r assist = |cffffff00chill|r - bots never attack (follow, heal, buff only).");
    }
    else
    {
        handler->PSendSysMessage("|cff33ff99LegionBot:|r usage: .lbot assist full | defend | chill");
    }
}

void LegionBot_Spawn(Player* owner, std::string const& charName, ChatHandler* handler, uint8 role = 255)
{
    // Sanitize the name (character names are alphanumeric only) to keep the query safe
    std::string safeName;
    for (char c : charName)
        if (isalnum(static_cast<unsigned char>(c)))
            safeName += c;

    if (safeName.empty())
    {
        if (handler)
        {
            handler->SendSysMessage("|cffff4444Playerbot:|r invalid character name.");
            handler->SetSentErrorMessage(true);
        }
        return;
    }

    // Locate the character + its account
    QueryResult result = CharacterDatabase.PQuery("SELECT guid, account FROM characters WHERE name = '%s'", safeName.c_str());
    if (!result)
    {
        if (handler)
        {
            handler->PSendSysMessage("|cffff4444Playerbot:|r no character named '%s' exists.", charName.c_str());
            handler->SetSentErrorMessage(true);
        }
        return;
    }

    Field* fields = result->Fetch();
    uint32 lowGuid = fields[0].GetUInt32();
    uint32 accountId = fields[1].GetUInt32();

    if (LegionBot_IsBot(ObjectGuid::Create<HighGuid::Player>(lowGuid)) || ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(lowGuid)))
    {
        // A previous instance of this bot is still around - clean it up so we can spawn fresh
        WorldSessionPtr oldSession;
        {
            std::lock_guard<std::mutex> lock(g_legionBotsMutex);
            auto itr = g_legionBotSessions.find(ObjectGuid::Create<HighGuid::Player>(lowGuid));
            if (itr != g_legionBotSessions.end())
            {
                oldSession = itr->second;
                g_legionBotSessions.erase(itr);
            }
        }
        DestroyBotPlayer(ObjectGuid::Create<HighGuid::Player>(lowGuid), oldSession);

        {
            std::lock_guard<std::mutex> lock(g_legionBotsMutex);
            for (auto& entry : g_legionBots)
            {
                auto& botList = entry.second;
                botList.erase(std::remove(botList.begin(), botList.end(), ObjectGuid::Create<HighGuid::Player>(lowGuid)), botList.end());
            }
        }

        CharacterDatabase.PExecute("DELETE FROM group_member WHERE memberGuid = %u", lowGuid);
        CharacterDatabase.PExecute("UPDATE characters SET online = 0 WHERE guid = %u", lowGuid);
    }

    // Clear stale instance binds so a previous dungeon run doesn't make AddPlayerToMap reject the bot
    CharacterDatabase.PExecute("DELETE FROM character_instance WHERE guid = %u", lowGuid);

    // Create a socket-less session for the bot.
    // NOTE: we intentionally do NOT register it with sWorld->AddSession() or map->AddSession().
    // Both would tick the session like a real client connection; without a socket that
    // fails and the core logs the player out (Map::UpdateSessions -> session->Update -> LogoutPlayer).
    // We keep the session alive ourselves and drive the bot from the owner's PlayerScript update.
    auto session = std::make_shared<WorldSession>(accountId, std::string(safeName), nullptr, SEC_MODERATOR, 6, 0, "bot",
        LOCALE_enUS, 0, false, AT_AUTH_FLAG_NONE, std::unordered_map<uint8, int64>(), 0);
    {
        std::lock_guard<std::mutex> lock(g_legionBotsMutex);
        g_legionBotSessions[ObjectGuid::Create<HighGuid::Player>(lowGuid)] = session;
    }

    // Load the character from the database
    LoginQueryHolder* holder = new LoginQueryHolder(accountId, ObjectGuid::Create<HighGuid::Player>(lowGuid));
    if (!holder->Initialize())
    {
        delete holder;
        UnregisterBotSession(ObjectGuid::Create<HighGuid::Player>(lowGuid));
        if (handler)
        {
            handler->PSendSysMessage("|cffff4444Playerbot:|r failed to prepare login queries for '%s'.", safeName.c_str());
            handler->SetSentErrorMessage(true);
        }
        return;
    }

    // Execute the login queries synchronously (the normal login flow runs this asynchronously
    // through the database worker pool; here we enqueue it and block until it is done)
    {
        QueryResultHolderFuture future = CharacterDatabase.DelayQueryHolder(holder);
        future.wait();
        holder = static_cast<LoginQueryHolder*>(future.get());
        if (!holder)
        {
            UnregisterBotSession(ObjectGuid::Create<HighGuid::Player>(lowGuid));
            if (handler)
            {
                handler->PSendSysMessage("|cffff4444Playerbot:|r login query execution failed for '%s'.", safeName.c_str());
                handler->SetSentErrorMessage(true);
            }
            return;
        }
    }

    Player* bot = new Player(session.get());
    if (!bot->LoadFromDB(ObjectGuid::Create<HighGuid::Player>(lowGuid), holder))
    {
        delete holder;
        session->SetPlayer(nullptr);
        UnregisterBotSession(ObjectGuid::Create<HighGuid::Player>(lowGuid));
        delete bot;
        if (handler)
        {
            handler->PSendSysMessage("|cffff4444Playerbot:|r failed to load character '%s'.", charName.c_str());
            handler->SetSentErrorMessage(true);
        }
        return;
    }
    delete holder;

    session->SetPlayer(bot);
    bot->GetMotionMaster()->Initialize();

    // Place the bot into the world - directly at the owner's spot (same map + instance)
    Map* map = nullptr;
    if (owner)
    {
        map = owner->GetMap();
        bot->Relocate(owner->GetPositionX(), owner->GetPositionY(), owner->GetPositionZ(), owner->GetOrientation());
        bot->SetPhaseMask(owner->GetPhaseMask(), false);
    }
    else
        map = sMapMgr->CreateMap(bot->GetMapId(), bot);

    if (!map)
    {
        session->SetPlayer(nullptr);
        UnregisterBotSession(ObjectGuid::Create<HighGuid::Player>(lowGuid));
        delete bot;
        if (handler)
        {
            handler->PSendSysMessage("|cffff4444Playerbot:|r failed to create a map for '%s'.", safeName.c_str());
            handler->SetSentErrorMessage(true);
        }
        return;
    }

    session->SetMap(map);
    bot->SetMap(map);

    bool placed = map->AddPlayerToMap(bot);

    // If we're in a dungeon and the bot (or its group) is still bound to a different instance,
    // clear the stale binds and retry once.
    if (!placed && map->IsDungeon())
    {
        bot->UnbindInstance(map->GetId(), map->GetDifficultyID());
        if (Group* botGroup = bot->GetGroup())
        {
            if (InstanceGroupBind* groupBind = botGroup->GetBoundInstance(map))
                if (groupBind->save && groupBind->save->GetInstanceId() != map->GetInstanceId())
                    botGroup->UnbindInstance(map->GetId(), map->GetDifficultyID());
        }
        placed = map->AddPlayerToMap(bot);
    }

    if (!placed)
    {
        TC_LOG_ERROR(LOG_FILTER_GENERAL, "PlayerBot: AddPlayerToMap failed for %s (map %u inst %u pos %.1f %.1f %.1f ownerMap %u ownerInst %u)",
            safeName.c_str(), map->GetId(), map->GetInstanceId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
            owner ? owner->GetMapId() : 0, owner ? owner->GetInstanceId() : 0);
        session->SetPlayer(nullptr);
        UnregisterBotSession(ObjectGuid::Create<HighGuid::Player>(lowGuid));
        if (bot->IsInGrid())
            map->RemovePlayerFromMap(bot, false);
        sObjectAccessor->RemoveObject(bot);
        delete bot;
        if (handler)
        {
            handler->PSendSysMessage("|cffff4444LegionBot:|r failed to place '%s' (map %u inst %u pos %.1f %.1f %.1f).", safeName.c_str(),
                map->GetId(), map->GetInstanceId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
            handler->SetSentErrorMessage(true);
        }
        return;
    }

    if (!bot->IsInWorld())
    {
        TC_LOG_ERROR(LOG_FILTER_GENERAL, "PlayerBot: %s not in world after AddPlayerToMap (map %u)", safeName.c_str(), map->GetId());
        session->SetPlayer(nullptr);
        UnregisterBotSession(ObjectGuid::Create<HighGuid::Player>(lowGuid));
        if (bot->IsInGrid())
            map->RemovePlayerFromMap(bot, false);
        sObjectAccessor->RemoveObject(bot);
        delete bot;
        if (handler)
        {
            handler->PSendSysMessage("|cffff4444Playerbot:|r '%s' could not enter the world.", safeName.c_str());
            handler->SetSentErrorMessage(true);
        }
        return;
    }

    sObjectAccessor->AddObject(bot);

    // Apply the owner's level mode, learn the class kit, gear for that level
    uint8 const spawnLevel = GetLegionBotTargetLevel(owner);
    if (bot->getLevel() != spawnLevel)
        bot->GiveLevel(spawnLevel);
    LearnBotSpells(bot);
    EquipBotGear(bot, spawnLevel);

    // Dungeon consumables: potions in the backpack (only if the items exist in this build)
    if (sObjectMgr->GetItemTemplate(LB_HEALTH_POTION_ID))
        bot->AddItem(LB_HEALTH_POTION_ID, 20);
    if (bot->getPowerType() == POWER_MANA && sObjectMgr->GetItemTemplate(LB_MANA_POTION_ID))
        bot->AddItem(LB_MANA_POTION_ID, 20);

    // Party buffs (only if the bot's level could have learned them)
    auto castBuffIfUsable = [&](uint32 spellId)
    {
        if (SpellInfo const* si = sSpellMgr->GetSpellInfo(spellId))
            if (si->BaseLevel <= bot->getLevel())
                bot->CastSpell(bot, spellId, true);
    };
    switch (bot->getClass())
    {
        case CLASS_WARRIOR:      castBuffIfUsable(6673);  break;   // Battle Shout
        case CLASS_PALADIN:      castBuffIfUsable(20217); break;   // Blessing of Kings
        case CLASS_PRIEST:       castBuffIfUsable(21562); break;   // Power Word: Fortitude
        case CLASS_DEATH_KNIGHT: castBuffIfUsable(57330); break;   // Horn of Winter
        default: break;
    }

    TC_LOG_ERROR(LOG_FILTER_GENERAL, "PlayerBot: %s (guid %u) entered world map %u inst %u pos %.1f %.1f %.1f (inWorld=%d canContact=%d)",
        safeName.c_str(), lowGuid, bot->GetMapId(), bot->GetInstanceId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
        bot->IsInWorld() ? 1 : 0, bot->CanContact() ? 1 : 0);

    // Derive the role from the class when not explicitly given
    if (role > LB_ROLE_DPS)
    {
        switch (bot->getClass())
        {
            case CLASS_DEATH_KNIGHT:
            case CLASS_WARRIOR:
            case CLASS_PALADIN:  role = LB_ROLE_TANK;   break;
            case CLASS_PRIEST:
            case CLASS_SHAMAN:
            case CLASS_DRUID:    role = LB_ROLE_HEALER; break;
            default:             role = LB_ROLE_DPS;    break;
        }
    }

    if (owner)
    {
        // Clean any stale membership row left over from a previous (possibly crashed) session
        CharacterDatabase.PExecute("DELETE FROM group_member WHERE memberGuid = %u", lowGuid);

        // Join the owner's party (create one if needed)
        Group* group = owner->GetGroup();
        if (!group)
        {
            group = new Group();
            if (group->Create(owner))
                sGroupMgr->AddGroup(group);
            else
            {
                delete group;
                group = nullptr;
            }
        }

        if (group && !group->IsMember(bot->GetGUID()))
            group->AddMember(bot);

        std::lock_guard<std::mutex> lock(g_legionBotsMutex);
        g_legionBotSlot[bot->GetGUID()] = uint8(g_legionBots[owner->GetGUID()].size());
        g_legionBots[owner->GetGUID()].push_back(bot->GetGUID());
        g_legionBotRoles[bot->GetGUID()] = role;
    }

    if (handler)
    {
        char const* roleName = (role == LB_ROLE_TANK) ? "tank" : ((role == LB_ROLE_HEALER) ? "healer" : "damage");
        handler->PSendSysMessage("|cff33ff99Playerbot:|r %s (%s, guid %u) has entered the world and joined your party.", safeName.c_str(), roleName, lowGuid);
    }
}

void LegionBot_DismissAll(Player* owner, ChatHandler* handler)
{
    if (!owner)
        return;

    std::vector<ObjectGuid> bots;
    {
        std::lock_guard<std::mutex> lock(g_legionBotsMutex);
        auto itr = g_legionBots.find(owner->GetGUID());
        if (itr != g_legionBots.end())
        {
            bots = itr->second;
            g_legionBots.erase(itr);
        }
    }

    uint32 count = 0;
    for (ObjectGuid botGuid : bots)
    {
        WorldSessionPtr session;
        {
            std::lock_guard<std::mutex> lock(g_legionBotsMutex);
            auto itr = g_legionBotSessions.find(botGuid);
            if (itr != g_legionBotSessions.end())
            {
                session = itr->second;
                g_legionBotSessions.erase(itr);
            }
        }

        if (ObjectAccessor::FindPlayer(botGuid))
        {
            DestroyBotPlayer(botGuid, session);
            ++count;
        }
    }

    if (handler)
        handler->PSendSysMessage("|cff33ff99Playerbot:|r %u bot(s) removed.", count);
}

// ---------------------------------------------------------------------------
// Follow / assist update loop (called from the world tick)
// ---------------------------------------------------------------------------

namespace
{
    // Self-AI: fight for the player (attack their target, use abilities + potions)
    void UpdateSelfAI(Player* player)
    {
        bool enabled = false;
        {
            std::lock_guard<std::mutex> lock(g_legionBotsMutex);
            enabled = g_legionPlayerAi.find(player->GetGUID()) != g_legionPlayerAi.end();
        }
        if (!enabled || !player->isAlive())
            return;

        // Consumables
        if (player->GetHealthPct() < 45.0f)
            BotUsePotion(player, LB_HEALTH_POTION_ID);
        else if (player->getPowerType() == POWER_MANA && player->GetPowerPct(POWER_MANA) < 35.0f)
            BotUsePotion(player, LB_MANA_POTION_ID);

        // Attack whatever is attacking us, or our current target
        Unit* target = player->getAttackerForHelper();
        if (!target)
            target = player->getVictim();

        if (target && target->isAlive() && player->IsValidAttackTarget(target))
        {
            if (player->getVictim() != target)
            {
                player->Attack(target, true);
                player->GetMotionMaster()->MoveChase(target);
            }
            CastClassAbilities(player, target, LB_ROLE_DPS);
        }
    }

    // Dungeon safety net: creatures in this repack's incomplete-vmap dungeons fall through
    // the floor when they chase. Pull them back up to the player's level (the player always
    // stands on the correct client-side collision).
    void FixFallenCreatures(Player* player, std::vector<ObjectGuid> const& botGuids)
    {
        auto fixOne = [&](Unit* unit)
        {
            if (!unit || unit->GetTypeId() != TYPEID_UNIT)
                return;

            Creature* creature = unit->ToCreature();
            if (!creature || !creature->isAlive())
                return;

            float const zDiff = player->GetPositionZ() - creature->GetPositionZ();
            if (zDiff > 3.0f && creature->GetDistance(player) < 80.0f)
                creature->NearTeleportTo(creature->GetPositionX(), creature->GetPositionY(), player->GetPositionZ(), creature->GetOrientation());
        };

        fixOne(player->getVictim());
        fixOne(player->getAttackerForHelper());

        for (ObjectGuid botGuid : botGuids)
            if (Player* bot = ObjectAccessor::FindPlayer(botGuid))
                fixOne(bot->getVictim());
    }

    void DismissBotsOfOwner(ObjectGuid ownerGuid)
    {
        std::vector<ObjectGuid> bots;
        {
            std::lock_guard<std::mutex> lock(g_legionBotsMutex);
            auto itr = g_legionBots.find(ownerGuid);
            if (itr != g_legionBots.end())
            {
                bots = itr->second;
                g_legionBots.erase(itr);
            }
        }

        for (ObjectGuid botGuid : bots)
        {
            WorldSessionPtr session;
            {
                std::lock_guard<std::mutex> lock(g_legionBotsMutex);
                auto itr = g_legionBotSessions.find(botGuid);
                if (itr != g_legionBotSessions.end())
                {
                    session = itr->second;
                    g_legionBotSessions.erase(itr);
                }
            }

            if (Player* owner = ObjectAccessor::FindPlayer(ownerGuid))
                if (owner->GetSession())
                    ChatHandler(owner->GetSession()).PSendSysMessage("|cffff4444BOT DEBUG:|r destroying bot %u", botGuid.GetCounter());

            if (ObjectAccessor::FindPlayer(botGuid))
                DestroyBotPlayer(botGuid, session);
        }
    }
}

void LegionBot_OnPlayerUpdate(Player* player, uint32 /*diff*/)
{
    if (!player || !player->IsInWorld())
        return;

    // Player self-AI (independent of the bot team)
    UpdateSelfAI(player);

    std::vector<ObjectGuid> botGuids;
    {
        std::lock_guard<std::mutex> lock(g_legionBotsMutex);
        auto itr = g_legionBots.find(player->GetGUID());
        if (itr != g_legionBots.end())
            botGuids = itr->second;
    }

    // Dungeon safety net: put creatures that fell through the floor back on our level
    if (player->GetMap() && player->GetMap()->IsDungeon())
        FixFallenCreatures(player, botGuids);

    if (botGuids.empty())
        return;

    // Level sync (every 2s): keep the team at the owner's chosen level
    {
        uint32 const now = getMSTime();
        bool doSync = false;
        {
            std::lock_guard<std::mutex> lock(g_legionBotsMutex);
            uint32& last = g_legionBotLastLevelCheck[player->GetGUID()];
            if (last == 0 || now - last >= 2000)
            {
                last = now;
                doSync = true;
            }
        }
        if (doSync)
        {
            uint8 const target = GetLegionBotTargetLevel(player);
            for (ObjectGuid botGuid : botGuids)
            {
                Player* bot = ObjectAccessor::FindPlayer(botGuid);
                if (!bot || !bot->IsInWorld() || bot->isDead())
                    continue;
                if (bot->getLevel() != target)
                {
                    bot->GiveLevel(target);
                    LearnBotSpells(bot);
                    EquipBotGear(bot, target);
                }
            }
        }
    }

    LegionBotSettings& settings = GetLegionBotSettings(player);

    for (ObjectGuid botGuid : botGuids)
    {
        Player* bot = ObjectAccessor::FindPlayer(botGuid);
        if (!bot || !bot->IsInWorld())
        {
            ChatHandler(player->GetSession()).PSendSysMessage("|cffff4444BOT DEBUG:|r self-heal triggered (found=%d inWorld=%d) - dismissing bots",
                bot ? 1 : 0, (bot && bot->IsInWorld()) ? 1 : 0);

            // Stale registry entry (e.g. bot was removed) - clean it up so it can be spawned again
            DismissBotsOfOwner(player->GetGUID());
            return;
        }

        if (bot->isDead())
        {
            // Resurrect bots once the fight is over
            if (!player->isInCombat())
            {
                bot->ResurrectPlayer(1.0f, false);
                bot->SpawnCorpseBones();
                bot->SetHealth(bot->GetMaxHealth());
                ChatHandler(player->GetSession()).PSendSysMessage("|cff33ff99LegionBot:|r %s has been resurrected.", bot->GetName());
            }
            continue;
        }

        uint8 role = LB_ROLE_DPS;
        {
            std::lock_guard<std::mutex> lock(g_legionBotsMutex);
            auto roleItr = g_legionBotRoles.find(botGuid);
            if (roleItr != g_legionBotRoles.end())
                role = roleItr->second;
        }

        // Formation slot = position in the owner's live bot list (always unique, never stale)
        uint8 slot = 0;
        uint8 roleIndex = 0;
        for (size_t i = 0; i < botGuids.size(); ++i)
        {
            if (botGuids[i] == botGuid)
            {
                slot = uint8(i);
                break;
            }
            if (GetBotRole(botGuids[i]) == role)
                ++roleIndex;
        }

        // Same map/instance?
        if (bot->GetMapId() != player->GetMapId() || bot->GetInstanceId() != player->GetInstanceId())
        {
            // Manual map transfer (bot sessions never tick, so TeleportTo's delayed events would never run).
            // Detach from the old map properly (grid/cell + visibility), like Player::TeleportTo does.
            if (Map* oldMap = bot->GetMap())
                oldMap->RemovePlayerFromMap(bot, false);

            Map* newMap = player->GetMap();
            Position formPos = GetFormationPosition(player, role, slot, roleIndex);
            bot->Relocate(formPos.m_positionX, formPos.m_positionY, formPos.m_positionZ, player->GetOrientation());
            bot->SetPhaseMask(player->GetPhaseMask(), false);
            bot->SetMap(newMap);
            // The map update loop only ticks players whose session map matches the map
            if (WorldSession* botSession = bot->GetSession())
                botSession->SetMap(newMap);
            newMap->AddPlayerToMap(bot);
            sObjectAccessor->AddObject(bot);
            continue;
        }

        float dist = bot->GetDistance(player);
        float zDiff = fabs(bot->GetPositionZ() - player->GetPositionZ());

        // If we're really far away or on a different floor/level, snap to the formation slot
        if (dist > 80.0f || zDiff > 3.0f)
        {
            Position formPos = GetFormationPosition(player, role, slot, roleIndex);
            bot->NearTeleportTo(formPos.m_positionX, formPos.m_positionY, formPos.m_positionZ, player->GetOrientation());
        }
        else
        {
            // Nobody repositions while attacking; the healer keeps distance when idle
            bool const canFollow = !bot->getVictim() && ((role == LB_ROLE_HEALER) || !bot->isInCombat());
            if (canFollow)
            {
                Position formPos = GetFormationPosition(player, role, slot, roleIndex);
                float const slotDist = bot->GetDistance(formPos.m_positionX, formPos.m_positionY, formPos.m_positionZ);
                if (slotDist > 2.5f && bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
                    bot->GetMotionMaster()->MovePoint(0, formPos.m_positionX, formPos.m_positionY, formPos.m_positionZ, false);
            }
        }

        // Loot corpses while out of combat
        if (!player->isInCombat() && !bot->isInCombat())
            BotTryLoot(bot);

        // LFG: automatically answer role checks and accept dungeon-ready proposals
        if (Group* lfgGroup = bot->GetGroup())
        {
            if (sLFGMgr->GetState(lfgGroup->GetGUID(), 0) == lfg::LFG_STATE_ROLECHECK)
            {
                uint8 lfgRoles = (role == LB_ROLE_TANK) ? lfg::PLAYER_ROLE_TANK : lfg::PLAYER_ROLE_DAMAGE;

                if (role == LB_ROLE_HEALER)
                {
                    // Dungeons only allow one healer: the first healer queues as healer,
                    // any extra healer queues as damage (they still heal in combat).
                    bool firstHealer = true;
                    for (ObjectGuid otherGuid : botGuids)
                    {
                        if (otherGuid == bot->GetGUID())
                            break;
                        if (GetBotRole(otherGuid) == LB_ROLE_HEALER)
                        {
                            firstHealer = false;
                            break;
                        }
                    }
                    lfgRoles = firstHealer ? lfg::PLAYER_ROLE_HEALER : lfg::PLAYER_ROLE_DAMAGE;
                }

                sLFGMgr->UpdateRoleCheck(lfgGroup->GetGUID(), bot->GetGUID(), lfgRoles);
            }
        }

        // LFG: accept the dungeon-ready proposal as soon as it appears (no client to click it)
        {
            lfg::LfgState playerLfgState = sLFGMgr->GetPlayerState(bot->GetGUID());

            uint8 lastState = 255;
            bool stateChanged = false;
            {
                std::lock_guard<std::mutex> lock(g_legionBotsMutex);
                auto stateItr = g_legionBotLfgState.find(bot->GetGUID());
                if (stateItr != g_legionBotLfgState.end())
                    lastState = stateItr->second;
                if (uint8(playerLfgState) != lastState)
                {
                    g_legionBotLfgState[bot->GetGUID()] = uint8(playerLfgState);
                    stateChanged = true;
                }
            }

            if (stateChanged)
                ChatHandler(player->GetSession()).PSendSysMessage("|cff33ff99LegionBot|r %s LFG state: %u", bot->GetName(), uint32(playerLfgState));

            if (sLFGMgr->AutoAcceptProposal(bot->GetGUID()))
                ChatHandler(player->GetSession()).PSendSysMessage("|cff33ff99LegionBot|r %s accepted the dungeon proposal.", bot->GetName());
        }

        // Consumables: chug a potion when hurt or low on mana
        if (bot->GetHealthPct() < 45.0f)
            BotUsePotion(bot, LB_HEALTH_POTION_ID);
        else if (bot->getPowerType() == POWER_MANA && bot->GetPowerPct(POWER_MANA) < 35.0f)
            BotUsePotion(bot, LB_MANA_POTION_ID);

        // Party buffs (out of combat)
        BuffParty(player, botGuids, bot);

        // ---- Team tactics ----

        // Healer: triage heals first; attack when everyone is taken care of
        bool healerCanDps = false;
        if (role == LB_ROLE_HEALER)
        {
            if (bot->GetPowerPct(POWER_MANA) > 5.0f)
            {
                if (Unit* healTarget = FindLowestHpAlly(player, bot))
                    CastHealAbility(bot, healTarget);
                else
                    healerCanDps = true;
            }
        }

        // Target selection (respects the assist mode)
        Unit* target = nullptr;
        if (settings.assistMode == LB_ASSIST_CHILL)
        {
            // chill: bots never attack - they follow, heal and buff only
            target = nullptr;
        }
        else if (settings.assistMode == LB_ASSIST_DEFEND)
        {
            // defend: only fight mobs that are attacking the owner
            target = player->getAttackerForHelper();
        }
        else if (role == LB_ROLE_TANK)
        {
            target = player->getAttackerForHelper();
            if (!target)
                target = bot->getVictim();
            if (!target)
                target = player->getVictim();
        }
        else if ((role == LB_ROLE_DPS || (role == LB_ROLE_HEALER && healerCanDps)) && (player->isInCombat() || bot->isInCombat()))
        {
            // Player-tank mode: assist the owner's target directly
            if (settings.playerTank)
            {
                target = player->getVictim();
                if (!target)
                    target = player->getAttackerForHelper();
            }
            if (!target)
            {
                for (ObjectGuid otherGuid : botGuids)
                {
                    if (otherGuid == bot->GetGUID() || GetBotRole(otherGuid) != LB_ROLE_TANK)
                        continue;
                    if (Player* tank = ObjectAccessor::FindPlayer(otherGuid))
                        if (tank->IsInWorld() && tank->getVictim() && tank->getVictim()->isAlive())
                        {
                            target = tank->getVictim();
                            break;
                        }
                }
            }
            if (!target)
                target = player->getAttackerForHelper();
            if (!target)
                target = player->getVictim();
        }

        // Tank: taunt mobs off any party member (works even before we have a target)
        // Skipped in player-tank mode - the owner wants to hold aggro.
        if (role == LB_ROLE_TANK && !settings.playerTank)
        {
            Unit* tauntTarget = player->getAttackerForHelper();
            if (!tauntTarget)
            {
                for (ObjectGuid otherGuid : botGuids)
                {
                    if (otherGuid == bot->GetGUID())
                        continue;
                    if (Player* mate = ObjectAccessor::FindPlayer(otherGuid))
                        if (mate->IsInWorld() && mate->getAttackerForHelper())
                        {
                            tauntTarget = mate->getAttackerForHelper();
                            break;
                        }
                }
            }
            if (tauntTarget && tauntTarget->isAlive() && tauntTarget != bot->getVictim() && bot->IsValidAttackTarget(tauntTarget)
                && player->GetDistance(tauntTarget) <= LB_MAX_ENGAGE_DISTANCE)
            {
                if (bot->getClass() == CLASS_DEATH_KNIGHT)
                {
                    if (!bot->HasSpellCooldown(56222))
                        BotCast(bot, tauntTarget, 56222);   // Dark Command
                    else if (!bot->HasSpellCooldown(49576))
                        BotCast(bot, tauntTarget, 49576);   // Death Grip (taunt on cooldown)
                }
                else
                {
                    BotCast(bot, tauntTarget, 355);         // Taunt (warrior)
                }
            }
        }

        if (target && target->isAlive() && bot->IsValidAttackTarget(target) &&
            player->GetDistance(target) <= LB_MAX_ENGAGE_DISTANCE)
        {
            // Give the owner the first swing: wait briefly after picking a new target
            uint32 const nowMs = getMSTime();
            uint32 engageAt = 0;
            {
                std::lock_guard<std::mutex> lock(g_legionBotsMutex);
                auto& since = g_legionBotTargetSince[bot->GetGUID()];
                if (since.first != target->GetGUID())
                {
                    since.first = target->GetGUID();
                    since.second = nowMs;
                }
                engageAt = since.second;
            }
            if (nowMs - engageAt < LB_ENGAGE_DELAY_MS)
                continue;

            if (bot->getVictim() != target)
                bot->Attack(target, true);

            // Always ensure we're chasing the target (formation movement may have taken over)
            if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
                bot->GetMotionMaster()->MoveChase(target);

            // Tank: extra threat per tick so mobs stick to us
            // (disabled in player-tank mode so the owner keeps aggro)
            if (role == LB_ROLE_TANK && !settings.playerTank)
                target->AddThreat(bot, 120.0f);

                // Remember the creature for looting once it dies
                if (target->GetTypeId() == TYPEID_UNIT)
                {
                    std::lock_guard<std::mutex> lock(g_legionBotsMutex);
                    g_legionBotLastTarget[bot->GetGUID()] = target->GetGUID();
                }

                // Class attack abilities
                CastClassAbilities(bot, target, role, (role != LB_ROLE_HEALER) || healerCanDps);
            }
    }
}

void LegionBot_OnPlayerLogout(Player* player)
{
    if (!player)
        return;

    DismissBotsOfOwner(player->GetGUID());
}

class LegionBotPlayerScript : public PlayerScript
{
public:
    LegionBotPlayerScript() : PlayerScript("LegionBotPlayerScript") {}

    void OnUpdate(Player* player, uint32 diff) override
    {
        LegionBot_OnPlayerUpdate(player, diff);
    }

    void OnLogout(Player* player) override
    {
        LegionBot_OnPlayerLogout(player);
    }
};

void AddSC_LegionBotAI()
{
    new LegionBotPlayerScript();
}


