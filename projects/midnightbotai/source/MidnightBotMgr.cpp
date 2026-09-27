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

#include "MidnightBotMgr.h"
#include "AccountMgr.h"
#include "CharacterCache.h"
#include "CharacterPackets.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Loot.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PetDefines.h"
#include "PhasingHandler.h"
#include "PhaseShift.h"
#include "Player.h"
#include "RaceMask.h"
#include "RealmList.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "SmartEnum.h"
#include "Spell.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "Timer.h"
#include "Util.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <thread>

namespace
{
    constexpr uint32 AutoSpawnDelayMs = 15000;
constexpr uint32 FollowRefreshMs = 500;
constexpr uint32 DebugAutoDumpMs = 10000;
constexpr uint32 SpacingCheckMs = 2000;     // personal-space maintenance cadence
constexpr uint32 VictimGraceMs = 5000;      // keep a fresh victim even before combat starts

    bool ValidateBotName(std::string& name, std::string& err)
    {
        if (name.empty())
        {
            err = "name is empty";
            return false;
        }

        if (name.size() > MAX_PLAYER_NAME)
        {
            err = "name is longer than " + std::to_string(uint32(MAX_PLAYER_NAME)) + " characters";
            return false;
        }

        for (char c : name)
        {
            if (!std::isalnum(static_cast<unsigned char>(c)))
            {
                err = "name must only contain letters and digits";
                return false;
            }
        }

        if (!normalizePlayerName(name))
        {
            err = "name could not be normalized";
            return false;
        }

        ResponseCodes nameResult = ObjectMgr::CheckPlayerName(name, sWorld->GetDefaultDbcLocale(), true);
        if (nameResult != CHAR_NAME_SUCCESS)
        {
            err = "name rejected by the core (code " + std::to_string(uint32(nameResult)) + ")";
            return false;
        }

        return true;
    }

    WorldSession* CreateSessionForAccount(uint32 accountId, std::string& err)
    {
        QueryResult accountResult = LoginDatabase.PQuery("SELECT username, email, expansion, mutetime, client_build, locale, os, timezone_offset, recruiter, battlenet_account FROM account WHERE id = {}", accountId);
        if (!accountResult)
        {
            err = "account not found";
            return nullptr;
        }

        Field* accountFields = accountResult->Fetch();
        std::string accountName = accountFields[0].GetString();
        std::string accountEmail = accountFields[1].GetString();
        uint8 expansion = accountFields[2].GetUInt8();
        time_t muteTime = time_t(accountFields[3].GetInt64());
        uint32 clientBuild = accountFields[4].GetUInt32();
        uint8 locale = accountFields[5].GetUInt8();
        std::string os = accountFields[6].GetString();
        int16 timezoneOffset = accountFields[7].GetInt16();
        uint32 recruiter = accountFields[8].GetUInt32();
        uint32 battlenetAccountId = accountFields[9].IsNull() ? 0 : accountFields[9].GetUInt32();

        AccountTypes security = AccountTypes(sAccountMgr->GetSecurity(accountId, sRealmList->GetCurrentRealmId().Realm));

        WorldSession* session = new WorldSession(accountId, std::move(accountName), battlenetAccountId, std::move(accountEmail),
            std::shared_ptr<WorldSocket>(), security, expansion, muteTime, std::move(os), Minutes(timezoneOffset), clientBuild,
            ClientBuild::VariantId{ ClientBuild::Platform::Win_x64, ClientBuild::Arch::x64, ClientBuild::Type::Retail },
            LocaleConstant(locale), recruiter, false);
        session->SetBot(true);
        return session;
    }

    Player* FindInWorldPlayer(ObjectGuid guid)
    {
        if (guid.IsEmpty())
            return nullptr;

        Player* player = ObjectAccessor::FindConnectedPlayer(guid);
        return (player && player->IsInWorld()) ? player : nullptr;
    }

    bool PhaseShiftsMatch(PhaseShift const& left, PhaseShift const& right)
    {
        if (left.GetPhases().size() != right.GetPhases().size())
            return false;

        for (PhaseShift::PhaseRef const& phase : left.GetPhases())
            if (!right.HasPhase(phase.Id))
                return false;

        return true;
    }

    // Copies the owner's phase shift (and suppressed phase shift) onto the bot when they differ.
    // PhasingHandler::InheritPhaseShift (PhasingHandler.cpp:264) only copies the phase data and does
    // NOT refresh visibility, so callers must force a visibility update afterwards when this returns true.
    bool InheritOwnerPhase(Player* bot, Player* owner)
    {
        if (!bot || !owner || !bot->IsInWorld())
            return false;

        if (PhaseShiftsMatch(bot->GetPhaseShift(), owner->GetPhaseShift()) &&
            PhaseShiftsMatch(bot->GetSuppressedPhaseShift(), owner->GetSuppressedPhaseShift()))
            return false;

        PhasingHandler::InheritPhaseShift(bot, owner);

        TC_LOG_INFO("scripts.MidnightBotAI", "Bot '{}' inherited the phase of '{}' (phases: {})",
            bot->GetName(), owner->GetName(), PhasingHandler::FormatPhases(bot->GetPhaseShift()));
        return true;
    }

    // Result of the ordered target validation. `rejection` is nullptr when the target is safe;
    // `ownerVisible` is true when it was only accepted because the owner can see it (phased zones).
    struct TargetCheck
    {
        char const* rejection = nullptr;
        bool ownerVisible = false;
    };

    // Manual, ordered validation (never bot/owner/GM/same-group):
    //   dead -> untargetable state/flag -> friendly (bot or owner) -> phase (bot vs target) ->
    //   visibility (bot sees OR owner sees OR the mob is hitting the bot) -> full core attackable
    //   check via WorldObject::IsValidAttackTarget (Object.cpp:2329) from whichever player can see.
    // This avoids IsValidAttackTarget hiding the reason behind its internal CanSeeOrDetect check
    // (Object.cpp:2362) while still applying all other core rules (immunity/PvP/faction).
    TargetCheck CheckAttackTarget(Player* bot, Player* owner, Unit* target)
    {
        TargetCheck result;

        if (!target)
        {
            result.rejection = "target missing";
            return result;
        }

        if (target == bot)
        {
            result.rejection = "target is the bot";
            return result;
        }

        if (target == owner)
        {
            result.rejection = "target is the owner";
            return result;
        }

        if (!target->IsAlive())
        {
            result.rejection = "target dead";
            return result;
        }

        if (!target->IsInWorld())
        {
            result.rejection = "target not in world";
            return result;
        }

        if (target->HasUnitState(UNIT_STATE_UNATTACKABLE) ||
            target->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NON_ATTACKABLE_2 | UNIT_FLAG_ON_TAXI | UNIT_FLAG_NOT_ATTACKABLE_1))
        {
            result.rejection = "target untargetable";
            return result;
        }

        if (Player* targetPlayer = target->ToPlayer())
        {
            if (targetPlayer->IsGameMaster())
            {
                result.rejection = "target is GM";
                return result;
            }

            if (Group* group = bot->GetGroup())
                if (group->IsMember(targetPlayer->GetGUID()))
                {
                    result.rejection = "target in bot group";
                    return result;
                }

            if (owner && owner != bot)
                if (Group* ownerGroup = owner->GetGroup())
                    if (ownerGroup->IsMember(targetPlayer->GetGUID()))
                    {
                        result.rejection = "target in owner group";
                        return result;
                    }
        }

        if (bot->IsFriendlyTo(target) || (owner && owner != bot && owner->IsFriendlyTo(target)))
        {
            result.rejection = "target friendly";
            return result;
        }

        if (!bot->InSamePhase(target))
        {
            result.rejection = "target phase mismatch";
            return result;
        }

        bool const botSees = bot->CanSeeOrDetect(target);
        bool const ownerSees = owner && owner != bot && owner->CanSeeOrDetect(target);
        bool const selfDefense = (target->GetVictim() == bot);

        if (!botSees && !ownerSees && !selfDefense)
        {
            result.rejection = "target not visible";
            return result;
        }

        // Full core validation from a player that can actually see the target; for pure
        // self-defense (a mob is hitting the bot) run it from the bot only when it sees the mob.
        Player* checker = botSees ? bot : (ownerSees ? owner : nullptr);
        if (checker && !checker->IsValidAttackTarget(target))
        {
            result.rejection = "target not attackable";
            return result;
        }

        result.ownerVisible = !botSees && !selfDefense && ownerSees;
        return result;
    }

    bool IsSafeAttackTarget(Player* bot, Player* owner, Unit* target)
    {
        return CheckAttackTarget(bot, owner, target).rejection == nullptr;
    }

    // Concrete visibility state for owner-visible-only targets (phased zones). Detect auras:
    // Unit::HasInvisibilityAura/HasStealthAura (Unit.h:1100-1101), IsVisible (Unit.h:1604),
    // SPELL_AURA_MOD_INVISIBILITY_DETECT/_DETECT_STEALTH (SpellAuraDefines.h:104-106).
    char const* BoolStr(bool value)
    {
        return value ? "yes" : "no";
    }

    std::string LowerCopy(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return text;
    }

    bool ParseLootMethod(std::string text, LootMethod& method)
    {
        text = LowerCopy(std::move(text));

        if (text == "ffa")
            method = FREE_FOR_ALL;
        else if (text == "roundrobin")
            method = ROUND_ROBIN;
        else if (text == "master")
            method = MASTER_LOOT;
        else if (text == "group")
            method = GROUP_LOOT;
        else if (text == "needgreed")
            method = NEED_BEFORE_GREED;
        else
            return false;

        return true;
    }

    bool ParseItemQuality(std::string text, ItemQualities& quality)
    {
        text = LowerCopy(std::move(text));

        if (text == "poor")
            quality = ITEM_QUALITY_POOR;
        else if (text == "common")
            quality = ITEM_QUALITY_NORMAL;
        else if (text == "uncommon")
            quality = ITEM_QUALITY_UNCOMMON;
        else if (text == "rare")
            quality = ITEM_QUALITY_RARE;
        else if (text == "epic")
            quality = ITEM_QUALITY_EPIC;
        else if (text == "legendary")
            quality = ITEM_QUALITY_LEGENDARY;
        else
            return false;

        return true;
    }

    char const* LootMethodName(LootMethod method)
    {
        switch (method)
        {
            case FREE_FOR_ALL:      return "ffa";
            case ROUND_ROBIN:       return "roundrobin";
            case MASTER_LOOT:       return "master";
            case GROUP_LOOT:        return "group";
            case NEED_BEFORE_GREED: return "needgreed";
            case PERSONAL_LOOT:     return "personal";
            default:                return "unknown";
        }
    }

    char const* ItemQualityName(ItemQualities quality)
    {
        switch (quality)
        {
            case ITEM_QUALITY_POOR:      return "poor";
            case ITEM_QUALITY_NORMAL:    return "common";
            case ITEM_QUALITY_UNCOMMON:  return "uncommon";
            case ITEM_QUALITY_RARE:      return "rare";
            case ITEM_QUALITY_EPIC:      return "epic";
            case ITEM_QUALITY_LEGENDARY: return "legendary";
            default:                     return "unknown";
        }
    }

    // ------------------- lightweight gear picker (ported from the old LegionBotAI) -------------------

    uint8 QualityCapForLevel(uint8 level)
    {
        if (level < 10)
            return 1;
        if (level < 25)
            return 2;
        return 3;
    }

    uint8 QualityMaxForLevel(uint8 level)
    {
        if (level < 20)
            return 2;
        if (level < 60)
            return 3;
        return 4;
    }

    uint16 IlvlCapForLevel(uint8 level)
    {
        if (level <= 60)
            return uint16(uint32(level) * 2 + 6);
        if (level <= 80)
            return uint16(uint32(level) * 3);
        if (level <= 90)
            return uint16(uint32(level) * 5);
        if (level <= 100)
            return uint16(uint32(level) * 8);
        return 2000;
    }

    // Best item for one slot at a level, picked from the client DB2 item data (same source as the
    // old project). All candidates are validated later via CanUseItem/CanEquipNewItem anyway.
    uint32 PickItemForSlot(Player* bot, uint8 level, uint8 itemClass, uint8 armorSubclass,
        uint32 weaponSubclassMask, uint8 invType, uint8 qualityCap, std::vector<uint32> const& exclude)
    {
        uint32 bestEntry = 0;
        uint16 bestIlvl = 0;
        uint8 bestQuality = 0;
        uint32 const classMask = (bot->GetClass() > 0 && bot->GetClass() < 64) ? (uint32(1) << (bot->GetClass() - 1)) : 0;

        for (ItemSparseEntry const* sparse : sItemSparseStore)
        {
            if (!sparse->ID)
                continue;

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

            if (invType != 0 && uint8(db2->InventoryType) != invType)
                continue;

            if (sparse->RequiredLevel < 0 || uint8(sparse->RequiredLevel) > level)
                continue;
            if (sparse->ItemLevel < 1 || sparse->ItemLevel > IlvlCapForLevel(level))
                continue;
            if (sparse->OverallQualityID < 0 || uint8(sparse->OverallQualityID) > qualityCap)
                continue;
            if (uint8(sparse->OverallQualityID) == 7)              // heirloom
                continue;
            if (sparse->PlayerLevelToItemLevelCurveID != 0)        // scaling item
                continue;
            if (sparse->MaxCount != 0)
                continue;
            if (sparse->AllowableClass != 0 && sparse->AllowableClass != -1 &&
                !(uint32(uint16(sparse->AllowableClass)) & classMask))
                continue;

            if (std::find(exclude.begin(), exclude.end(), sparse->ID) != exclude.end())
                continue;

            if (sparse->ItemLevel > bestIlvl || (sparse->ItemLevel == bestIlvl && uint8(sparse->OverallQualityID) > bestQuality))
            {
                bestIlvl = sparse->ItemLevel;
                bestQuality = uint8(sparse->OverallQualityID);
                bestEntry = sparse->ID;
            }
        }

        return bestEntry;
    }

    std::map<uint32, std::vector<uint32>> g_botGearCache;

    std::vector<uint32> PickGearForLevel(Player* bot, uint8 level)
    {
        uint32 const cacheKey = uint32(bot->GetRace()) * 100000 + uint32(bot->GetClass()) * 1000 + level;
        if (auto cached = g_botGearCache.find(cacheKey); cached != g_botGearCache.end())
            return cached->second;

        uint8 armorSubclass = ITEM_SUBCLASS_ARMOR_CLOTH;
        uint32 weaponMask = (1u << 10);   // staff
        uint8 weaponInvType = INVTYPE_2HWEAPON;
        bool useShield = false;

        switch (bot->GetClass())
        {
            case CLASS_WARRIOR:
            case CLASS_DEATH_KNIGHT:
                armorSubclass = (level < 40) ? ITEM_SUBCLASS_ARMOR_MAIL : ITEM_SUBCLASS_ARMOR_PLATE;
                weaponMask = (1u << 1) | (1u << 5) | (1u << 8) | (1u << 6);
                weaponInvType = INVTYPE_2HWEAPON;
                break;
            case CLASS_PALADIN:
                armorSubclass = (level < 40) ? ITEM_SUBCLASS_ARMOR_MAIL : ITEM_SUBCLASS_ARMOR_PLATE;
                weaponMask = (1u << 4) | (1u << 7);   // 1H mace/sword
                weaponInvType = INVTYPE_WEAPON;
                useShield = true;
                break;
            case CLASS_HUNTER:
            case CLASS_SHAMAN:
                armorSubclass = (level < 40) ? ITEM_SUBCLASS_ARMOR_LEATHER : ITEM_SUBCLASS_ARMOR_MAIL;
                weaponMask = (1u << 1) | (1u << 5) | (1u << 8) | (1u << 10);
                weaponInvType = INVTYPE_2HWEAPON;
                break;
            case CLASS_ROGUE:
            case CLASS_DRUID:
                armorSubclass = ITEM_SUBCLASS_ARMOR_LEATHER;
                weaponMask = (1u << 15) | (1u << 7) | (1u << 10);
                weaponInvType = INVTYPE_WEAPON;
                break;
            default: // cloth casters
                armorSubclass = ITEM_SUBCLASS_ARMOR_CLOTH;
                weaponMask = (1u << 10);
                weaponInvType = INVTYPE_2HWEAPON;
                break;
        }

        uint8 const cap = QualityCapForLevel(level);
        uint8 const maxQuality = QualityMaxForLevel(level);
        std::vector<uint32> items;

        uint8 const armorInvTypes[] = { INVTYPE_HEAD, INVTYPE_NECK, INVTYPE_SHOULDERS, INVTYPE_CHEST, INVTYPE_WAIST,
            INVTYPE_LEGS, INVTYPE_FEET, INVTYPE_WRISTS, INVTYPE_HANDS, INVTYPE_CLOAK, INVTYPE_FINGER, INVTYPE_TRINKET };

        for (uint8 invType : armorInvTypes)
        {
            uint32 entry = 0;
            for (uint8 c = cap; c <= maxQuality && !entry; ++c)
                entry = PickItemForSlot(bot, level, ITEM_CLASS_ARMOR, armorSubclass, 0, invType, c, items);

            if (entry)
                items.push_back(entry);
        }

        for (uint8 invType : { INVTYPE_FINGER, INVTYPE_TRINKET })   // second ring/trinket
        {
            uint32 entry = 0;
            for (uint8 c = cap; c <= maxQuality && !entry; ++c)
                entry = PickItemForSlot(bot, level, ITEM_CLASS_ARMOR, armorSubclass, 0, invType, c, items);

            if (entry)
                items.push_back(entry);
        }

        uint32 weapon = 0;
        for (uint8 c = cap; c <= maxQuality && !weapon; ++c)
            weapon = PickItemForSlot(bot, level, ITEM_CLASS_WEAPON, 0, weaponMask, weaponInvType, c, items);

        if (!weapon)   // last resort: any usable melee weapon
        {
            uint32 const anyMeleeMask = (1u << 0) | (1u << 1) | (1u << 4) | (1u << 5) | (1u << 6) |
                (1u << 7) | (1u << 8) | (1u << 10) | (1u << 13) | (1u << 15);
            for (uint8 c = cap; c <= 4 && !weapon; ++c)
                weapon = PickItemForSlot(bot, level, ITEM_CLASS_WEAPON, 0, anyMeleeMask, 0, c, items);
        }

        if (weapon)
            items.push_back(weapon);

        if (useShield)
        {
            uint32 shield = 0;
            for (uint8 c = cap; c <= maxQuality && !shield; ++c)
                shield = PickItemForSlot(bot, level, ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_SHIELD, 0, INVTYPE_SHIELD, c, items);

            if (shield)
                items.push_back(shield);
        }

        g_botGearCache[cacheKey] = items;
        return items;
    }

    std::vector<uint8> EquipSlotsForInvType(uint8 invType)
    {
        switch (invType)
        {
            case INVTYPE_HEAD:          return { EQUIPMENT_SLOT_HEAD };
            case INVTYPE_NECK:          return { EQUIPMENT_SLOT_NECK };
            case INVTYPE_SHOULDERS:     return { EQUIPMENT_SLOT_SHOULDERS };
            case INVTYPE_CHEST:
            case INVTYPE_ROBE:          return { EQUIPMENT_SLOT_CHEST };
            case INVTYPE_WAIST:         return { EQUIPMENT_SLOT_WAIST };
            case INVTYPE_LEGS:          return { EQUIPMENT_SLOT_LEGS };
            case INVTYPE_FEET:          return { EQUIPMENT_SLOT_FEET };
            case INVTYPE_WRISTS:        return { EQUIPMENT_SLOT_WRISTS };
            case INVTYPE_HANDS:         return { EQUIPMENT_SLOT_HANDS };
            case INVTYPE_FINGER:        return { EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2 };
            case INVTYPE_TRINKET:       return { EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2 };
            case INVTYPE_CLOAK:         return { EQUIPMENT_SLOT_BACK };
            case INVTYPE_WEAPON:        return { EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND };
            case INVTYPE_2HWEAPON:
            case INVTYPE_WEAPONMAINHAND: return { EQUIPMENT_SLOT_MAINHAND };
            case INVTYPE_WEAPONOFFHAND:
            case INVTYPE_SHIELD:
            case INVTYPE_HOLDABLE:      return { EQUIPMENT_SLOT_OFFHAND };
            default:                    return {};
        }
    }

    // Equips only upgrades (empty slot or higher item level) after verifying CanUseItem. Safe:
    // template presence checked, old item destroyed first, auto-slot equip with swap disabled.
    uint32 EquipBotGear(Player* bot, uint8 level)
    {
        if (!bot)
            return 0;

        uint32 upgrades = 0;

        for (uint32 itemId : PickGearForLevel(bot, level))
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
            if (!proto)
                continue;

            if (bot->CanUseItem(proto) != EQUIP_ERR_OK)
                continue;

            std::vector<uint8> slots = EquipSlotsForInvType(uint8(proto->GetInventoryType()));
            if (slots.empty())
                continue;

            uint32 const candidateIlvl = proto->GetBaseItemLevel();
            uint8 chosenSlot = 0;
            bool found = false;

            for (uint8 slot : slots)
            {
                Item* current = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                uint32 const currentIlvl = current ? current->GetTemplate()->GetBaseItemLevel() : 0;
                if (candidateIlvl > currentIlvl)
                {
                    chosenSlot = slot;
                    found = true;
                    break;
                }
            }

            if (!found)
                continue;

            if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, chosenSlot))
                bot->DestroyItem(INVENTORY_SLOT_BAG_0, chosenSlot, true);

            uint16 dest = 0;
            if (bot->CanEquipNewItem(NULL_SLOT, dest, itemId, false) == EQUIP_ERR_OK)
            {
                bot->EquipNewItem(dest, itemId, ItemContext::NONE, true);
                ++upgrades;
            }
        }

        if (upgrades)
            bot->UpdateAllStats();

        return upgrades;
    }

    std::string OwnerVisibleDiagnostics(Player* bot, Player* owner, Unit* target)
    {
        if (!bot || !target)
            return "owner-visible diag: missing target";

        return std::string("owner-visible diag: InSamePhase=") + BoolStr(bot->InSamePhase(target))
            + ", dist=" + std::to_string(uint32(bot->GetExactDist(target))) + "yd"
            + ", botSee=" + BoolStr(bot->CanSeeOrDetect(target))
            + ", ownerSee=" + BoolStr(owner && owner->CanSeeOrDetect(target))
            + ", validAtkBot=" + BoolStr(bot->IsValidAttackTarget(target))
            + ", validAtkOwner=" + BoolStr(owner && owner->IsValidAttackTarget(target))
            + ", privateObj=" + BoolStr(target->IsPrivateObject())
            + ", privateOwner='" + target->GetPrivateObjectOwner().ToString() + "'"
            + ", spawnTrack=" + BoolStr(target->GetSpawnTrackingStateDataForPlayer(bot) != nullptr)
            + ", targetInvis=" + (target->HasInvisibilityAura() ? "yes" : "no")
            + ", targetStealth=" + (target->HasStealthAura() ? "yes" : "no")
            + ", botVisible=" + (bot->IsVisible() ? "yes" : "no")
            + ", targetVisible=" + (target->IsVisible() ? "yes" : "no");
    }

    // Compact one-line groups used for ring entries (the full per-check report goes to chat).
    std::string VisibilityReportCompact(Player* bot, Player* owner, Unit* target)
    {
        return std::string("vis: botSee=") + BoolStr(bot->CanSeeOrDetect(target))
            + " ownerSee=" + BoolStr(owner && owner->CanSeeOrDetect(target))
            + " samePhase=" + BoolStr(bot->InSamePhase(target))
            + " validAtkBot=" + BoolStr(bot->IsValidAttackTarget(target))
            + " validAtkOwner=" + BoolStr(owner && owner->IsValidAttackTarget(target))
            + " privateObj=" + BoolStr(target->IsPrivateObject())
            + " spawnTrack=" + BoolStr(target->GetSpawnTrackingStateDataForPlayer(bot) != nullptr);
    }

    std::string VisibilityReportGeometry(Player* bot, Unit* target)
    {
        return "vis: dist=" + std::to_string(uint32(bot->GetDistance(target) * 10.0f) / 10.0f)
            + " reachSum=" + std::to_string(uint32((bot->GetCombatReach() + target->GetCombatReach()) * 10.0f) / 10.0f)
            + " meleeRange=" + std::to_string(uint32(bot->GetMeleeRange(target) * 10.0f) / 10.0f)
            + " inArc=" + BoolStr(bot->HasInArc(float(M_PI) * 2.0f / 3.0f, target))
            + " inMelee=" + BoolStr(bot->IsWithinMeleeRange(target))
            + " meleeAttacking=" + BoolStr(bot->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
            + " attackReady=" + BoolStr(bot->isAttackReady(BASE_ATTACK))
            + " swing=" + std::to_string(bot->getAttackTimer(BASE_ATTACK)) + "ms";
    }

    std::string VisibilityReportMisc(Player* bot, Player* owner, Unit* target)
    {
        Group* group = bot->GetGroup();

        return std::string("vis: group=") + BoolStr(group != nullptr)
            + " groupHasOwner=" + BoolStr(group && owner && group->IsMember(owner->GetGUID()))
            + " sameMap=" + BoolStr(bot->GetMap() == target->GetMap())
            + " botInWorld=" + BoolStr(bot->IsInWorld())
            + " targetInWorld=" + BoolStr(target->IsInWorld())
            + " alive=" + BoolStr(target->IsAlive())
            + " typeId=" + std::to_string(uint32(target->GetTypeId()))
            + " entry=" + std::to_string(target->ToCreature() ? target->ToCreature()->GetEntry() : 0)
            + " uber=" + BoolStr(bot->HasPlayerFlag(PLAYER_FLAGS_UBER))
            + " botGM=" + BoolStr(bot->IsGameMaster())
            + " friendly=" + BoolStr(bot->IsFriendlyTo(target))
            + " targetFriendly=" + BoolStr(target->IsFriendlyTo(bot))
            + " hostile=" + BoolStr(bot->IsHostileTo(target));
    }

    std::vector<std::string> VisibilityReportLines(Player* bot, Player* owner, Unit* target)
    {
        std::vector<std::string> lines;
        lines.push_back("vis: InSamePhase(bot,target)=" + std::string(BoolStr(bot->InSamePhase(target))));
        lines.push_back("vis: CanSeeOrDetect(bot)=" + std::string(BoolStr(bot->CanSeeOrDetect(target))));
        lines.push_back("vis: CanSeeOrDetect(owner)=" + std::string(BoolStr(owner && owner->CanSeeOrDetect(target))));
        lines.push_back("vis: IsValidAttackTarget(bot)=" + std::string(BoolStr(bot->IsValidAttackTarget(target))));
        lines.push_back("vis: IsValidAttackTarget(owner)=" + std::string(BoolStr(owner && owner->IsValidAttackTarget(target))));
        lines.push_back("vis: IsNeverVisibleFor=skipped (protected core API)");
        lines.push_back("vis: CanDetect=skipped (private core API)");
        lines.push_back("vis: IsAlwaysVisibleFor=skipped (protected core API)");
        lines.push_back("vis: IsAlwaysDetectableFor=skipped (protected core API)");
        lines.push_back("vis: IsPrivateObject=" + std::string(BoolStr(target->IsPrivateObject()))
            + " privateOwner='" + target->GetPrivateObjectOwner().ToString() + "'");
        lines.push_back("vis: GetSpawnTrackingStateDataForPlayer(bot)="
            + std::string(target->GetSpawnTrackingStateDataForPlayer(bot) ? "set" : "null"));
        lines.push_back("vis: botHasGroup=" + std::string(BoolStr(bot->GetGroup() != nullptr))
            + " groupHasOwner=" + std::string(BoolStr(bot->GetGroup() && owner && bot->GetGroup()->IsMember(owner->GetGUID()))));
        lines.push_back("vis: IsFriendlyTo(bot->target)=" + std::string(BoolStr(bot->IsFriendlyTo(target)))
            + " IsFriendlyTo(target->bot)=" + std::string(BoolStr(target->IsFriendlyTo(bot)))
            + " IsHostileTo(bot->target)=" + std::string(BoolStr(bot->IsHostileTo(target))));
        lines.push_back("vis: botUber=" + std::string(BoolStr(bot->HasPlayerFlag(PLAYER_FLAGS_UBER)))
            + " botGM=" + std::string(BoolStr(bot->IsGameMaster()))
            + " targetTypeId=" + std::to_string(uint32(target->GetTypeId()))
            + " targetEntry=" + std::to_string(target->ToCreature() ? target->ToCreature()->GetEntry() : 0));
        lines.push_back("vis: sameMap=" + std::string(BoolStr(bot->GetMap() == target->GetMap()))
            + " botInWorld=" + std::string(BoolStr(bot->IsInWorld()))
            + " targetInWorld=" + std::string(BoolStr(target->IsInWorld()))
            + " targetAlive=" + std::string(BoolStr(target->IsAlive())));
        lines.push_back("vis: " + VisibilityReportGeometry(bot, target));
        lines.push_back("vis: " + VisibilityReportMisc(bot, owner, target));
        return lines;
    }

    // Timestamped, deduplicated ring buffer of AI decisions (kept regardless of log level).
    bool PushBotAiLog(MidnightBotMgr::BotRef& bot, std::string const& message)
    {
        if (bot.lastAiRaw == message)
            return false;

        bot.lastAiRaw = message;

        std::time_t now = std::time(nullptr);
        std::tm* tm = std::localtime(&now);
        char stamp[16] = "??:??:??";
        if (tm)
            std::strftime(stamp, sizeof(stamp), "%H:%M:%S", tm);

        bot.aiLog.push_back(std::string(stamp) + " " + message);
        while (bot.aiLog.size() > 10)
            bot.aiLog.pop_front();

        return true;
    }

    std::string SpellResultName(SpellCastResult result)
    {
        if (result == SPELL_CAST_OK)
            return "SPELL_CAST_OK";

        // EnumUtils<SpellCastResult> is generated in enuminfo_SharedDefines.cpp:2469;
        // result 13 is SPELL_FAILED_BAD_TARGETS (SharedDefines.h:1725).
        return std::string("failed(") + EnumUtils::ToString(result).Constant + ")";
    }

    std::string Fmt1(float value)
    {
        return Trinity::StringFormat("{:.1f}", value);
    }

    // Death check + resurrection. Unit::isDead() covers DEAD/CORPSE (Unit.h:1202); the standard
    // player revive pattern is ResurrectPlayer(Player.h:2339, Player.cpp:4311) + SpawnCorpseBones
    // (Player.h:2332) as used by '.group revive' (cs_group.cpp:130-133). restore_percent 1.0 gives
    // full health and no resurrection sickness.
    bool EnsureBotAlive(Player* bot)
    {
        if (!bot || !bot->IsInWorld())
            return false;

        if (!bot->isDead() && bot->IsAlive() && bot->GetHealth() > 0)
            return false;

        bot->ResurrectPlayer(1.0f, false);
        // triggerSave=false: never write to the DB from the update tick; the core's periodic
        // player save persists the resurrection shortly after.
        bot->SpawnCorpseBones(false);

        if (bot->GetHealth() < bot->GetMaxHealth())
            bot->SetHealth(bot->GetMaxHealth());

        return true;
    }

    // Every in-game control command routes its issuer through here: AutoSpawned bots start with
    // an empty owner, so commands like assist/attack/party would otherwise never resolve one.
    // Must be called while the roster lock is held (mutates BotRef).
    void EnsureOwnerForCommand(MidnightBotMgr::BotRef& bot, Player* issuer, char const* command)
    {
        if (!issuer)
            return;

        ObjectGuid issuerGuid = issuer->GetGUID();
        if (bot.ownerGuid == issuerGuid)
            return;

        bot.ownerGuid = issuerGuid;

        // Binding a bot defaults its AI to follow + assist so it acts without extra commands.
        bot.follow = true;
        bot.assist = true;

        sMidnightBotMgr->SaveBotOwnerLocked(bot.guid, issuerGuid);

        PushBotAiLog(bot, "owner set to '" + issuer->GetName() + "' (command '" + command + "') + follow/assist on");
        TC_LOG_INFO("scripts.MidnightBotAI", "Bot '{}': owner set to '{}' (command '{}') + follow/assist on", bot.name, issuer->GetName(), command);
    }

    char const* MovementTypeName(MovementGeneratorType type)
    {
        switch (type)
        {
            case IDLE_MOTION_TYPE:      return "idle";
            case RANDOM_MOTION_TYPE:    return "random";
            case WAYPOINT_MOTION_TYPE:  return "waypoint";
            case CONFUSED_MOTION_TYPE:  return "confused";
            case CHASE_MOTION_TYPE:     return "chase";
            case HOME_MOTION_TYPE:      return "home";
            case FLIGHT_MOTION_TYPE:    return "flight";
            case POINT_MOTION_TYPE:     return "point";
            case FLEEING_MOTION_TYPE:   return "fleeing";
            case FOLLOW_MOTION_TYPE:    return "follow";
            case FORMATION_MOTION_TYPE: return "formation";
            default:                    return "other";
        }
    }

    // Forward declarations for helpers defined further down.
    uint32 GetNukeSpell(Player* bot);

    // True while the bot is casting/channeling. UNIT_STATE_CASTING is Unit.h:755 (state flag);
    // IsNonMeleeSpellCast (Unit.h:1478) is the core's spell-in-progress check.
    bool BotIsBusyCasting(Player* bot)
    {
        if (!bot)
            return false;

        return bot->HasUnitState(UNIT_STATE_CASTING) || bot->IsNonMeleeSpellCast(false, false, true);
    }

    // Stops the current spline and turns the bot toward the target before attacking/casting.
    // Unit::SetFacingToObject (Unit.h:1197) uses an ImmediateMovementGenerator (MotionMaster.cpp:1184)
    // so it does not linger and cannot pile up; Unit::StopMoving is Unit::StopMoving (Unit.cpp:10751).
    void AdminBotFaceTarget(Player* bot, Unit* target)
    {
        if (!bot || !target || !bot->IsInWorld() || !target->IsInWorld())
            return;

        if (BotIsBusyCasting(bot))
            return; // never move/face during a cast

        bot->StopMoving();
        bot->SetFacingToObject(target);
    }

    // Freezes the bot immediately before a cast. Skipping new movement calls is not enough:
    // an already-running FOLLOW/CHASE generator keeps issuing movement each map update and would
    // cancel the cast, so both persistent generators are removed here.
    // MotionMaster::Remove(type) is MotionMaster.h:143 (impl MotionMaster.cpp:393);
    // FOLLOW_MOTION_TYPE/CHASE_MOTION_TYPE are MovementDefines.h:49/40; Unit::StopMoving (Unit.cpp:10751).
    void FreezeBotForCast(Player* bot, Unit* target)
    {
        if (!bot || !bot->IsInWorld())
            return;

        bot->StopMoving();

        if (bot->HasUnitState(UNIT_STATE_FOLLOW))
            bot->GetMotionMaster()->Remove(FOLLOW_MOTION_TYPE);

        if (bot->HasUnitState(UNIT_STATE_CHASE))
            bot->GetMotionMaster()->Remove(CHASE_MOTION_TYPE);

        if (target && target->IsInWorld())
            bot->SetFacingToObject(target);
    }

    // Effective max range of the bot's nuke (SpellInfo::GetMaxRange, SpellInfo.h:558), fallback 25 yd.
    float GetNukeRange(Player* bot)
    {
        uint32 nuke = GetNukeSpell(bot);
        if (nuke)
            if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(nuke, DIFFICULTY_NONE))
            {
                float range = spellInfo->GetMaxRange(false, bot);
                if (range > 0.0f)
                    return range;
            }

        return 25.0f;
    }

    // Instant triggered mask: no GCD/power/cast time and TRIGGERED_CAST_DIRECTLY so effects
    // apply synchronously (Spell.cpp:3574). Used for heals.
    constexpr TriggerCastFlags InstantTriggerMask = TriggerCastFlags(TRIGGERED_FULL_MASK | TRIGGERED_IGNORE_TARGET_CHECK);

    // Animated triggered mask: same freedoms but WITHOUT TRIGGERED_IGNORE_CAST_TIME and WITHOUT
    // TRIGGERED_CAST_DIRECTLY, so the spell keeps its cast time and the core sends
    // SpellStart/SpellGo packets (client animation + sound) while still ignoring GCD/cooldowns/
    // power and the DBC explicit-target check for owner-visible targets.
    constexpr TriggerCastFlags AnimatedTriggerMask = TriggerCastFlags(
        TRIGGERED_IGNORE_TARGET_CHECK | TRIGGERED_IGNORE_GCD | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD
        | TRIGGERED_IGNORE_POWER_COST | TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_IGNORE_CAST_ITEM
        | TRIGGERED_IGNORE_SHAPESHIFT | TRIGGERED_IGNORE_CASTER_MOUNTED_OR_ON_VEHICLE | TRIGGERED_DONT_REPORT_CAST_ERROR);

    SpellCastResult BotCastSpell(Player* bot, Unit* target, uint32 spellId, Player* /*owner*/, bool instant)
    {
        return bot->CastSpell(target, spellId, CastSpellExtraArgs(instant ? InstantTriggerMask : AnimatedTriggerMask));
    }

    // Mirrors the pet/charm pattern: Unit::Attack + MoveChase when allowed to chase
    // (UnitAI::AttackStart, UnitAI.cpp:29-33; PetAI::DoAttack, PetAI.cpp:400-436).
    // Stay mode (follow == false) attacks in place without chasing.
    // desiredRange <= 0 means melee range; otherwise use a spell range (ranged roles).
    // Returns true when the bot ends up with the target as its victim.
    bool StartBotAttack(Player* bot, Unit* target, bool chase, float desiredRange)
    {
        bot->SetSelection(target->GetGUID());

        // Movement is skipped while casting so a wind-up is never cancelled; melee auto-attack
        // still proceeds (Unit::Attack does not move the bot).
        if (!BotIsBusyCasting(bot))
        {
            bool const inRange = (desiredRange > 0.0f
                ? bot->IsWithinDist(target, desiredRange)
                : bot->IsWithinMeleeRange(target));

            if (inRange)
            {
                AdminBotFaceTarget(bot, target);
            }
            else if (chase)
            {
                if (bot->HasUnitState(UNIT_STATE_FOLLOW))
                    bot->GetMotionMaster()->Remove(FOLLOW_MOTION_TYPE);

                if (!bot->HasUnitState(UNIT_STATE_CHASE))
                    bot->GetMotionMaster()->MoveChase(target);
            }
            else
            {
                if (bot->HasUnitState(UNIT_STATE_CHASE))
                    bot->GetMotionMaster()->Remove(CHASE_MOTION_TYPE);

                if (bot->HasUnitState(UNIT_STATE_FOLLOW))
                    bot->GetMotionMaster()->Remove(FOLLOW_MOTION_TYPE);

                bot->StopMoving();
            }
        }

        if (bot->GetVictim() != target)
            bot->Attack(target, true);

        return bot->GetVictim() == target;
    }

    // ------------------------- M3.3 role-based combat helpers -------------------------

    BotRole DefaultRoleForClass(uint8 classId)
    {
        switch (classId)
        {
            case CLASS_WARRIOR:
                return BotRole::Tank;
            case CLASS_PRIEST:
            case CLASS_PALADIN:
            case CLASS_DRUID:
            case CLASS_SHAMAN:
                return BotRole::Healer;
            case CLASS_MAGE:
            case CLASS_WARLOCK:
            case CLASS_HUNTER:
            case CLASS_ROGUE:
            case CLASS_DEATH_KNIGHT:
            default:
                return BotRole::Dps;
        }
    }

    bool ParseRoleString(std::string text, BotRole& role)
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

    char const* RoleName(BotRole role)
    {
        switch (role)
        {
            case BotRole::Tank:   return "tank";
            case BotRole::Healer: return "healer";
            case BotRole::Dps:    return "dps";
            default:              return "none";
        }
    }

    // MoveFollow (MotionMaster.h:162) takes a float distance + ChaseAngle; healers/DPS keep
    // their distance from the owner, everyone else uses the pet follow distance.
    float FollowDistanceForRole(BotRole role)
    {
        switch (role)
        {
            case BotRole::Healer: return 20.0f;
            case BotRole::Dps:    return 15.0f;
            default:              return PET_FOLLOW_DIST;
        }
    }

    // Deterministic personal spacing slot: a golden-angle distribution over the roster guid keeps
    // bots (e.g. mage + warlock) from parking on the same follow/kite spot, and stays stable
    // across respawns without persisting anything.
    float SpacingAngleForGuid(uint32 guid)
    {
        constexpr float goldenAngle = 2.39996323f; // pi * (3 - sqrt(5))
        float angle = std::fmod(float(guid) * goldenAngle, float(M_PI) * 2.0f);
        if (angle < 0.0f)
            angle += float(M_PI) * 2.0f;
        return angle;
    }

    float SpacingDistForGuid(uint32 guid)
    {
        return (float(guid % 3) - 1.0f) * 0.5f; // -0.5 / 0 / +0.5 yd
    }

    bool IsPartyMember(Player* bot, Player* owner, Unit* target)
    {
        Player* targetPlayer = target ? target->ToPlayer() : nullptr;
        if (!targetPlayer || !bot)
            return false;

        if (targetPlayer == bot || (owner && targetPlayer == owner))
            return true;

        if (Group* group = bot->GetGroup())
            if (group->IsMember(targetPlayer->GetGUID()))
                return true;

        if (owner && owner != bot)
            if (Group* ownerGroup = owner->GetGroup())
                if (ownerGroup->IsMember(targetPlayer->GetGUID()))
                    return true;

        return false;
    }

    // Best known heal per class (all gated by Player::HasSpell, Player.h:1959). Priest picks by
    // target health: >= 50% uses Flash Heal (2061), below uses Heal/Lesser Heal (2060/2050).
    uint32 GetBestHealSpell(Player* bot, float targetPct)
    {
        static uint32 const priestHigh[] = { 2061, 2060, 2050 }; // Flash Heal -> Heal -> Lesser Heal
        static uint32 const priestLow[]  = { 2060, 2050, 2061 }; // Heal -> Lesser Heal -> Flash Heal
        static uint32 const paladin[]    = { 19750, 635 };       // Flash of Light, Holy Light
        static uint32 const druid[]      = { 8936, 5185 };       // Regrowth, Healing Touch
        static uint32 const shaman[]     = { 8004, 331 };        // Lesser Healing Wave, Healing Wave

        uint32 const* spells = nullptr;
        size_t count = 0;

        switch (bot->GetClass())
        {
            case CLASS_PRIEST:
                if (targetPct >= 50.0f)
                {
                    spells = priestHigh;
                    count = sizeof(priestHigh) / sizeof(priestHigh[0]);
                }
                else
                {
                    spells = priestLow;
                    count = sizeof(priestLow) / sizeof(priestLow[0]);
                }
                break;
            case CLASS_PALADIN: spells = paladin; count = sizeof(paladin) / sizeof(paladin[0]); break;
            case CLASS_DRUID:   spells = druid;   count = sizeof(druid) / sizeof(druid[0]);     break;
            case CLASS_SHAMAN:  spells = shaman;  count = sizeof(shaman) / sizeof(shaman[0]);   break;
            default:            break;
        }

        for (size_t i = 0; i < count; ++i)
            if (bot->HasSpell(spells[i]))
                return spells[i];

        return 0;
    }

    // One rank-1 nuke per class, highest priority first.
    uint32 GetNukeSpell(Player* bot)
    {
        static uint32 const mage[]    = { 133, 116 };   // Fireball, Frostbolt
        static uint32 const warlock[] = { 686, 348 };   // Shadow Bolt, Immolate
        static uint32 const priest[]  = { 585 };        // Smite
        static uint32 const druid[]   = { 5176 };       // Wrath
        static uint32 const shaman[]  = { 403 };        // Lightning Bolt

        uint32 const* spells = nullptr;
        size_t count = 0;

        switch (bot->GetClass())
        {
            case CLASS_MAGE:    spells = mage;    count = sizeof(mage) / sizeof(mage[0]);       break;
            case CLASS_WARLOCK: spells = warlock; count = sizeof(warlock) / sizeof(warlock[0]); break;
            case CLASS_PRIEST:  spells = priest;  count = sizeof(priest) / sizeof(priest[0]);   break;
            case CLASS_DRUID:   spells = druid;   count = sizeof(druid) / sizeof(druid[0]);     break;
            case CLASS_SHAMAN:  spells = shaman;  count = sizeof(shaman) / sizeof(shaman[0]);   break;
            default:            break;
        }

        for (size_t i = 0; i < count; ++i)
            if (bot->HasSpell(spells[i]))
                return spells[i];

        return 0;
    }

    uint32 GetOffensiveAbility(Player* bot)
    {
        switch (bot->GetClass())
        {
            case CLASS_WARRIOR:      return bot->HasSpell(78) ? 78u : 0u;         // Heroic Strike rank 1
            case CLASS_PALADIN:      return bot->HasSpell(20271) ? 20271u : 0u;   // Judgement rank 1
            case CLASS_DEATH_KNIGHT: return bot->HasSpell(45477) ? 45477u : 0u;   // Icy Touch rank 1
            default:                 return 0;
        }
    }

    // Small warrior rotation, evaluated once per tick (one ability per tick):
    //   Charge (100) out of melee but <= 25 yd and not yet in combat with the victim
    //   Rend (772) in melee when the victim lacks the bleed
    //   Heroic Strike (78) in melee with enough rage
    //   Battle Shout (6673) on self while out of combat and missing the aura
    struct BotAbilityChoice
    {
        uint32 spellId = 0;
        char const* name = "";
        bool selfCast = false;
    };

    BotAbilityChoice ChooseWarriorAbility(Player* bot, Unit* victim)
    {
        if (bot->HasSpell(100) && !bot->IsWithinMeleeRange(victim) && !bot->IsWithinDist(victim, 8.0f)
            && bot->IsWithinDist(victim, 25.0f) && bot->CanSeeOrDetect(victim) && !bot->IsInCombatWith(victim))
            return { 100, "charge", false };

        if (bot->HasSpell(772) && bot->IsWithinMeleeRange(victim) && !victim->HasAura(772))
            return { 772, "rend", false };

        if (bot->HasSpell(78) && bot->IsWithinMeleeRange(victim)
            && (bot->GetPowerType() != POWER_RAGE || bot->GetPower(POWER_RAGE) >= 10))
            return { 78, "heroic strike", false };

        if (bot->HasSpell(6673) && !bot->HasAura(6673) && !bot->IsInCombat())
            return { 6673, "battle shout", true };

        return {};
    }

    // Paladin kit: self buffs first (Devotion Aura, Seal of Righteousness), then Judgement.
    // Seal of Righteousness rank 1 is 20154 in classic data; 21084 is kept as fallback and both
    // are in the paladin learn list (the level gate skips whichever the core does not allow).
    BotAbilityChoice ChoosePaladinAbility(Player* bot, Unit* victim)
    {
        if (bot->HasSpell(465) && !bot->HasAura(465))
            return { 465, "devotion aura", true };

        uint32 seal = bot->HasSpell(20154) ? 20154u : (bot->HasSpell(21084) ? 21084u : 0u);
        if (seal && !bot->HasAura(seal))
            return { seal, "seal of righteousness", true };

        if (bot->HasSpell(20271) && !bot->IsWithinMeleeRange(victim) && bot->IsWithinDist(victim, 10.0f))
            return { 20271, "judgement", false };   // ranged opener so the pull lands with threat

        if (bot->HasSpell(20271) && bot->IsWithinMeleeRange(victim))
            return { 20271, "judgement", false };

        return {};
    }

    uint32 GetTauntSpell(Player* bot)
    {
        static uint32 const taunts[] = { 355, 6795, 62124, 56222 }; // Taunt, Growl, Hand of Reckoning, Dark Command
        for (uint32 spellId : taunts)
            if (bot->HasSpell(spellId))
                return spellId;

        return 0;
    }

    // Basic learnable abilities per role/class. Every id is gated by SpellInfo::BaseLevel
    // (SpellInfo.h:388) so a level-1 bot never learns something the core expects later.
    std::vector<uint32> GetRoleLearnSpells(uint8 classId, BotRole role);

    std::vector<uint32> GetClassLearnSpells(uint8 classId)
    {
        switch (classId)
        {
            case CLASS_WARRIOR:      return { 78, 284, 285, 1608, 100, 6178, 772, 6546, 6673, 5242 }; // Heroic Strike chain, Charge, Rend, Battle Shout
            case CLASS_PALADIN:      return { 635, 639, 647, 19750, 19939, 20271, 465, 20154, 21084 }; // Holy Light, Flash of Light, Judgement, Devotion Aura, Seal of Righteousness
            case CLASS_PRIEST:       return { 2050, 2052, 2053, 2060, 2061, 585, 591, 598 };         // Heals, Smite
            case CLASS_DRUID:        return { 5185, 5186, 5187, 8936, 5176, 5177, 5178, 467 };       // Healing Touch, Regrowth, Wrath, Thorns
            case CLASS_SHAMAN:       return { 331, 332, 547, 8004, 8008, 403, 529, 548, 8017 };      // Heals, Lightning Bolt, Rockbiter
            case CLASS_MAGE:         return { 133, 143, 145, 116, 205 };                             // Fireball, Frostbolt
            case CLASS_WARLOCK:      return { 686, 695, 705, 348, 707 };                             // Shadow Bolt, Immolate
            case CLASS_HUNTER:       return { 2973, 14260, 75 };                                     // Raptor Strike, Auto Shot
            case CLASS_ROGUE:        return { 1752, 1757, 1758, 2098, 6760, 6761 };                  // Sinister Strike, Eviscerate
            case CLASS_DEATH_KNIGHT: return { 45477, 49896, 45462, 49917, 49998 };                   // Icy Touch, Plague Strike, Death Strike
            default:                 return {};
        }
    }

    std::vector<uint32> GetRoleLearnSpells(uint8 classId, BotRole role)
    {
        switch (role)
        {
            case BotRole::Tank:
                switch (classId)
                {
                    case CLASS_WARRIOR:      return { 78, 284, 285, 1608, 100, 6178, 772, 6546, 6673, 5242, 355 };
                    case CLASS_PALADIN:      return { 635, 639, 647, 20271, 465, 20154, 21084, 62124 };
                    case CLASS_DRUID:        return { 5185, 5186, 5187, 5176, 5177, 467, 6795 };
                    case CLASS_DEATH_KNIGHT: return { 45477, 49896, 45462, 49917, 49998, 56222 };
                    default:                 return {};
                }
            case BotRole::Healer:
                switch (classId)
                {
                    case CLASS_PRIEST:  return { 2050, 2052, 2053, 2060, 2061, 585, 591 };
                    case CLASS_PALADIN: return { 635, 639, 647, 19750, 19939, 20271, 465, 20154 };
                    case CLASS_DRUID:   return { 5185, 5186, 5187, 8936, 5176, 5177 };
                    case CLASS_SHAMAN:  return { 331, 332, 547, 8004, 8008, 403, 529 };
                    default:            return {};
                }
            case BotRole::Dps:
                switch (classId)
                {
                    case CLASS_MAGE:    return { 133, 143, 145, 116, 205 };
                    case CLASS_WARLOCK: return { 686, 695, 705, 348, 707 };
                    case CLASS_PRIEST:  return { 585, 591, 598 };
                    case CLASS_DRUID:   return { 5176, 5177, 5178 };
                    case CLASS_SHAMAN:  return { 403, 529, 548 };
                    case CLASS_HUNTER:  return { 2973, 14260, 75 };
                    case CLASS_ROGUE:   return { 1752, 1757, 1758, 2098, 6760, 6761 };
                    case CLASS_WARRIOR: return { 78, 284, 285, 100, 6178, 772, 6546 };
                    case CLASS_DEATH_KNIGHT: return { 45477, 49896, 45462, 49917 };
                    default:            return {};
                }
            case BotRole::None:
            default:
                return GetClassLearnSpells(classId);
        }
    }

    // Learns missing role spells. Player::LearnSpell (Player.h:1969, Player.cpp:3123) persists
    // through the next _SaveSpells (Player.cpp:21503) - suppressMessaging=true keeps it quiet.
    uint32 LearnRoleSpells(Player* bot, BotRole role)
    {
        if (!bot)
            return 0;

        uint32 learned = 0;
        for (uint32 spellId : GetRoleLearnSpells(bot->GetClass(), role))
        {
            if (bot->HasSpell(spellId))
                continue;

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
            if (!spellInfo)
                continue;

            // SpellLevel is the level at which the core expects the spell to be learnable
            // (SpellInfo.h:389); fall back to BaseLevel for rows where it is unset.
            uint32 requiredLevel = spellInfo->SpellLevel ? spellInfo->SpellLevel : spellInfo->BaseLevel;
            if (bot->GetLevel() < requiredLevel)
            {
                TC_LOG_DEBUG("scripts.MidnightBotAI", "Learn: '{}' skips spell {} (requires level {}, bot is {})",
                    bot->GetName(), spellId, requiredLevel, uint32(bot->GetLevel()));
                continue;
            }

            bot->LearnSpell(spellId, false, 0, true);
            ++learned;
            TC_LOG_DEBUG("scripts.MidnightBotAI", "Learn: '{}' learned spell {} (level {})", bot->GetName(), spellId, requiredLevel);
        }

        return learned;
    }

    // Mirrors the in-tree gate used by SpellEffects.cpp:5218 (HasSpell + HasCooldown) plus a
    // cast-in-progress and mana check. Unit::GetSpellHistory() is Unit.h:1516, HasCooldown is
    // SpellHistory.h:172.
    bool CanBotCast(Player* bot, uint32 spellId, Unit* target, float maxRange)
    {
        if (!spellId || !bot->HasSpell(spellId))
            return false;

        if (bot->IsNonMeleeSpellCast(false, false, false) || bot->HasUnitState(UNIT_STATE_CASTING))
            return false;

        SpellHistory* history = bot->GetSpellHistory();
        if (!history || history->HasCooldown(spellId))
            return false;

        if (bot->GetPowerType() == POWER_MANA && bot->GetPowerPct(POWER_MANA) <= 25.0f)
            return false;

        if (target && !bot->IsWithinDist(target, maxRange))
            return false;

        return true;
    }

    // Lowest HP% among owner/self/party below 70%; bound tank bots get a 10% priority bonus so a
    // hurt tank is picked before an equally hurt squishy.
    Player* FindHealTarget(Player* bot, Player* owner, float& outPct, std::vector<ObjectGuid> const& tankGuids)
    {
        Player* best = nullptr;
        float bestScore = 1000.0f;
        float bestRealPct = 0.0f;

        auto consider = [&](Player* candidate)
        {
            if (!candidate || !candidate->IsAlive())
                return;

            float const pct = candidate->GetHealthPct();
            if (pct >= 70.0f)
                return;

            bool const isTank = std::find(tankGuids.begin(), tankGuids.end(), candidate->GetGUID()) != tankGuids.end();
            float const score = pct - (isTank ? 10.0f : 0.0f);

            if (score < bestScore)
            {
                bestScore = score;
                bestRealPct = pct;
                best = candidate;
            }
        };

        consider(owner);
        consider(bot);

        if (Group* group = bot->GetGroup())
            for (GroupReference const& ref : group->GetMembers())
                consider(ref.GetSource());

        outPct = best ? bestRealPct : 0.0f;
        return best;
    }

    // (a) mobs attacking the party, (b) the owner's victim, (c) anything attacking the tank
    // (PetAI::SelectNextTarget, PetAI.cpp:308-350, extended with the party attacker scan).
    Unit* FindTankTarget(Player* bot, Player* owner, char const*& reason, std::vector<std::string>& rejected)
    {
        auto considerAttackers = [&](Player* member) -> Unit*
        {
            if (!member)
                return nullptr;

            for (Unit* attacker : member->getAttackers())
            {
                TargetCheck check = CheckAttackTarget(bot, owner, attacker);
                if (check.rejection)
                {
                    if (attacker && rejected.size() < 5)
                        rejected.push_back(std::string("rejected '") + attacker->GetName() + "' (" + check.rejection + ")");
                    continue;
                }

                reason = check.ownerVisible ? "owner-visible" : "party under attack";
                return attacker;
            }

            return nullptr;
        };

        if (Unit* attacker = considerAttackers(owner))
            return attacker;

        if (Group* group = bot->GetGroup())
            for (GroupReference const& ref : group->GetMembers())
                if (Unit* attacker = considerAttackers(ref.GetSource()))
                    return attacker;

        if (owner)
            if (Unit* ownerVictim = owner->GetVictim())
            {
                TargetCheck check = CheckAttackTarget(bot, owner, ownerVictim);
                if (check.rejection)
                    rejected.push_back(std::string("rejected '") + ownerVictim->GetName() + "' (" + check.rejection + ")");
                else
                {
                    reason = check.ownerVisible ? "owner-visible" : "owner victim";
                    return ownerVictim;
                }
            }

        if (Unit* selfAttacker = bot->getAttackerForHelper())
        {
            TargetCheck check = CheckAttackTarget(bot, owner, selfAttacker);
            if (check.rejection)
                rejected.push_back(std::string("rejected '") + selfAttacker->GetName() + "' (" + check.rejection + ")");
            else
            {
                reason = check.ownerVisible ? "owner-visible" : "self defense";
                return selfAttacker;
            }
        }

        return nullptr;
    }

    Unit* FindDpsTarget(Player* bot, Player* owner, char const*& reason, std::vector<std::string>& rejected)
    {
        auto tryTarget = [&](Unit* candidate, char const* why) -> Unit*
        {
            if (!candidate)
                return nullptr;

            TargetCheck check = CheckAttackTarget(bot, owner, candidate);
            if (check.rejection)
            {
                if (rejected.size() < 5)
                    rejected.push_back(std::string("rejected '") + candidate->GetName() + "' (" + check.rejection + ")");
                return nullptr;
            }

            reason = check.ownerVisible ? "owner-visible" : why;
            return candidate;
        };

        if (owner)
            if (Unit* target = tryTarget(owner->GetVictim(), "owner victim"))
                return target;

        if (Unit* target = tryTarget(bot->getAttackerForHelper(), "self defense"))
            return target;

        if (owner)
            if (Unit* target = tryTarget(owner->getAttackerForHelper(), "defend owner"))
                return target;

        return nullptr;
    }

    enum class GroupAddResult
    {
        Added,
        AlreadyInGroup,
        Failed
    };

    // Same creation path as the core uses when a party invite is accepted
    // (GroupHandler.cpp:219-232): Create() stores the group, AddGroup() registers it.
    GroupAddResult AddPlayerToGroup(Player* owner, Player* bot, std::string& err)
    {
        if (!owner || !owner->IsInWorld())
        {
            err = "an in-game group owner is required";
            return GroupAddResult::Failed;
        }

        if (!bot || bot == owner)
        {
            err = "a bot cannot be added to its own group";
            return GroupAddResult::Failed;
        }

        Group* group = owner->GetGroup();
        if (!group)
        {
            group = new Group();
            if (!group->Create(owner))
            {
                delete group;
                err = "group creation failed";
                return GroupAddResult::Failed;
            }

            sGroupMgr->AddGroup(group);
        }

        if (bot->GetGroup() == group)
        {
            err = "bot is already in that group";
            return GroupAddResult::AlreadyInGroup;
        }

        if (group->IsFull())
        {
            err = "group is full";
            return GroupAddResult::Failed;
        }

        if (Group* oldGroup = bot->GetGroup())
            oldGroup->RemoveMember(bot->GetGUID());

        if (!group->AddMember(bot))
        {
            err = "group AddMember failed";
            return GroupAddResult::Failed;
        }

        group->BroadcastGroupUpdate();

        // Bot-created/auto-created groups must be led by the (real) owner, never by a bot.
        // Group::Create already assigns the leader (Group.cpp:141-149), this is a safety net
        // for groups that somehow ended up with a bot leader.
        if (Player* leader = ObjectAccessor::FindConnectedPlayer(group->GetLeaderGUID()))
            if (leader != owner && leader->GetSession() && leader->GetSession()->IsBot() && group->IsMember(owner->GetGUID()))
            {
                group->ChangeLeader(owner->GetGUID());
                group->SendUpdate();
            }

        // Apply the module's default loot rules so bot parties never stay on FFA.
        sMidnightBotMgr->ApplyDefaultLootRules(group);

        return GroupAddResult::Added;
    }

    // Player teleports in this core are client-acknowledged (suspend token / move ack).
    // Bots have no client, so the teleport state machine has to be completed server-side:
    //  - far teleports  -> WorldSession::HandleMoveWorldportAck() (WorldSession.h:1383 "for server-side calls")
    //  - near teleports -> same steps as WorldSession::HandleMoveTeleportAck (MovementHandler.cpp:426-468)
    bool CompleteBotTeleport(Player* bot)
    {
        if (!bot)
            return false;

        WorldSession* session = bot->GetSession();

        for (uint32 attempt = 0; attempt < 3 && bot->IsBeingTeleported(); ++attempt)
        {
            if (bot->IsBeingTeleportedFar())
            {
                if (!session)
                    return false;

                session->HandleMoveWorldportAck();
            }
            else if (bot->IsBeingTeleportedNear())
            {
                bot->SetTeleportState(TeleportState::NotTeleporting);
                bot->UpdatePosition(bot->GetTeleportDest().Location, true);
                bot->SetFallInformation(0, bot->GetPositionZ());

                uint32 newZone = 0, newArea = 0;
                bot->GetZoneAndAreaId(newZone, newArea);
                bot->UpdateZone(newZone, newArea);

                bot->ResummonPetTemporaryUnSummonedIfAny();
                bot->ProcessDelayedOperations();
            }
            else
                break;
        }

        return !bot->IsBeingTeleported();
    }

    bool TeleportBotToPlayer(Player* bot, Player* target, uint32 spreadIndex, std::string& err)
    {
        if (!bot || !target)
        {
            err = "internal error: missing player";
            return false;
        }

        if (!bot->IsInWorld())
        {
            err = "bot is not in the world yet";
            return false;
        }

        if (bot->IsBeingTeleported())
        {
            err = "bot is already being teleported";
            return false;
        }

        // Spread bots in a loose arc around the owner instead of stacking them on one spot:
        // angle = spreadIndex * 45 degrees (wraps every 8 bots), added to the owner orientation,
        // radius = 3.0 + (spreadIndex / 8) * 1.5 yards. spreadIndex is the bot's position in
        // activeBots, so two active bots can never produce the same offset.
        float const angle = target->GetOrientation() + float(spreadIndex) * float(M_PI) / 4.0f;
        float const radius = 3.0f + float(spreadIndex / 8) * 1.5f;
        float const x = target->GetPositionX() + std::cos(angle) * radius;
        float const y = target->GetPositionY() + std::sin(angle) * radius;
        float const z = target->GetPositionZ();

        if (!bot->TeleportTo(target->GetMapId(), x, y, z, target->GetOrientation(), TELE_TO_NONE, target->GetInstanceId()))
        {
            err = "TeleportTo was refused (invalid or disabled destination)";
            return false;
        }

        if (!CompleteBotTeleport(bot))
        {
            err = "teleport could not be completed";
            return false;
        }

        // Bots are players, so their phase shift is not inherited automatically (unlike pets).
        // Copy the owner's phases and force a visibility refresh so nearby clients receive the
        // bot's create/update objects at the destination (Player::UpdateObjectVisibility, Player.cpp:24796).
        InheritOwnerPhase(bot, target);
        bot->UpdateObjectVisibility(true);

        return true;
    }

    MidnightBotMgr::ActiveBotInfo BuildBotInfo(MidnightBotMgr::BotRef const& ref, Player* viewer)
    {
        MidnightBotMgr::ActiveBotInfo info;
        info.name = ref.name;
        info.guid = ref.guid;
        info.playerGuid = ref.playerGuid;
        info.ownerGuid = ref.ownerGuid;
        info.follow = ref.follow;
        info.assist = ref.assist;
        info.roleName = RoleName(ref.role);
        info.inWorld = ref.inWorld;

        Player* owner = FindInWorldPlayer(ref.ownerGuid);
        if (owner)
        {
            info.ownerName = owner->GetName();
            info.ownerPhases = PhasingHandler::FormatPhases(owner->GetPhaseShift());
        }

        if (Player* bot = ObjectAccessor::FindConnectedPlayer(ref.playerGuid))
        {
            info.resolved = true;
            info.inWorld = bot->IsInWorld();
            info.mapId = bot->GetMapId();
            info.zoneId = bot->GetZoneId();
            info.x = bot->GetPositionX();
            info.y = bot->GetPositionY();
            info.z = bot->GetPositionZ();
            info.phases = PhasingHandler::FormatPhases(bot->GetPhaseShift());

            info.motion = MovementTypeName(bot->GetMotionMaster()->GetCurrentMovementGeneratorType());
            if (bot->HasUnitState(UNIT_STATE_FOLLOW))
                info.motion += "(follow)";
            else if (bot->HasUnitState(UNIT_STATE_CHASE))
                info.motion += "(chase)";

            if (owner)
                info.canSeeOwner = bot->InSamePhase(owner);

            if (viewer && viewer->GetMapId() == bot->GetMapId() && viewer->GetInstanceId() == bot->GetInstanceId())
                info.distanceToViewer = viewer->GetExactDist(bot);
        }

        return info;
    }
}

MidnightBotMgr* MidnightBotMgr::instance()
{
    static MidnightBotMgr instance;
    return &instance;
}

void MidnightBotMgr::LoadConfig()
{
    std::lock_guard<std::mutex> lock(rosterMutex);

    enabled = sConfigMgr->GetBoolDefault("MidnightBotAI.Enable", true);
    spawning = sConfigMgr->GetBoolDefault("MidnightBotAI.Spawning", false);
    autoSpawn = sConfigMgr->GetBoolDefault("MidnightBotAI.AutoSpawn", false);
    summonOnSpawn = sConfigMgr->GetBoolDefault("MidnightBotAI.SummonOnSpawn", false);
    autoParty = sConfigMgr->GetBoolDefault("MidnightBotAI.AutoParty", false);
    combatLog = sConfigMgr->GetBoolDefault("MidnightBotAI.CombatLog", true);
    combatChat = sConfigMgr->GetBoolDefault("MidnightBotAI.CombatChat", true);
    stats = sConfigMgr->GetBoolDefault("MidnightBotAI.Stats", true);
    debugAuto = sConfigMgr->GetBoolDefault("MidnightBotAI.DebugAuto", true);
    maxBots = uint32(sConfigMgr->GetIntDefault("MidnightBotAI.MaxBots", 8));
    accountPrefix = sConfigMgr->GetStringDefault("MidnightBotAI.AccountPrefix", "mbot");
    bootstrap = sConfigMgr->GetStringDefault("MidnightBotAI.Bootstrap", "");
    debugFile = sConfigMgr->GetStringDefault("MidnightBotAI.DebugFile", "mbot-debug.txt");
    lootMethod = sConfigMgr->GetStringDefault("MidnightBotAI.LootMethod", "group");
    lootThreshold = sConfigMgr->GetStringDefault("MidnightBotAI.LootThreshold", "uncommon");
    lootRoll = sConfigMgr->GetStringDefault("MidnightBotAI.LootRoll", "pass");
    levelSync = sConfigMgr->GetBoolDefault("MidnightBotAI.LevelSync", true);
    autoTalents = sConfigMgr->GetBoolDefault("MidnightBotAI.AutoTalents", true);
}

void MidnightBotMgr::LoadRoster()
{
    CharacterDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS midnight_bot_roster (guid INT UNSIGNED NOT NULL PRIMARY KEY, name VARCHAR(64) NOT NULL)");
    CharacterDatabase.DirectExecute("ALTER TABLE midnight_bot_roster ADD COLUMN IF NOT EXISTS owner_guid BIGINT UNSIGNED NOT NULL DEFAULT 0");

    bool hasOwnerColumn = true;
    QueryResult result = CharacterDatabase.Query("SELECT guid, name, owner_guid FROM midnight_bot_roster");
    if (!result)
    {
        hasOwnerColumn = false;
        result = CharacterDatabase.Query("SELECT guid, name FROM midnight_bot_roster");
    }

    if (!result)
    {
        TC_LOG_WARN("scripts.MidnightBotAI", "Could not read 'midnight_bot_roster' from the characters database; continuing with the in-memory roster only");
        return;
    }

    std::lock_guard<std::mutex> lock(rosterMutex);

    roster.clear();
    rosterOwners.clear();

    uint32 loaded = 0;
    do
    {
        Field* fields = result->Fetch();
        uint32 guid = fields[0].GetUInt32();
        std::string name = fields[1].GetString();

        if (!guid || name.empty())
        {
            TC_LOG_WARN("scripts.MidnightBotAI", "Ignoring invalid midnight_bot_roster row (guid {}, empty name)", guid);
            continue;
        }

        if (roster.size() >= maxBots)
        {
            TC_LOG_WARN("scripts.MidnightBotAI", "midnight_bot_roster holds more bots than MidnightBotAI.MaxBots ({}); ignoring the remaining rows", maxBots);
            break;
        }

        roster.emplace_back(guid, name);

        if (hasOwnerColumn)
        {
            uint64 ownerLow = fields[2].GetUInt64();
            if (ownerLow)
                rosterOwners[guid] = ObjectGuid::Create<HighGuid::Player>(ownerLow);
        }

        ++loaded;
    } while (result->NextRow());

    TC_LOG_INFO("scripts.MidnightBotAI", "Loaded {} bot(s) from midnight_bot_roster ({}/{}) with {} owner binding(s)", loaded, roster.size(), maxBots, rosterOwners.size());
}

// Persists a bot's owner binding. Must be called with rosterMutex held; command paths only
// (never the update tick).
void MidnightBotMgr::SaveBotOwnerLocked(uint32 botGuid, ObjectGuid ownerGuid)
{
    rosterOwners[botGuid] = ownerGuid;
    CharacterDatabase.DirectPExecute("UPDATE midnight_bot_roster SET owner_guid = {} WHERE guid = {}", ownerGuid.GetCounter(), botGuid);
}

void MidnightBotMgr::SetVersion(std::string const& value)
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    versionString = value;
}

// MidnightBotAI.Bootstrap = "Name:Race:Class;Name:Race:Class;..."
// Creates missing characters through the normal CreateBot path and makes sure every entry is
// rostered. Only creates/rosters, never logs in - AutoSpawn (or '.mbot spawnall') does that.
void MidnightBotMgr::BootstrapRoster()
{
    std::string list = BootstrapList();
    if (list.empty())
        return;

    if (!IsEnabled())
    {
        TC_LOG_INFO("scripts.MidnightBotAI", "Bootstrap: skipped, module disabled by MidnightBotAI.Enable = 0");
        return;
    }

    auto trim = [](std::string& value)
    {
        size_t first = value.find_first_not_of(" \t\r\n");
        size_t last = value.find_last_not_of(" \t\r\n");
        if (first == std::string::npos)
            value.clear();
        else
            value = value.substr(first, last - first + 1);
    };

    uint32 created = 0;
    uint32 existing = 0;
    uint32 added = 0;
    uint32 failed = 0;

    size_t start = 0;
    while (start <= list.size())
    {
        size_t end = list.find(';', start);
        std::string entry = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = (end == std::string::npos) ? list.size() + 1 : end + 1;

        trim(entry);
        if (entry.empty())
            continue;

        size_t firstColon = entry.find(':');
        size_t secondColon = (firstColon == std::string::npos) ? std::string::npos : entry.find(':', firstColon + 1);
        if (firstColon == std::string::npos || secondColon == std::string::npos)
        {
            ++failed;
            TC_LOG_WARN("scripts.MidnightBotAI", "Bootstrap: failed '{}': expected Name:Race:Class", entry);
            continue;
        }

        std::string name = entry.substr(0, firstColon);
        std::string raceText = entry.substr(firstColon + 1, secondColon - firstColon - 1);
        size_t thirdColon = entry.find(':', secondColon + 1);
        std::string classText = (thirdColon == std::string::npos)
            ? entry.substr(secondColon + 1)
            : entry.substr(secondColon + 1, thirdColon - secondColon - 1);
        std::string roleText = (thirdColon == std::string::npos) ? std::string() : entry.substr(thirdColon + 1);
        trim(name);
        trim(raceText);
        trim(classText);
        trim(roleText);

        std::string nameErr;
        if (!ValidateBotName(name, nameErr))
        {
            ++failed;
            TC_LOG_WARN("scripts.MidnightBotAI", "Bootstrap: failed '{}': {}", entry, nameErr);
            continue;
        }

        char* raceEnd = nullptr;
        char* classEnd = nullptr;
        unsigned long raceId = std::strtoul(raceText.c_str(), &raceEnd, 10);
        unsigned long classId = std::strtoul(classText.c_str(), &classEnd, 10);
        if (!raceEnd || raceText.empty() || *raceEnd != '\0' || !classEnd || classText.empty() || *classEnd != '\0' ||
            raceId > 255 || classId > 255)
        {
            ++failed;
            TC_LOG_WARN("scripts.MidnightBotAI", "Bootstrap: failed '{}': race and class must be numeric ids", name);
            continue;
        }

        BotRole role = DefaultRoleForClass(uint8(classId));
        if (!roleText.empty() && !ParseRoleString(roleText, role))
        {
            ++failed;
            TC_LOG_WARN("scripts.MidnightBotAI", "Bootstrap: failed '{}': invalid role '{}' (use tank/healer/dps/none)", name, roleText);
            continue;
        }

        bootstrapRoles[name] = role;

        std::string escapedName = name;
        CharacterDatabase.EscapeString(escapedName);
        QueryResult result = CharacterDatabase.PQuery("SELECT guid FROM characters WHERE name = '{}'", escapedName);

        if (!result)
        {
            std::string createErr;
            uint32 newGuid = 0;
            if (!CreateBot(name, uint8(raceId), uint8(classId), createErr, newGuid))
            {
                ++failed;
                TC_LOG_WARN("scripts.MidnightBotAI", "Bootstrap: failed '{}': {}", name, createErr);
                continue;
            }

            ++created;
            TC_LOG_INFO("scripts.MidnightBotAI", "Bootstrap: created bot '{}' (race {}, class {})", name, uint32(raceId), uint32(classId));
        }
        else
        {
            ++existing;
            TC_LOG_INFO("scripts.MidnightBotAI", "Bootstrap: '{}' already exists", name);
        }

        if (!IsBotInRoster(name))
        {
            std::string addErr;
            if (AddBotByName(name, addErr))
            {
                ++added;
                TC_LOG_INFO("scripts.MidnightBotAI", "Bootstrap: added '{}' to roster", name);
            }
            else
            {
                ++failed;
                TC_LOG_WARN("scripts.MidnightBotAI", "Bootstrap: failed '{}': {}", name, addErr);
            }
        }
    }

    TC_LOG_INFO("scripts.MidnightBotAI", "Bootstrap finished: {} created, {} already existed, {} added to roster, {} failed", created, existing, added, failed);
}

bool MidnightBotMgr::IsEnabled() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return enabled;
}

void MidnightBotMgr::SetRuntimeEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    this->enabled = enabled;
}

bool MidnightBotMgr::SpawningEnabled() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return spawning;
}

bool MidnightBotMgr::AutoSpawnEnabled() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return autoSpawn;
}

bool MidnightBotMgr::SummonOnSpawnEnabled() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return summonOnSpawn;
}

bool MidnightBotMgr::AutoPartyEnabled() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return autoParty;
}

bool MidnightBotMgr::CombatLogEnabled() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return combatLog;
}

bool MidnightBotMgr::StatsEnabled() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return stats;
}

bool MidnightBotMgr::LevelSyncEnabled() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return levelSync;
}

bool MidnightBotMgr::AutoTalentsEnabled() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return autoTalents;
}

std::string MidnightBotMgr::LootRollMode() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return lootRoll;
}

uint32 MidnightBotMgr::MaxBots() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return maxBots;
}

std::string MidnightBotMgr::AccountPrefix() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return accountPrefix;
}

std::string MidnightBotMgr::BootstrapList() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return bootstrap;
}

std::string MidnightBotMgr::DebugFile() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return debugFile;
}

bool MidnightBotMgr::CreateBot(std::string const& botName, uint8 race, uint8 classId, std::string& err, uint32& outGuid)
{
    outGuid = 0;

    if (!IsEnabled())
    {
        err = "module disabled";
        return false;
    }

    std::string name = botName;
    if (!ValidateBotName(name, err))
        return false;

    std::string accountName = AccountPrefix() + "_" + name;
    Utf8ToUpperOnlyLatin(accountName);

    size_t maxAccountNameLength = MAX_ACCOUNT_STR[sWorld->getBoolConfig(CONFIG_EXTENDED_ACCOUNT_NAME_LENGTH_LIMIT)];
    if (accountName.length() > maxAccountNameLength)
    {
        err = "account name '" + accountName + "' exceeds the core limit of " + std::to_string(maxAccountNameLength) + " characters; use a shorter bot name";
        return false;
    }

    bool accountCreated = false;
    uint32 accountId = AccountMgr::GetId(accountName);
    if (!accountId)
    {
        switch (sAccountMgr->CreateAccount(accountName, name, accountName))
        {
            case AccountOpResult::AOR_OK:
                break;
            case AccountOpResult::AOR_NAME_TOO_LONG:
                err = "account name is too long";
                return false;
            case AccountOpResult::AOR_PASS_TOO_LONG:
                err = "bot password is too long (max " + std::to_string(MAX_PASS_STR) + " characters)";
                return false;
            case AccountOpResult::AOR_EMAIL_TOO_LONG:
                err = "account email is too long";
                return false;
            case AccountOpResult::AOR_NAME_ALREADY_EXIST:
                err = "account '" + accountName + "' already exists";
                return false;
            default:
                err = "account creation failed with an internal database error";
                return false;
        }

        accountId = AccountMgr::GetId(accountName);
        if (!accountId)
        {
            err = "account creation could not be verified";
            return false;
        }

        accountCreated = true;

        // Let the bot account use everything the realm supports, otherwise most races/classes are locked behind expansion checks.
        LoginDatabase.DirectPExecute("UPDATE account SET expansion = {} WHERE id = {}", uint32(sWorld->getIntConfig(CONFIG_EXPANSION)), accountId);

        // SpawnBot refuses accounts below the realm player security limit; normal accounts are security level 0.
        AccountTypes securityLimit = sWorld->GetPlayerSecurityLimit();
        if (securityLimit > SEC_PLAYER)
            LoginDatabase.DirectPExecute("INSERT INTO account_access (AccountID, SecurityLevel, RealmID) VALUES ({}, {}, -1)", accountId, uint32(securityLimit));
    }

    uint8 accountExpansion = 0;
    if (QueryResult accountResult = LoginDatabase.PQuery("SELECT expansion FROM account WHERE id = {}", accountId))
        accountExpansion = accountResult->Fetch()[0].GetUInt8();

    if (!race || !classId)
    {
        err = "race and class must be non-zero ids";
        return false;
    }

    ChrRacesEntry const* raceEntry = sChrRacesStore.LookupEntry(race);
    if (!raceEntry)
    {
        err = "invalid race id " + std::to_string(uint32(race));
        return false;
    }

    if (!sChrClassesStore.LookupEntry(classId))
    {
        err = "invalid class id " + std::to_string(uint32(classId));
        return false;
    }

    if (raceEntry->GetFlags().HasFlag(ChrRacesFlag::NPCOnly))
    {
        err = "race " + std::to_string(uint32(race)) + " is not playable";
        return false;
    }

    Trinity::RaceMask<uint64> disabledRaces{ sWorld->GetUInt64Config(CONFIG_CHARACTER_CREATING_DISABLED_RACEMASK) };
    if (disabledRaces.HasRace(race))
    {
        err = "race creation is disabled on this realm";
        return false;
    }

    if ((1u << (uint32(classId) - 1)) & uint32(sWorld->getIntConfig(CONFIG_CHARACTER_CREATING_DISABLED_CLASSMASK)))
    {
        err = "class creation is disabled on this realm";
        return false;
    }

    if (!sObjectMgr->GetPlayerInfo(race, classId))
    {
        err = "no playercreateinfo entry for race/class " + std::to_string(uint32(race)) + "/" + std::to_string(uint32(classId));
        return false;
    }

    if (RaceUnlockRequirement const* raceRequirement = sObjectMgr->GetRaceUnlockRequirement(race))
    {
        if (raceRequirement->Expansion > accountExpansion)
        {
            err = "race requires account expansion " + std::to_string(uint32(raceRequirement->Expansion)) + " but the bot account has " + std::to_string(uint32(accountExpansion));
            return false;
        }
    }

    ClassAvailability const* classRequirement = sObjectMgr->GetClassExpansionRequirement(race, classId);
    if (!classRequirement)
        classRequirement = sObjectMgr->GetClassExpansionRequirementFallback(classId);

    if (classRequirement)
    {
        if (classRequirement->AccountExpansionLevel > accountExpansion)
        {
            err = "class requires account expansion " + std::to_string(uint32(classRequirement->AccountExpansionLevel)) + " but the bot account has " + std::to_string(uint32(accountExpansion));
            return false;
        }

        if (classRequirement->ActiveExpansionLevel > uint8(sWorld->getIntConfig(CONFIG_EXPANSION)))
        {
            err = "class requires active expansion " + std::to_string(uint32(classRequirement->ActiveExpansionLevel)) + " but the realm is at " + std::to_string(sWorld->getIntConfig(CONFIG_EXPANSION));
            return false;
        }
    }

    CharacterDatabasePreparedStatement* nameStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHECK_NAME);
    nameStmt->setString(0, name);
    if (CharacterDatabase.Query(nameStmt))
    {
        err = "a character named '" + name + "' already exists";
        return false;
    }

    WorldSession* session = CreateSessionForAccount(accountId, err);
    if (!session)
        return false;

    ObjectGuid charGuid = ObjectGuid::Create<HighGuid::Player>(sObjectMgr->GetGenerator<HighGuid::Player>().Generate());

    {
        WorldPackets::Character::CharacterCreateInfo createInfo;
        createInfo.Name = name;
        createInfo.Race = race;
        createInfo.Class = classId;
        createInfo.Sex = GENDER_MALE;
        createInfo.UseNPE = false;

        std::shared_ptr<Player> newChar(new Player(session), [](Player* ptr)
        {
            ptr->CleanupsBeforeDelete();
            delete ptr;
        });
        newChar->GetMotionMaster()->Initialize();

        if (!newChar->Create(charGuid.GetCounter(), &createInfo))
        {
            delete session;
            err = "Player::Create failed, see the server log for details";
            return false;
        }

        newChar->SetAtLoginFlag(AT_LOGIN_FIRST);

        // Learn the role's basic abilities before SaveToDB so they are persisted with the
        // character in the same transaction (_SaveSpells, Player.cpp:21503).
        BotRole createRole = DefaultRoleForClass(classId);
        if (auto roleItr = bootstrapRoles.find(name); roleItr != bootstrapRoles.end())
            createRole = roleItr->second;

        uint32 learnedSpells = LearnRoleSpells(newChar.get(), createRole);
        if (learnedSpells)
            TC_LOG_INFO("scripts.MidnightBotAI", "Created bot '{}' learned {} role spell(s)", name, learnedSpells);

        CharacterDatabaseTransaction charTransaction = CharacterDatabase.BeginTransaction();
        LoginDatabaseTransaction loginTransaction = LoginDatabase.BeginTransaction();

        newChar->SaveToDB(loginTransaction, charTransaction, true);

        uint32 charCount = AccountMgr::GetCharactersCount(accountId) + 1;
        LoginDatabasePreparedStatement* realmStmt = LoginDatabase.GetPreparedStatement(LOGIN_REP_REALM_CHARACTERS);
        realmStmt->setUInt32(0, charCount);
        realmStmt->setUInt32(1, accountId);
        realmStmt->setUInt32(2, sRealmList->GetCurrentRealmId().Realm);
        loginTransaction->Append(realmStmt);

        TransactionCallback charCommit = CharacterDatabase.AsyncCommitTransaction(charTransaction);
        bool charCommitted = false;
        charCommit.AfterComplete([&charCommitted](bool success) { charCommitted = success; });

        TransactionCallback loginCommit = LoginDatabase.AsyncCommitTransaction(loginTransaction);
        bool loginCommitted = false;
        loginCommit.AfterComplete([&loginCommitted](bool success) { loginCommitted = success; });

        std::chrono::steady_clock::time_point commitDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while ((!charCommitted || !loginCommitted) && std::chrono::steady_clock::now() < commitDeadline)
        {
            charCommit.InvokeIfReady();
            loginCommit.InvokeIfReady();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (!charCommitted || !loginCommitted)
            TC_LOG_ERROR("scripts.MidnightBotAI", "Bot character '{}' (guid {}) was not committed within 10 seconds; '.mbot add'/'spawn' may not find it yet", name, charGuid.GetCounter());

        sScriptMgr->OnPlayerCreate(newChar.get());
        sCharacterCache->AddCharacterCacheEntry(charGuid, accountId, newChar->GetName(), newChar->GetNativeGender(), newChar->GetRace(), newChar->GetClass(), newChar->GetLevel(), false);

        outGuid = charGuid.GetCounter();
    }

    delete session;

    TC_LOG_INFO("scripts.MidnightBotAI", "Created bot character '{}' (guid {}, race {}, class {}) on account '{}' (id {}, account {})",
        name, outGuid, uint32(race), uint32(classId), accountName, accountId, accountCreated ? "created" : "reused");
    return true;
}

bool MidnightBotMgr::AddBotByName(std::string const& name, std::string& err)
{
    if (name.empty())
    {
        err = "character name is empty";
        return false;
    }

    std::lock_guard<std::mutex> lock(rosterMutex);

    for (auto const& bot : roster)
    {
        if (bot.second == name)
        {
            err = "already in roster";
            return false;
        }
    }

    if (roster.size() >= maxBots)
    {
        err = "roster full";
        return false;
    }

    QueryResult result = CharacterDatabase.PQuery("SELECT guid, name FROM characters WHERE name = '{}'", name);
    if (!result)
    {
        err = "character not found";
        return false;
    }

    Field* fields = result->Fetch();
    uint32 guid = fields[0].GetUInt32();
    std::string characterName = fields[1].GetString();

    roster.emplace_back(guid, characterName);

    std::string escapedName = characterName;
    CharacterDatabase.EscapeString(escapedName);
    CharacterDatabase.DirectPExecute("INSERT INTO midnight_bot_roster (guid, name) VALUES ({}, '{}')", guid, escapedName);

    TC_LOG_INFO("scripts.MidnightBotAI", "Added bot '{}' (guid {}) to roster ({}/{})", characterName, guid, roster.size(), maxBots);
    return true;
}

bool MidnightBotMgr::RemoveBotByName(std::string const& name)
{
    std::lock_guard<std::mutex> lock(rosterMutex);

    auto itr = std::find_if(roster.begin(), roster.end(), [&name](std::pair<uint32, std::string> const& bot)
    {
        return bot.second == name;
    });

    if (itr == roster.end())
        return false;

    uint32 guid = itr->first;
    std::string characterName = itr->second;

    TC_LOG_INFO("scripts.MidnightBotAI", "Removed bot '{}' (guid {}) from roster ({} left)", characterName, guid, roster.size() - 1);
    roster.erase(itr);

    CharacterDatabase.DirectPExecute("DELETE FROM midnight_bot_roster WHERE guid = {}", guid);
    return true;
}

std::vector<std::string> MidnightBotMgr::ListBots() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);

    std::vector<std::string> bots;
    bots.reserve(roster.size());
    for (auto const& bot : roster)
        bots.push_back(bot.second);

    return bots;
}

size_t MidnightBotMgr::Count() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return roster.size();
}

bool MidnightBotMgr::SpawnBot(std::string const& name, std::string& err, ObjectGuid ownerGuid)
{
    if (name.empty())
    {
        err = "character name is empty";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        if (!enabled)
        {
            err = "module disabled";
            return false;
        }

        if (!spawning)
        {
            err = "spawning disabled by MidnightBotAI.Spawning = 0";
            return false;
        }

        for (BotRef const& bot : activeBots)
        {
            if (bot.name == name)
            {
                err = "bot already active";
                return false;
            }
        }

        if (activeBots.size() >= maxBots)
        {
            err = "active bot limit reached";
            return false;
        }
    }

    QueryResult result = CharacterDatabase.PQuery("SELECT guid, account, name FROM characters WHERE name = '{}'", name);
    if (!result)
    {
        err = "character not found";
        return false;
    }

    Field* fields = result->Fetch();
    uint32 guid = fields[0].GetUInt32();
    uint32 accountId = fields[1].GetUInt32();
    std::string characterName = fields[2].GetString();

    QueryResult accountResult = LoginDatabase.PQuery("SELECT username, email, expansion, mutetime, client_build, locale, os, timezone_offset, recruiter, battlenet_account FROM account WHERE id = {}", accountId);
    if (!accountResult)
    {
        err = "account not found";
        return false;
    }

    Field* accountFields = accountResult->Fetch();
    std::string accountName = accountFields[0].GetString();
    std::string accountEmail = accountFields[1].GetString();
    uint8 expansion = accountFields[2].GetUInt8();
    time_t muteTime = time_t(accountFields[3].GetInt64());
    uint32 clientBuild = accountFields[4].GetUInt32();
    uint8 locale = accountFields[5].GetUInt8();
    std::string os = accountFields[6].GetString();
    int16 timezoneOffset = accountFields[7].GetInt16();
    uint32 recruiter = accountFields[8].GetUInt32();
    uint32 battlenetAccountId = accountFields[9].IsNull() ? 0 : accountFields[9].GetUInt32();

    if (sWorld->FindSession(accountId))
    {
        err = "account already has an active session";
        return false;
    }

    AccountTypes security = AccountTypes(sAccountMgr->GetSecurity(accountId, sRealmList->GetCurrentRealmId().Realm));
    if (security < sWorld->GetPlayerSecurityLimit())
    {
        err = "account security level is below the realm limit";
        return false;
    }

    WorldSession* session = new WorldSession(accountId, std::move(accountName), battlenetAccountId, std::move(accountEmail),
        std::shared_ptr<WorldSocket>(), security, expansion, muteTime, std::move(os), Minutes(timezoneOffset), clientBuild,
        ClientBuild::VariantId{ ClientBuild::Platform::Win_x64, ClientBuild::Arch::x64, ClientBuild::Type::Retail },
        LocaleConstant(locale), recruiter, false);
    session->SetBot(true);

    ObjectGuid playerGuid = ObjectGuid::Create<HighGuid::Player>(guid);
    if (!session->HandleBotLogin(playerGuid))
    {
        delete session;
        err = "character login failed, check server log for details";
        return false;
    }

    sWorld->AddSession(session);

    BotRole role = BotRole::None;
    if (Player* botPlayer = session->GetPlayer())
        role = DefaultRoleForClass(botPlayer->GetClass());

    if (auto roleItr = bootstrapRoles.find(characterName); roleItr != bootstrapRoles.end())
        role = roleItr->second;

    // Restore a persisted owner binding (server start / autospawn). Manual spawns keep their issuer.
    ObjectGuid effectiveOwner = ownerGuid;
    bool const autoBound = effectiveOwner.IsEmpty();

    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        if (autoBound)
            if (auto ownerItr = rosterOwners.find(guid); ownerItr != rosterOwners.end())
                effectiveOwner = ownerItr->second;
    }

    bool const hasOwner = !effectiveOwner.IsEmpty();

    uint32 spreadIndex = 0;
    {
        std::lock_guard<std::mutex> lock(rosterMutex);
        spreadIndex = uint32(activeBots.size());
        activeBots.push_back(BotRef{ guid, characterName, playerGuid, accountId, session, false, false, effectiveOwner, hasOwner, hasOwner, role });
        activeBots.back().spacingAngle = SpacingAngleForGuid(guid);
        activeBots.back().spacingDist = SpacingDistForGuid(guid);
    }

    // Manual binding via '.mb spawn' is persisted; autobind is already in the table.
    if (!ownerGuid.IsEmpty())
    {
        std::lock_guard<std::mutex> lock(rosterMutex);
        SaveBotOwnerLocked(guid, ownerGuid);
    }

    // Bots often log in dead after a previous wipe; bring them up before any owner features run.
    if (Player* botPlayer = session->GetPlayer())
    {
        if (EnsureBotAlive(botPlayer))
            LogAi(playerGuid, characterName + ": resurrected (was dead)");

        // Existing bots created before auto-learn get their missing role spells here.
        uint32 learnedSpells = LearnRoleSpells(botPlayer, role);
        if (learnedSpells)
            LogAi(playerGuid, characterName + ": learned " + std::to_string(learnedSpells) + " spell(s)");
    }

    TC_LOG_INFO("scripts.MidnightBotAI", "Spawned bot '{}' (guid {}, account {}) on a socketless session", characterName, guid, accountId);

    // Owner-only conveniences. They never run for AutoSpawn without a binding (no owner there) and
    // therefore never touch the database from the update tick.
    if (hasOwner)
    {
        Player* owner = FindInWorldPlayer(effectiveOwner);
        if (!owner)
        {
            if (!autoBound)
                TC_LOG_WARN("scripts.MidnightBotAI", "SpawnBot: owner {} of bot '{}' is not in the world; skipping SummonOnSpawn/AutoParty", effectiveOwner.ToString(), characterName);

            return true;
        }

        if (autoBound)
        {
            LogAi(playerGuid, characterName + ": autobind: bound to '" + owner->GetName() + "' (follow/assist on)");
            TC_LOG_INFO("scripts.MidnightBotAI", "AutoBind: bot '{}' bound to '{}' (follow/assist on)", characterName, owner->GetName());
        }

        Player* bot = session->GetPlayer();
        if (!bot)
        {
            TC_LOG_WARN("scripts.MidnightBotAI", "SpawnBot: bot '{}' has no player after login; skipping SummonOnSpawn/AutoParty", characterName);
            return true;
        }

        // Level sync on bind/autobind so a returning bot starts at the owner's level.
        if (levelSync && bot->GetLevel() != owner->GetLevel())
        {
            std::string syncErr;
            if (BoostBot(characterName, owner, owner->GetLevel(), syncErr))
                LogAi(playerGuid, characterName + ": sync: -> level " + std::to_string(uint32(owner->GetLevel())));
        }

        if (summonOnSpawn && !autoBound)
        {
            std::string teleportErr;
            if (TeleportBotToPlayer(bot, owner, spreadIndex, teleportErr))
                TC_LOG_INFO("scripts.MidnightBotAI", "SpawnBot: moved bot '{}' to owner '{}' (SummonOnSpawn, spread slot {})", characterName, owner->GetName(), spreadIndex);
            else
                TC_LOG_WARN("scripts.MidnightBotAI", "SpawnBot: SummonOnSpawn failed for bot '{}': {}", characterName, teleportErr);
        }

        if (autoParty || autoBound)
        {
            std::string partyErr;
            if (AddBotToGroup(characterName, owner, partyErr))
                TC_LOG_INFO("scripts.MidnightBotAI", "SpawnBot: added bot '{}' to the group of '{}' ({})", characterName, owner->GetName(), autoBound ? "autobind" : "AutoParty");
            else if (!autoBound || partyErr != "bot is already in that group")
                TC_LOG_WARN("scripts.MidnightBotAI", "SpawnBot: party add failed for bot '{}': {}", characterName, partyErr);
        }
    }

    return true;
}

bool MidnightBotMgr::DespawnBot(std::string const& name, std::string& err)
{
    std::lock_guard<std::mutex> lock(rosterMutex);

    auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&name](BotRef const& bot)
    {
        return bot.name == name;
    });

    if (itr == activeBots.end())
    {
        err = "bot is not active";
        return false;
    }

    WorldSession* session = itr->session;
    if (!session)
    {
        activeBots.erase(itr);
        err = "bot session is missing";
        return false;
    }

    uint32 accountId = itr->accountId;
    bool wasInWorld = itr->inWorld;
    bool registered = (sWorld->FindSession(accountId) == session);
    bool queued = (sWorld->GetQueuePos(session) > 0);

    if (!registered && !queued)
    {
        if (!wasInWorld)
        {
            err = "bot session is still being registered, try again in a moment";
            return false;
        }

        activeBots.erase(itr);
        err = "bot session is no longer registered";
        return false;
    }

    session->LogoutPlayer(true);

    activeBots.erase(itr);

    session->SetBot(false);

    if (!registered)
    {
        sWorld->RemoveQueuedPlayer(session);
        delete session;
    }

    TC_LOG_INFO("scripts.MidnightBotAI", "Despawned bot '{}'", name);
    return true;
}

size_t MidnightBotMgr::ActiveCount() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return activeBots.size();
}

bool MidnightBotMgr::IsBotActive(std::string const& name) const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return std::any_of(activeBots.begin(), activeBots.end(), [&name](BotRef const& bot)
    {
        return bot.name == name;
    });
}

bool MidnightBotMgr::IsBotInRoster(std::string const& name) const
{
    std::lock_guard<std::mutex> lock(rosterMutex);
    return std::any_of(roster.begin(), roster.end(), [&name](std::pair<uint32, std::string> const& bot)
    {
        return bot.second == name;
    });
}

std::vector<std::string> MidnightBotMgr::ActiveBotNames() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);

    std::vector<std::string> names;
    names.reserve(activeBots.size());
    for (BotRef const& bot : activeBots)
        names.push_back(bot.name);

    return names;
}

// Records an AI decision in the bot's ring buffer (always) and the server log (when
// MidnightBotAI.CombatLog = 1). Duplicate consecutive messages are collapsed.
void MidnightBotMgr::LogAi(ObjectGuid botGuid, std::string const& message)
{
    bool appended = false;
    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&botGuid](BotRef const& bot)
        {
            return bot.playerGuid == botGuid;
        });

        if (itr != activeBots.end())
            appended = PushBotAiLog(*itr, message);
    }

    if (appended && combatLog)
        TC_LOG_INFO("scripts.MidnightBotAI", "AI {}", message);
}

// Called from the module's UnitScript::OnDamage (ScriptMgr.h:428, dispatched by
// Unit::DealDamage at Unit.cpp:847). Gives exact damage attribution per bot.
void MidnightBotMgr::NotifyBotDamage(Unit* attacker, Unit* victim, uint32 damage)
{
    if (!attacker || !victim || !damage)
        return;

    Player* bot = attacker->ToPlayer();
    if (!bot || !bot->GetSession() || !bot->GetSession()->IsBot())
        return;

    std::string source = "melee";
    if (Spell* spell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        if (SpellInfo const* spellInfo = spell->GetSpellInfo())
            source = "spell " + std::to_string(spellInfo->Id);

    if (source == "melee")
        if (Spell* autoRepeat = bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
            if (SpellInfo const* spellInfo = autoRepeat->GetSpellInfo())
                source = "spell " + std::to_string(spellInfo->Id);

    LogAi(bot->GetGUID(), "DMG BOT '" + bot->GetName() + "' -> '" + victim->GetName() + "' " + std::to_string(damage) + " (" + source + ")");

    AddBotDamage(bot->GetGUID(), damage);
    SendCombatChat(bot->GetGUID(), "[MC] " + bot->GetName() + " hits " + victim->GetName() + " for " + std::to_string(damage));
}

// Sends owner-facing combat feedback, rate-limited to one message per bot per second.
void MidnightBotMgr::SendCombatChat(ObjectGuid botGuid, std::string const& message)
{
    if (!combatChat)
        return;

    Player* owner = nullptr;
    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&botGuid](BotRef const& botRef)
        {
            return botRef.playerGuid == botGuid;
        });

        if (itr == activeBots.end() || itr->ownerGuid.IsEmpty())
            return;

        uint32 const now = getMSTime();
        if (now < itr->combatChatAt)
            return;

        itr->combatChatAt = now + 1000;
        owner = ObjectAccessor::FindConnectedPlayer(itr->ownerGuid);
    }

    if (owner && owner->GetSession())
        ChatHandler(owner->GetSession()).SendSysMessage(message);
}

// Damage statistics: session total/hit count plus a 60 s sample ring for DPS.
void MidnightBotMgr::AddBotDamage(ObjectGuid botGuid, uint32 damage)
{
    if (!damage)
        return;

    uint32 const now = getMSTime();

    std::lock_guard<std::mutex> lock(rosterMutex);

    auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&botGuid](BotRef const& botRef)
    {
        return botRef.playerGuid == botGuid;
    });

    if (itr == activeBots.end())
        return;

    itr->dmgSession += damage;
    ++itr->hitsSession;
    itr->dmgRecent.emplace_back(now, damage);

    while (!itr->dmgRecent.empty() && now - itr->dmgRecent.front().first > 60000)
        itr->dmgRecent.pop_front();
}

// Healing statistics: session total/cast count plus a 60 s sample ring.
void MidnightBotMgr::AddBotHeal(ObjectGuid botGuid, uint32 heal)
{
    if (!heal)
        return;

    uint32 const now = getMSTime();

    std::lock_guard<std::mutex> lock(rosterMutex);

    auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&botGuid](BotRef const& botRef)
    {
        return botRef.playerGuid == botGuid;
    });

    if (itr == activeBots.end())
        return;

    itr->healSession += heal;
    ++itr->healHits;
    itr->healRecent.emplace_back(now, heal);

    while (!itr->healRecent.empty() && now - itr->healRecent.front().first > 60000)
        itr->healRecent.pop_front();
}

// One "[MC] <name> dmg ... | total ..." line per active bot (60 s window + session totals).
std::vector<std::string> MidnightBotMgr::DamageReportLines()
{
    uint32 const now = getMSTime();
    std::vector<std::string> lines;

    std::lock_guard<std::mutex> lock(rosterMutex);

    lines.reserve(activeBots.size());

    for (BotRef& bot : activeBots)
    {
        while (!bot.dmgRecent.empty() && now - bot.dmgRecent.front().first > 60000)
            bot.dmgRecent.pop_front();

        while (!bot.healRecent.empty() && now - bot.healRecent.front().first > 60000)
            bot.healRecent.pop_front();

        uint64 dmg60 = 0;
        uint32 hits60 = 0;
        for (auto const& sample : bot.dmgRecent)
        {
            dmg60 += sample.second;
            ++hits60;
        }

        uint64 heal60 = 0;
        uint32 healHits60 = 0;
        for (auto const& sample : bot.healRecent)
        {
            heal60 += sample.second;
            ++healHits60;
        }

        uint32 seconds = 60;
        if (!bot.dmgRecent.empty())
            seconds = std::max<uint32>(1, std::min<uint32>(60, (now - bot.dmgRecent.front().first) / 1000));

        uint32 const dps = uint32(dmg60 / std::max<uint32>(1, seconds));

        std::string line = "[MC] " + Trinity::StringFormat("{:<10}", bot.name)
            + " dmg " + std::to_string(dmg60) + " (" + std::to_string(hits60) + " hits, " + std::to_string(dps) + " dps)";

        if (bot.healSession)
            line += "  | heals " + std::to_string(heal60) + " (" + std::to_string(healHits60) + ")";

        line += "  | total " + std::to_string(bot.dmgSession);

        if (bot.healSession)
            line += " | heal total " + std::to_string(bot.healSession);

        lines.push_back(std::move(line));
    }

    return lines;
}

// Posts a compact stats block to every owner whose bots were in combat within the last 10 s.
void MidnightBotMgr::SendStatsToOwners()
{
    uint32 const now = getMSTime();
    std::vector<ObjectGuid> owners;

    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        for (BotRef const& bot : activeBots)
        {
            if (bot.ownerGuid.IsEmpty())
                continue;

            bool recent = false;
            if (!bot.dmgRecent.empty() && now - bot.dmgRecent.back().first <= 10000)
                recent = true;
            if (!bot.healRecent.empty() && now - bot.healRecent.back().first <= 10000)
                recent = true;

            if (recent && std::find(owners.begin(), owners.end(), bot.ownerGuid) == owners.end())
                owners.push_back(bot.ownerGuid);
        }
    }

    if (owners.empty())
        return;

    std::vector<std::string> const lines = DamageReportLines();

    for (ObjectGuid const& ownerGuid : owners)
    {
        Player* owner = FindInWorldPlayer(ownerGuid);
        if (!owner || !owner->GetSession())
            continue;

        ChatHandler(owner->GetSession()).SendSysMessage("[MC] --- Bot damage (60s) ---");
        for (std::string const& line : lines)
            ChatHandler(owner->GetSession()).SendSysMessage(line);
    }
}

bool MidnightBotMgr::LootShow(Player* owner, std::string& out, std::string& err)
{
    if (!owner || !owner->IsInWorld())
    {
        err = "an in-game player is required";
        return false;
    }

    Group* group = owner->GetGroup();
    if (!group)
    {
        err = "you are not in a group";
        return false;
    }

    out = "loot method " + std::string(LootMethodName(group->GetLootMethod()))
        + ", threshold " + ItemQualityName(group->GetLootThreshold())
        + ", looter " + group->GetLooterGuid().ToString();
    return true;
}

bool MidnightBotMgr::LootSetMethod(Player* owner, std::string const& methodText, std::string& err)
{
    if (!owner || !owner->IsInWorld())
    {
        err = "an in-game player is required";
        return false;
    }

    Group* group = owner->GetGroup();
    if (!group)
    {
        err = "you are not in a group";
        return false;
    }

    LootMethod method;
    if (!ParseLootMethod(methodText, method))
    {
        err = "invalid loot method (use ffa, roundrobin, master, group or needgreed)";
        return false;
    }

    group->SetLootMethod(method);          // Group.h:266
    if (method == MASTER_LOOT || method == ROUND_ROBIN)
        group->SetLooterGuid(owner->GetGUID()); // Group.h:267

    group->SendUpdate();
    return true;
}

bool MidnightBotMgr::LootSetThreshold(Player* owner, std::string const& qualityText, std::string& err)
{
    if (!owner || !owner->IsInWorld())
    {
        err = "an in-game player is required";
        return false;
    }

    Group* group = owner->GetGroup();
    if (!group)
    {
        err = "you are not in a group";
        return false;
    }

    ItemQualities quality;
    if (!ParseItemQuality(qualityText, quality))
    {
        err = "invalid threshold (use poor, common, uncommon, rare, epic or legendary)";
        return false;
    }

    group->SetLootThreshold(quality);      // Group.h:270
    group->SendUpdate();
    return true;
}

// Module defaults applied whenever a bot is added to the owner's group, so the group does not
// stay on FFA (the core's default for a freshly created group).
void MidnightBotMgr::ApplyDefaultLootRules(Group* group)
{
    if (!group)
        return;

    if (LootMethod method; ParseLootMethod(lootMethod, method))
        group->SetLootMethod(method);

    if (ItemQualities quality; ParseItemQuality(lootThreshold, quality))
        group->SetLootThreshold(quality);

    group->SendUpdate();
}

// Triggered casts apply effects synchronously (Spell.cpp:3574, cast(true)), so the target's
// health is sampled immediately before/after to report the exact delta for this cast.
// This is the single completion report per cast (NoteBotCastStarted is no longer used here).
SpellCastResult MidnightBotMgr::BotCastAndReport(ObjectGuid botGuid, Player* bot, Unit* target, uint32 spellId, Player* owner, bool offensive)
{
    if (!bot || !target)
        return SPELL_FAILED_BAD_TARGETS;

    uint64 const healthBefore = target->GetHealth();
    SpellCastResult const result = BotCastSpell(bot, target, spellId, owner, !offensive);

    if (result == SPELL_CAST_OK)
    {
        uint64 const healthAfter = target->GetHealth();

        if (offensive)
        {
            uint64 const dealt = healthBefore > healthAfter ? healthBefore - healthAfter : 0;
            if (dealt)
                LogAi(botGuid, "cast '" + target->GetName() + "' done: hp " + std::to_string(healthBefore) + " -> " + std::to_string(healthAfter) + " (delta " + std::to_string(dealt) + ") (immediate)");
            else
                LogAi(botGuid, "cast '" + target->GetName() + "' done: NO DAMAGE (0) (immediate)");
        }
        else
        {
            uint64 const applied = healthAfter > healthBefore ? healthAfter - healthBefore : 0;
            LogAi(botGuid, "heal '" + target->GetName() + "' done: hp " + std::to_string(healthBefore) + " -> " + std::to_string(healthAfter) + " (+" + std::to_string(applied) + ") (immediate)");

            // Direct-heal top-up for visible group members: rank-1 heals are far too small versus
            // mob damage, and incoming damage during the cast can make healthAfter <= healthBefore.
            // Always ensure at least `minimum` healing lands while the target is hurt.
            if (target->IsAlive() && target->ToPlayer())
            {
                uint64 const missing = target->GetMaxHealth() > target->GetHealth() ? target->GetMaxHealth() - target->GetHealth() : 0;
                uint32 const minimum = uint32(std::max(1.0, double(bot->GetLevel()) * 25.0) * frand(0.9f, 1.1f));

                if (missing > 0 && (healthAfter <= healthBefore || applied < minimum))
                {
                    uint32 topUp = applied < minimum ? uint32(minimum - applied) : 0u;
                    topUp = uint32(std::min<uint64>(uint64(topUp), missing));

                    if (topUp)
                    {
                        uint64 const directBefore = target->GetHealth();
                        target->ModifyHealth(int64(topUp));   // Unit.h:815
                        uint64 const directAfter = target->GetHealth();

                        LogAi(botGuid, "heal '" + target->GetName() + "' direct: hp " + std::to_string(directBefore)
                            + " -> " + std::to_string(directAfter) + " (+" + std::to_string(directAfter - directBefore) + ")");
                    }
                }
            }
        }

        // Owner-visible-only targets: the triggered cast completes but the effect is dropped by
        // the visibility gate. Schedule a direct hit through Unit::DealDamage after the spell's
        // cast time (wind-up) so it feels like a cast; melee already bypasses the same gate.
        // Only when the bot cannot see the target, so normal hits/resists are never overridden.
        bool scheduleDirect = false;
        uint32 directDamage = 0;
        uint32 directSchool = 0;
        uint32 directDelay = 1500;

        if (offensive && target->IsAlive() && !bot->CanSeeOrDetect(target))
        {
            if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE))
            {
                int32 total = 0;
                for (SpellEffectInfo const& effect : spellInfo->GetEffects())
                {
                    if (!effect.IsEffect(SPELL_EFFECT_SCHOOL_DAMAGE) && !effect.IsEffect(SPELL_EFFECT_WEAPON_DAMAGE)
                        && !effect.IsEffect(SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL))
                        continue;

                    total += int32(effect.CalcValue(bot, nullptr, target));
                }

                // Cap so one rank-1 spell can never one-shot: level*40 .. level*70 (level 6 -> 240..420).
                int32 scaled = std::min<int32>(std::max<int32>(total, int32(bot->GetLevel()) * 40), int32(bot->GetLevel()) * 70);
                scaled = int32(float(scaled) * frand(0.9f, 1.1f));

                if (scaled > 0)
                {
                    scheduleDirect = true;
                    directDamage = uint32(scaled);
                    directSchool = uint32(spellInfo->GetSchoolMask());
                    directDelay = spellInfo->CalcCastTime();
                    if (!directDelay)
                        directDelay = 1500;
                }

                LogAi(botGuid, "direct queued: dmg " + std::to_string(directDamage) + " in " + std::to_string(directDelay) + "ms (total=" + std::to_string(total) + " see=" + (bot->CanSeeOrDetect(target) ? "1" : "0") + ")");
            }
        }

        // Queue delayed work: the wind-up direct hit (if any) plus the effect verification.
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&botGuid](BotRef const& botRef)
        {
            return botRef.playerGuid == botGuid;
        });

        if (itr != activeBots.end())
        {
            if (scheduleDirect)
            {
                if (itr->pendingCasts.size() >= 8)
                    itr->pendingCasts.pop_front();

                itr->pendingCasts.push_back(PendingCastCheck{ target->GetGUID(), healthBefore, spellId, getMSTime() + directDelay, true, directDamage, directSchool, true });
            }

            if (itr->pendingCasts.size() >= 8)
                itr->pendingCasts.pop_front();

            itr->pendingCasts.push_back(PendingCastCheck{ target->GetGUID(), healthBefore, spellId, getMSTime() + 1500, offensive });
        }
    }

    return result;
}

// Ground-truth check 1.5 s after a cast: logs whether the effect actually applied.
void MidnightBotMgr::CheckPendingCasts(ObjectGuid botGuid, Player* bot)
{
    if (!bot)
        return;

    uint32 const now = getMSTime();
    std::vector<PendingCastCheck> due;

    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&botGuid](BotRef const& botRef)
        {
            return botRef.playerGuid == botGuid;
        });

        if (itr == activeBots.end())
            return;

        for (auto it = itr->pendingCasts.begin(); it != itr->pendingCasts.end(); )
        {
            if (it->dueMs <= now)
            {
                due.push_back(*it);
                it = itr->pendingCasts.erase(it);
            }
            else
                ++it;
        }
    }

    for (PendingCastCheck const& pending : due)
    {
        Unit* target = ObjectAccessor::GetUnit(*bot, pending.targetGuid);
        if (!target)
        {
            if (pending.applyDirect)
                LogAi(botGuid, "cast direct spell " + std::to_string(pending.spellId) + ": target gone");

            LogAi(botGuid, "cast-check spell " + std::to_string(pending.spellId) + ": target gone");
            continue;
        }

        // Wind-up payload: apply the scheduled direct hit when it comes due.
        if (pending.applyDirect && target->IsAlive() && target->IsInWorld())
        {
            if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(pending.spellId, DIFFICULTY_NONE))
            {
                uint64 const hpBefore = target->GetHealth();
                LogAi(botGuid, "direct due: applying " + std::to_string(pending.damage) + " to '" + target->GetName() + "' (hp " + std::to_string(hpBefore) + ")");

                // Exact damage: Unit::DealDamage amplifies through the server's level scaling
                // (~30x for low-level players vs creatures, the same reason bot melee hits ~500
                // with a starter weapon). ModifyHealth applies the payload we computed.
                if (int64(hpBefore) <= int64(pending.damage))
                    Unit::Kill(bot, target, true);
                else
                    target->ModifyHealth(-int64(pending.damage));

                uint64 const hpAfter = target->GetHealth();
                uint64 const dmgDelta = hpBefore > hpAfter ? hpBefore - hpAfter : 0;

                if (dmgDelta)
                {
                    // Impact feedback for observers: damage log packet + spell visual, so the
                    // direct hit is visible/heard even though it bypassed the normal effect path.
                    SpellNonMeleeDamage damageLog(bot, target, spellInfo, SpellCastVisual(), pending.schoolMask, ObjectGuid::Empty);
                    damageLog.damage = uint32(dmgDelta);
                    damageLog.originalDamage = pending.damage;
                    damageLog.preHitHealth = hpBefore;
                    damageLog.HitInfo = HITINFO_AFFECTS_VICTIM; // normal hit so the client shows/attributes it
                    bot->SendSpellNonMeleeDamageLog(&damageLog);
                    bot->SendPlaySpellVisual(target, spellInfo->GetSpellVisual(bot, target), 0, 0, 0.0f, false);

                    AddBotDamage(botGuid, uint32(dmgDelta));
                    SendCombatChat(botGuid, "[MC] " + bot->GetName() + " hits " + target->GetName() + " for " + std::to_string(dmgDelta));

                    LogAi(botGuid, "cast '" + target->GetName() + "' direct: hp " + std::to_string(hpBefore)
                        + " -> " + std::to_string(hpAfter) + " (delta " + std::to_string(dmgDelta) + ")");
                }
                else if (target->IsAlive())
                    LogAi(botGuid, "cast '" + target->GetName() + "' direct FAILED (delta 0, alive)");
            }
        }

        uint64 const healthAfter = target->GetHealth();

        if (pending.offensive)
        {
            uint64 const delta = pending.healthBefore > healthAfter ? pending.healthBefore - healthAfter : 0;
            LogAi(botGuid, "cast-check '" + target->GetName() + "' spell " + std::to_string(pending.spellId)
                + ": hp " + std::to_string(pending.healthBefore) + " -> " + std::to_string(healthAfter)
                + " (delta " + std::to_string(delta) + ")");
        }
        else
        {
            uint64 const delta = healthAfter > pending.healthBefore ? healthAfter - pending.healthBefore : 0;
            LogAi(botGuid, "heal-check '" + target->GetName() + "' spell " + std::to_string(pending.spellId)
                + ": hp " + std::to_string(pending.healthBefore) + " -> " + std::to_string(healthAfter)
                + " (+" + std::to_string(delta) + ")");

            if (delta)
            {
                AddBotHeal(botGuid, uint32(delta));
                SendCombatChat(botGuid, "[MC] " + bot->GetName() + " heals " + target->GetName() + " +" + std::to_string(delta));
            }
        }
    }
}

// Module-side swing pacing: Player::Update's DoMeleeAttackIfReady fails its target check for
// owner-visible victims and resets BASE_ATTACK to 100 ms every frame, so isAttackReady() can
// never be trusted. Uses our own timestamp with the weapon speed (floor 800 ms, fallback 2000 ms).
bool MidnightBotMgr::TryBotMeleeSwing(ObjectGuid botGuid, Player* bot)
{
    if (!bot)
        return false;

    uint32 const now = getMSTime();

    std::lock_guard<std::mutex> lock(rosterMutex);

    auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&botGuid](BotRef const& botRef)
    {
        return botRef.playerGuid == botGuid;
    });

    if (itr == activeBots.end())
        return false;

    uint32 const baseAttack = bot->GetBaseAttackTime(BASE_ATTACK);
    uint32 const interval = std::max<uint32>(800, baseAttack ? baseAttack : 2000);

    if (itr->lastManualSwingMs && now - itr->lastManualSwingMs < interval)
        return false;

    itr->lastManualSwingMs = now;
    return true;
}

// Module-side per-spell pacing: triggered casts ignore the core cooldowns, so each ability has
// its own next-allowed timestamp per bot. Returns true (and arms the cooldown) when it may fire.
bool MidnightBotMgr::TryBotSpellPace(ObjectGuid botGuid, uint32 spellId, uint32 cooldownMs)
{
    uint32 const now = getMSTime();

    std::lock_guard<std::mutex> lock(rosterMutex);

    auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&botGuid](BotRef const& botRef)
    {
        return botRef.playerGuid == botGuid;
    });

    if (itr == activeBots.end())
        return false;

    uint32& next = itr->spellPace[spellId];
    if (next && now < next)
        return false;

    next = now + cooldownMs;
    return true;
}

// Rate limit for the "dps waiting for tank threat" line (4 s per bot).
bool MidnightBotMgr::DpsWaitLogDue(ObjectGuid botGuid)
{
    uint32 const now = getMSTime();

    std::lock_guard<std::mutex> lock(rosterMutex);

    auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&botGuid](BotRef const& botRef)
    {
        return botRef.playerGuid == botGuid;
    });

    if (itr == activeBots.end())
        return false;

    if (itr->dpsWaitLogAt && getMSTimeDiff(itr->dpsWaitLogAt, now) < 4000)
        return false;

    itr->dpsWaitLogAt = now;
    return true;
}

// Tracks the cast that just started. Start time + cast time are stored so CheckBotCastState can
// tell a normal completion from a true interrupt; the target's health before the cast is stored
// so the completion delta can be reported.
void MidnightBotMgr::NoteBotCastStarted(ObjectGuid botGuid, uint32 spellId, Unit* target, uint32 castTimeMs, bool offensive, float maxRange)
{
    if (!target)
        return;

    std::string const targetName = target->GetName();
    std::string message = "casting '" + targetName + "' spell " + std::to_string(spellId) +
        " (" + std::to_string(castTimeMs / 1000) + "." + std::to_string((castTimeMs % 1000) / 100) + "s)";

    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&botGuid](BotRef const& bot)
        {
            return bot.playerGuid == botGuid;
        });

        if (itr == activeBots.end())
            return;

        itr->castSpellId = spellId;
        itr->castTargetName = targetName;
        itr->castTargetGuid = target->GetGUID();
        itr->castHealthBefore = target->GetHealth();
        itr->castOffensive = offensive;
        itr->castMaxRange = maxRange;
        itr->castStartMs = getMSTime();
        itr->castTimeMs = castTimeMs;
        itr->lastAiRaw.clear(); // casts are always recorded, even repeated identical ones
    }

    LogAi(botGuid, message);
}

void MidnightBotMgr::CheckBotCastState(ObjectGuid botGuid, Player* bot)
{
    if (!bot)
        return;

    uint32 spellId = 0;
    std::string targetName;
    ObjectGuid targetGuid;
    uint64 healthBefore = 0;
    bool offensive = false;
    float maxRange = 0.0f;
    uint32 castTime = 0;
    uint32 elapsed = 0;
    bool interrupted = false;

    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&botGuid](BotRef const& botRef)
        {
            return botRef.playerGuid == botGuid;
        });

        if (itr == activeBots.end() || !itr->castSpellId)
            return;

        if (BotIsBusyCasting(bot))
            return; // still winding up / channeling

        spellId = itr->castSpellId;
        targetName = itr->castTargetName;
        targetGuid = itr->castTargetGuid;
        healthBefore = itr->castHealthBefore;
        offensive = itr->castOffensive;
        maxRange = itr->castMaxRange;
        castTime = itr->castTimeMs;
        elapsed = getMSTime() - itr->castStartMs;

        // Normal completion happens at ~castTime; allow 250ms slack, then it is a true interrupt.
        uint32 const minElapsed = (castTime > 250) ? (castTime - 250) : 0;
        interrupted = elapsed < minElapsed;

        itr->castSpellId = 0;
        itr->castTargetName.clear();
        itr->castTargetGuid.Clear();
        itr->castHealthBefore = 0;
        itr->castOffensive = false;
        itr->castMaxRange = 0.0f;
        itr->castStartMs = 0;
        itr->castTimeMs = 0;
    }

    if (interrupted)
    {
        // Distinguish "target ran out of range" from movement/other interrupts.
        std::string reason = "movement?";
        if (Unit* castTarget = ObjectAccessor::GetUnit(*bot, targetGuid))
        {
            if (maxRange > 0.0f && !bot->IsWithinDist(castTarget, maxRange))
                reason = "target moved/out of range?";
        }
        else
            reason = "target gone";

        LogAi(botGuid, "cast lost/interrupted '" + targetName + "' spell " + std::to_string(spellId) + " (" + reason + ")");
        return;
    }

    // Completed: report the primary target's health delta.
    if (Unit* castTarget = ObjectAccessor::GetUnit(*bot, targetGuid))
    {
        uint64 const nowHealth = castTarget->GetHealth();

        if (offensive)
        {
            uint64 const dealt = healthBefore > nowHealth ? healthBefore - nowHealth : 0;
            if (dealt)
                LogAi(botGuid, "cast '" + targetName + "' done: hp " + std::to_string(healthBefore) + " -> " + std::to_string(nowHealth) + " (delta " + std::to_string(dealt) + ")");
            else
                LogAi(botGuid, "cast '" + targetName + "' done: NO DAMAGE (0)");
        }
        else
        {
            uint64 const healed = nowHealth > healthBefore ? nowHealth - healthBefore : 0;
            LogAi(botGuid, "heal '" + targetName + "' done: hp " + std::to_string(healthBefore) + " -> " + std::to_string(nowHealth) + " (+" + std::to_string(healed) + ")");
        }
    }
    else
        LogAi(botGuid, "cast '" + targetName + "' done: target gone");
}

// Accumulates visible victim health drops between ticks (auto-attacks + abilities) and flushes
// a compact `melee: N dmg` note every 3 seconds. Cheap: one comparison per tick per bot.
// Kept for the periodic threat/geometry proof. The old sampled `melee: N dmg` line was removed:
// exact melee damage is now reported per swing (`swing 'X': hp A -> B (delta N)`) and by the
// DMG BOT UnitScript hook, so sampling the shared victim HP would only duplicate/mislead.
void MidnightBotMgr::TrackBotMeleeDamage(ObjectGuid botGuid, Player* bot)
{
    if (!bot)
        return;

    Unit* victim = bot->GetVictim();
    if (!victim)
        return;

    uint32 const now = getMSTime();
    std::string threatMessage;

    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&botGuid](BotRef const& botRef)
        {
            return botRef.playerGuid == botGuid;
        });

        if (itr == activeBots.end())
            return;

        if (now < itr->threatLogAt)
            return;

        itr->threatLogAt = now + 5000;

        bool const meleeRole = (itr->role == BotRole::Tank || itr->role == BotRole::None);
        Unit* mobVictim = victim->GetVictim();
        Player* owner = ObjectAccessor::FindConnectedPlayer(itr->ownerGuid);

        threatMessage = "threat: bot=" + Fmt1(victim->GetThreatManager().GetThreat(bot))
            + " victim=" + Fmt1(owner ? victim->GetThreatManager().GetThreat(owner) : 0.0f)
            + " mobVictim='" + (mobVictim ? mobVictim->GetName() : std::string("(none)")) + "'";

        if (meleeRole)
            threatMessage += " dist=" + Fmt1(bot->GetDistance(victim))
                + " reach=" + Fmt1(bot->GetCombatReach() + victim->GetCombatReach())
                + " meleeRange=" + Fmt1(bot->GetMeleeRange(victim));
    }

    if (!threatMessage.empty())
        LogAi(botGuid, threatMessage);
}

// Keeps the victim acquisition timestamp and makes sure selection matches the victim.
void MidnightBotMgr::TrackBotVictim(ObjectGuid botGuid, Player* bot)
{
    if (!bot)
        return;

    Unit* victim = bot->GetVictim();
    if (victim && bot->GetTarget() != victim->GetGUID())
        bot->SetSelection(victim->GetGUID());

    uint32 const now = getMSTime();

    std::lock_guard<std::mutex> lock(rosterMutex);

    auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&botGuid](BotRef const& botRef)
    {
        return botRef.playerGuid == botGuid;
    });

    if (itr == activeBots.end())
        return;

    if (!victim)
    {
        itr->victimGuid.Clear();
        itr->victimSinceMs = 0;
        return;
    }

    if (victim->GetGUID() != itr->victimGuid)
    {
        itr->victimGuid = victim->GetGUID();
        itr->victimSinceMs = now;
        itr->attackRejectedGuid.Clear();
        itr->attackAcceptedGuid.Clear();
    }
}

// Damage-taken proof: samples bot health per tick and logs each detected drop while in combat.
void MidnightBotMgr::TrackBotDamageTaken(ObjectGuid botGuid, Player* bot)
{
    if (!bot)
        return;

    uint64 const health = bot->GetHealth();
    std::string message;

    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&botGuid](BotRef const& botRef)
        {
            return botRef.playerGuid == botGuid;
        });

        if (itr == activeBots.end())
            return;

        if (!itr->healthLast)
        {
            itr->healthLast = health;
            return;
        }

        if (health < itr->healthLast && bot->IsInCombat())
            message = "took ~" + std::to_string(itr->healthLast - health)
                + " (hp " + std::to_string(itr->healthLast) + " -> " + std::to_string(health) + ")";

        itr->healthLast = health;
    }

    if (!message.empty())
        LogAi(botGuid, message);
}

bool MidnightBotMgr::BotVictimInGrace(ObjectGuid botGuid, uint32 graceMs) const
{
    std::lock_guard<std::mutex> lock(rosterMutex);

    auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&botGuid](BotRef const& botRef)
    {
        return botRef.playerGuid == botGuid;
    });

    if (itr == activeBots.end() || !itr->victimSinceMs)
        return false;

    return (getMSTime() - itr->victimSinceMs) < graceMs;
}

// Records Unit::Attack() rejections once per target so silent failures are visible in the ring.
void MidnightBotMgr::NoteAttackResult(ObjectGuid botGuid, Unit* target, bool success)
{
    if (!target)
        return;

    std::string message;
    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&botGuid](BotRef const& botRef)
        {
            return botRef.playerGuid == botGuid;
        });

        if (itr == activeBots.end())
            return;

        if (success)
        {
            itr->attackRejectedGuid.Clear();

            if (itr->attackAcceptedGuid == target->GetGUID())
                return; // already reported the engagement with this target

            itr->attackAcceptedGuid = target->GetGUID();

            if (Player* bot = ObjectAccessor::FindConnectedPlayer(botGuid))
                message = "attack engaged '" + target->GetName()
                    + "' (ready=" + (bot->isAttackReady() ? "yes" : "no")
                    + " inMelee=" + (bot->IsWithinMeleeRange(target) ? "yes" : "no") + ")";
        }
        else
        {
            if (itr->attackRejectedGuid == target->GetGUID())
                return; // already reported for this target

            itr->attackRejectedGuid = target->GetGUID();
            message = "Attack() rejected on '" + target->GetName() + "'";
        }
    }

    if (!message.empty())
        LogAi(botGuid, message);
}

Player* MidnightBotMgr::ResolveActiveBotPlayer(std::string const& name, std::string& err, uint32* outIndex) const
{
    ObjectGuid playerGuid;
    uint32 index = 0;

    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&name](BotRef const& bot)
        {
            return bot.name == name;
        });

        if (itr == activeBots.end())
        {
            err = "bot is not active";
            return nullptr;
        }

        playerGuid = itr->playerGuid;
        index = uint32(itr - activeBots.begin());
    }

    Player* bot = FindInWorldPlayer(playerGuid);
    if (!bot)
    {
        err = "bot is still logging in or not in the world";
        return nullptr;
    }

    if (outIndex)
        *outIndex = index;

    return bot;
}

// Focus fire helper: first active tank bot (other than self) with a safe victim.
Unit* MidnightBotMgr::FindTankBotVictim(Player* self, Player* owner)
{
    std::vector<ObjectGuid> tankGuids;

    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        for (BotRef const& bot : activeBots)
            if (bot.role == BotRole::Tank && (!self || bot.playerGuid != self->GetGUID()))
                tankGuids.push_back(bot.playerGuid);
    }

    for (ObjectGuid const& guid : tankGuids)
    {
        Player* tank = FindInWorldPlayer(guid);
        if (!tank)
            continue;

        if (Unit* victim = tank->GetVictim())
            if (IsSafeAttackTarget(self, owner, victim))
                return victim;
    }

    return nullptr;
}

// First active tank bot (other than self) currently in the world, used by the DPS hold-fire gate.
Player* MidnightBotMgr::FindTankBot(Player* self) const
{
    std::vector<ObjectGuid> tankGuids;

    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        for (BotRef const& bot : activeBots)
            if (bot.role == BotRole::Tank && (!self || bot.playerGuid != self->GetGUID()))
                tankGuids.push_back(bot.playerGuid);
    }

    for (ObjectGuid const& guid : tankGuids)
        if (Player* tank = FindInWorldPlayer(guid))
            return tank;

    return nullptr;
}

std::vector<ObjectGuid> MidnightBotMgr::GetTankBotGuids() const
{
    std::lock_guard<std::mutex> lock(rosterMutex);

    std::vector<ObjectGuid> guids;
    for (BotRef const& bot : activeBots)
        if (bot.role == BotRole::Tank)
            guids.push_back(bot.playerGuid);

    return guids;
}

bool MidnightBotMgr::SummonBot(std::string const& name, Player* target, std::string& err)
{
    if (!target || !target->IsInWorld())
    {
        err = "an in-game player is required";
        return false;
    }

    uint32 spreadIndex = 0;
    Player* bot = ResolveActiveBotPlayer(name, err, &spreadIndex);
    if (!bot)
        return false;

    if (bot == target)
    {
        err = "the target already is that bot";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&name](BotRef const& botRef)
        {
            return botRef.name == name;
        });

        if (itr != activeBots.end())
            EnsureOwnerForCommand(*itr, target, "summon");
    }

    return TeleportBotToPlayer(bot, target, spreadIndex, err);
}

bool MidnightBotMgr::GotoBot(std::string const& name, Player* target, std::string& err)
{
    if (!target || !target->IsInWorld())
    {
        err = "an in-game player is required";
        return false;
    }

    Player* bot = ResolveActiveBotPlayer(name, err);
    if (!bot)
        return false;

    if (bot == target)
    {
        err = "you are that bot";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&name](BotRef const& botRef)
        {
            return botRef.name == name;
        });

        if (itr != activeBots.end())
            EnsureOwnerForCommand(*itr, target, "goto");
    }

    if (!target->TeleportTo(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetOrientation(), TELE_TO_NONE, bot->GetInstanceId()))
    {
        err = "TeleportTo was refused (invalid or disabled destination)";
        return false;
    }

    return true;
}

bool MidnightBotMgr::SummonAllBots(Player* target, uint32& moved, uint32& failed)
{
    moved = 0;
    failed = 0;

    if (!target || !target->IsInWorld())
        return false;

    std::vector<BotRef> snapshot;
    {
        std::lock_guard<std::mutex> lock(rosterMutex);
        snapshot = activeBots;
    }

    for (size_t i = 0; i < snapshot.size(); ++i)
    {
        BotRef const& ref = snapshot[i];
        Player* bot = FindInWorldPlayer(ref.playerGuid);
        if (!bot || bot == target)
        {
            ++failed;
            TC_LOG_WARN("scripts.MidnightBotAI", "SummonAll: could not move bot '{}' (not in the world)", ref.name);
            continue;
        }

        std::string err;
        if (TeleportBotToPlayer(bot, target, uint32(i), err))
            ++moved;
        else
        {
            ++failed;
            TC_LOG_WARN("scripts.MidnightBotAI", "SummonAll: could not move bot '{}': {}", ref.name, err);
        }
    }

    return true;
}

bool MidnightBotMgr::AddBotToGroup(std::string const& name, Player* owner, std::string& err)
{
    if (!owner || !owner->IsInWorld())
    {
        err = "an in-game group owner is required";
        return false;
    }

    Player* bot = ResolveActiveBotPlayer(name, err);
    if (!bot)
        return false;

    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&name](BotRef const& botRef)
        {
            return botRef.name == name;
        });

        if (itr != activeBots.end())
            EnsureOwnerForCommand(*itr, owner, "party");
    }

    return AddPlayerToGroup(owner, bot, err) == GroupAddResult::Added;
}

bool MidnightBotMgr::PartyAllBots(Player* owner, uint32& added, uint32& alreadyGrouped, uint32& failed)
{
    added = 0;
    alreadyGrouped = 0;
    failed = 0;

    if (!owner || !owner->IsInWorld())
        return false;

    std::vector<BotRef> snapshot;
    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        for (BotRef& bot : activeBots)
            EnsureOwnerForCommand(bot, owner, "partyall");

        snapshot = activeBots;
    }

    for (BotRef const& ref : snapshot)
    {
        Player* bot = FindInWorldPlayer(ref.playerGuid);
        if (!bot || bot == owner)
        {
            ++failed;
            TC_LOG_WARN("scripts.MidnightBotAI", "PartyAll: bot '{}' is not in the world", ref.name);
            continue;
        }

        std::string err;
        switch (AddPlayerToGroup(owner, bot, err))
        {
            case GroupAddResult::Added:
                ++added;
                break;
            case GroupAddResult::AlreadyInGroup:
                ++alreadyGrouped;
                break;
            default:
                ++failed;
                TC_LOG_WARN("scripts.MidnightBotAI", "PartyAll: could not add bot '{}': {}", ref.name, err);
                break;
        }
    }

    return true;
}

bool MidnightBotMgr::SetBotFollow(std::string const& name, bool follow, Player* issuer, std::string& err)
{
    ObjectGuid playerGuid;
    ObjectGuid followTarget;
    BotRole role = BotRole::None;
    float spacingAngle = 0.0f;
    float spacingDist = 0.0f;

    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&name](BotRef const& bot)
        {
            return bot.name == name;
        });

        if (itr == activeBots.end())
        {
            err = "bot is not active";
            return false;
        }

        EnsureOwnerForCommand(*itr, issuer, follow ? "follow" : "stay");

        itr->follow = follow;
        PushBotAiLog(*itr, follow ? "follow on" : "follow off (stay)");

        playerGuid = itr->playerGuid;
        followTarget = itr->ownerGuid;
        role = itr->role;
        spacingAngle = itr->spacingAngle;
        spacingDist = itr->spacingDist;
    }

    Player* bot = FindInWorldPlayer(playerGuid);
    if (!bot)
        return true; // flag stored; the update tick applies it once the bot is in the world

    if (!follow)
    {
        if (bot->HasUnitState(UNIT_STATE_FOLLOW))
            bot->GetMotionMaster()->Remove(FOLLOW_MOTION_TYPE);

        bot->GetMotionMaster()->MoveIdle();
        bot->StopMoving();
        return true;
    }

    Player* owner = FindInWorldPlayer(followTarget);
    if (!owner || bot->GetMapId() != owner->GetMapId() || bot->GetInstanceId() != owner->GetInstanceId())
        return true; // tick retries when both share a map

    if (!bot->InSamePhase(owner))
        return true; // tick retries once the phase is inherited

    if (!bot->HasUnitState(UNIT_STATE_FOLLOW))
        bot->GetMotionMaster()->MoveFollow(owner, FollowDistanceForRole(role) + spacingDist, spacingAngle);

    return true;
}

bool MidnightBotMgr::SetBotAssist(std::string const& name, bool assist, Player* issuer, std::string& err)
{
    std::lock_guard<std::mutex> lock(rosterMutex);

    auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&name](BotRef const& bot)
    {
        return bot.name == name;
    });

    if (itr == activeBots.end())
    {
        err = "bot is not active";
        return false;
    }

    EnsureOwnerForCommand(*itr, issuer, assist ? "assist" : "assist-off");

    itr->assist = assist;
    PushBotAiLog(*itr, assist ? "assist on" : "assist off");
    return true;
}

bool MidnightBotMgr::SetBotRole(std::string const& name, BotRole role, std::string& err)
{
    ObjectGuid playerGuid;
    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&name](BotRef const& bot)
        {
            return bot.name == name;
        });

        if (itr == activeBots.end())
        {
            err = "bot is not active";
            return false;
        }

        itr->role = role;
        PushBotAiLog(*itr, std::string("role set to ") + RoleName(role));
        playerGuid = itr->playerGuid;
        bootstrapRoles[name] = role; // keeps the new role across respawns in this server session
    }

    // Drop an active follow generator so the tick re-issues it at the new role's distance.
    if (Player* bot = FindInWorldPlayer(playerGuid))
        if (bot->HasUnitState(UNIT_STATE_FOLLOW))
            bot->GetMotionMaster()->Remove(FOLLOW_MOTION_TYPE);

    return true;
}

bool MidnightBotMgr::ResurrectBot(std::string const& name, std::string& err)
{
    Player* bot = ResolveActiveBotPlayer(name, err);
    if (!bot)
        return false;

    if (EnsureBotAlive(bot))
        LogAi(bot->GetGUID(), std::string(bot->GetName()) + ": resurrected (was dead)");

    if (bot->GetHealth() < bot->GetMaxHealth())
        bot->SetHealth(bot->GetMaxHealth());

    return true;
}

uint32 MidnightBotMgr::LearnBotSpells(std::string const& name, std::string& err)
{
    Player* bot = ResolveActiveBotPlayer(name, err);
    if (!bot)
        return 0;

    BotRole role = DefaultRoleForClass(bot->GetClass());
    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&name](BotRef const& botRef)
        {
            return botRef.name == name;
        });

        if (itr != activeBots.end() && itr->role != BotRole::None)
            role = itr->role;
    }

    uint32 learned = LearnRoleSpells(bot, role);
    if (learned)
        LogAi(bot->GetGUID(), std::string(bot->GetName()) + ": learned " + std::to_string(learned) + " spell(s)");

    return learned;
}

// Auto-spends talent points through this core's modern (Dragonflight-style) Talent.dbc system:
// every entry is a single-rank node and the level budget (DB2Manager::GetNumTalentsAtLevel, the
// same value Player::InitTalentForLevel, Player.cpp:2306, stores in the MaxTalentTiers update
// field) gates which tiers may be learned. Bots below the first talent level have a zero budget,
// so the call is a graceful no-op there.
uint32 MidnightBotMgr::LearnAutoTalents(Player* bot)
{
    if (!autoTalents || !bot || !bot->IsInWorld())
        return 0;

    int32 const budget = DB2Manager::GetNumTalentsAtLevel(bot->GetLevel(), Classes(bot->GetClass()));
    if (budget <= 0)
        return 0;

    uint8 const group = bot->GetActiveTalentGroup();
    uint32 spent = 0;
    for (auto const& entry : *bot->GetTalentMap(group))
        if (entry.second != PLAYERSPELL_REMOVED)
            ++spent;

    if (spent >= uint32(budget))
        return 0;

    // Refresh the tier budget field (reset/trim rules live in Player::InitTalentForLevel) so
    // LearnTalent's MaxTalentTiers gate (Player.cpp:27602) matches the current level.
    bot->InitTalentForLevel();

    uint32 const target = uint32(budget);
    uint32 learned = 0;

    for (uint32 tier = 0; tier < MAX_TALENT_TIERS && tier < target && (spent + learned) < target; ++tier)
    {
        // Never touch a tier that already has a chosen talent: LearnTalent would remove it and
        // demand a rest area (Player.cpp:27628-27647).
        bool occupied = false;
        for (uint32 column = 0; column < MAX_TALENT_COLUMNS && !occupied; ++column)
            for (TalentEntry const* talent : sDB2Manager.GetTalentsByPosition(bot->GetClass(), tier, column))
                if (bot->HasTalent(talent->ID, group))
                    occupied = true;

        if (occupied)
            continue;

        // Same slot match rule as Player::LearnTalent (Player.cpp:27613-27623): spec-specific
        // entry when it exists, otherwise the shared one.
        TalentEntry const* pick = nullptr;
        for (uint32 column = 0; column < MAX_TALENT_COLUMNS && !pick; ++column)
        {
            for (TalentEntry const* talent : sDB2Manager.GetTalentsByPosition(bot->GetClass(), tier, column))
            {
                if (!talent->SpecID)
                    pick = talent;
                else if (ChrSpecialization(talent->SpecID) == bot->GetPrimarySpecialization())
                {
                    pick = talent;
                    break;
                }
            }
        }

        if (!pick)
            continue;

        int32 spellOnCooldown = 0;
        TalentLearnResult const result = bot->LearnTalent(pick->ID, &spellOnCooldown);

        if (result == TALENT_LEARN_OK)
        {
            ++learned;
            if (learned <= 3)
                LogAi(bot->GetGUID(), std::string(bot->GetName()) + ": talent: learned " + std::to_string(pick->ID) + " rank 1");
        }
        else if (result == TALENT_FAILED_AFFECTING_COMBAT || result == TALENT_FAILED_CANT_DO_THAT_RIGHT_NOW)
            break; // transient (combat/dead): retry on the next tick
        // Other failures (bad slot/already known) just skip this tier.
    }

    if (learned)
    {
        bot->SendTalentsInfoData();
        LogAi(bot->GetGUID(), std::string(bot->GetName()) + ": talents: spent " + std::to_string(learned) + " point(s)");
    }

    return learned;
}

bool MidnightBotMgr::BoostBot(std::string const& name, Player* issuer, uint8 targetLevel, std::string& err)
{
    Player* bot = ResolveActiveBotPlayer(name, err);
    if (!bot)
        return false;

    BotRole role = DefaultRoleForClass(bot->GetClass());
    ObjectGuid ownerGuid;
    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&name](BotRef const& botRef)
        {
            return botRef.name == name;
        });

        if (itr == activeBots.end())
        {
            err = "bot is not active";
            return false;
        }

        ownerGuid = itr->ownerGuid;
        if (itr->role != BotRole::None)
            role = itr->role;
    }

    // Only the owner or an unowned bot may be boosted; never touch a real player (targets are
    // always active bots by construction, and owned bots must belong to the issuer).
    if (issuer && !ownerGuid.IsEmpty() && ownerGuid != issuer->GetGUID())
    {
        err = "bot is owned by another player";
        return false;
    }

    if (EnsureBotAlive(bot))
        LogAi(bot->GetGUID(), std::string(bot->GetName()) + ": resurrected (was dead)");

    uint8 oldLevel = bot->GetLevel();
    if (targetLevel < 1)
        targetLevel = 1;
    if (targetLevel > STRONG_MAX_LEVEL)
        targetLevel = STRONG_MAX_LEVEL;

    if (bot->GetLevel() != targetLevel)
    {
        // Same path as the '.character level' command (cs_character.cpp:772-774); GiveLevel
        // (Player.h:1332, Player.cpp:2202) updates skills/talents/stats and full-heals.
        bot->GiveLevel(targetLevel);
        bot->InitTalentForLevel();
        bot->SetXP(0);
    }

    if (bot->GetHealth() < bot->GetMaxHealth())
        bot->SetHealth(bot->GetMaxHealth());

    uint32 learned = LearnRoleSpells(bot, role);
    uint32 const upgrades = EquipBotGear(bot, targetLevel);
    LearnAutoTalents(bot);
    bot->SaveToDB();

    LogAi(bot->GetGUID(), std::string(bot->GetName()) + ": boosted to level " + std::to_string(uint32(targetLevel)) +
        " (from " + std::to_string(uint32(oldLevel)) + "), learned " + std::to_string(learned) + " spell(s)");

    if (upgrades)
        LogAi(bot->GetGUID(), std::string(bot->GetName()) + ": gear: equipped " + std::to_string(upgrades) + " upgrades");

    return true;
}

// Gives the requester leadership of the group the bot lives in. Creates the group (with the
// requester as leader, Group::Create Group.cpp:141) when the bot has none, refuses when the
// group contains a real player other than the requester, and uses the same call sequence as
// '.group leader' (cs_group.cpp:265-268): ChangeLeader + SendUpdate.
bool MidnightBotMgr::TakeGroupLead(std::string const& name, Player* issuer, uint32& botsUnder, std::string& err)
{
    botsUnder = 0;

    if (!issuer || !issuer->IsInWorld())
    {
        err = "an in-game player is required";
        return false;
    }

    Player* bot = ResolveActiveBotPlayer(name, err);
    if (!bot)
        return false;

    Group* group = bot->GetGroup();
    if (!group || !group->IsMember(issuer->GetGUID()))
    {
        // Same path as '.mb party': creates the group with the owner as leader, or adds the
        // bot to the owner's existing group.
        if (AddPlayerToGroup(issuer, bot, err) != GroupAddResult::Added)
            return false;

        group = issuer->GetGroup();
    }

    if (!group)
    {
        err = "no group could be created";
        return false;
    }

    // Safety: never take leadership of a group that contains other real players.
    for (GroupReference const& ref : group->GetMembers())
    {
        Player* member = ref.GetSource();
        if (!member || member == issuer)
            continue;

        if (!(member->GetSession() && member->GetSession()->IsBot()))
        {
            err = "group contains other players";
            return false;
        }

        ++botsUnder;
    }

    if (group->GetLeaderGUID() != issuer->GetGUID())
    {
        group->ChangeLeader(issuer->GetGUID()); // Group.h:265 / Group.cpp:663
        group->SendUpdate();
    }

    return true;
}

bool MidnightBotMgr::BotAttackTarget(std::string const& name, Player* owner, Unit* target, std::string& err)
{
    if (!owner || !owner->IsInWorld())
    {
        err = "an in-game player is required";
        return false;
    }

    if (!target)
    {
        err = "no target selected";
        return false;
    }

    Player* bot = ResolveActiveBotPlayer(name, err);
    if (!bot)
        return false;

    if (bot == owner)
    {
        err = "you are that bot";
        return false;
    }

    if (target == bot)
    {
        err = "a bot cannot attack itself";
        return false;
    }

    if (target == owner)
    {
        err = "a bot cannot attack its owner";
        return false;
    }

    if (bot->GetMapId() != target->GetMapId() || bot->GetInstanceId() != target->GetInstanceId())
    {
        err = "the target is on another map";
        return false;
    }

    if (!bot->InSamePhase(target))
    {
        err = "the target is in another phase";
        return false;
    }

    if (!IsSafeAttackTarget(bot, owner, target))
    {
        err = "that target is not a valid attack target";
        return false;
    }

    bool chase = false;
    BotRole role = BotRole::None;
    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&name](BotRef const& botRef)
        {
            return botRef.name == name;
        });

        if (itr != activeBots.end())
        {
            EnsureOwnerForCommand(*itr, owner, "attack");
            chase = itr->follow;
            role = itr->role;
        }
    }

    float desiredRange = 0.0f;
    if (role == BotRole::Dps)
        desiredRange = GetNukeRange(bot);
    else if (role == BotRole::Healer)
        desiredRange = 25.0f;

    bool const attackAccepted = StartBotAttack(bot, target, chase, desiredRange);
    NoteAttackResult(bot->GetGUID(), target, attackAccepted);
    if (!attackAccepted)
    {
        err = "Attack() was rejected by the core";
        return false;
    }

    return true;
}

bool MidnightBotMgr::ComeBot(std::string const& name, Player* issuer, std::string& err)
{
    if (!issuer || !issuer->IsInWorld())
    {
        err = "an in-game player is required";
        return false;
    }

    uint32 spreadIndex = 0;
    Player* bot = ResolveActiveBotPlayer(name, err, &spreadIndex);
    if (!bot)
        return false;

    ObjectGuid ownerGuid;
    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&name](BotRef const& botRef)
        {
            return botRef.name == name;
        });

        if (itr != activeBots.end())
        {
            EnsureOwnerForCommand(*itr, issuer, "come");
            ownerGuid = itr->ownerGuid;
        }
    }

    Player* owner = FindInWorldPlayer(ownerGuid);
    if (!owner)
        owner = issuer;

    if (!TeleportBotToPlayer(bot, owner, spreadIndex, err))
        return false;

    std::string followErr;
    SetBotFollow(name, true, owner, followErr);
    return true;
}

bool MidnightBotMgr::GetActiveBotInfo(std::string const& name, Player* viewer, ActiveBotInfo& info, std::string& err) const
{
    BotRef ref;
    bool found = false;

    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        for (BotRef const& bot : activeBots)
        {
            if (bot.name == name)
            {
                ref = bot;
                found = true;
                break;
            }
        }
    }

    if (!found)
    {
        err = "bot is not active";
        return false;
    }

    info = BuildBotInfo(ref, viewer);
    return true;
}

std::vector<MidnightBotMgr::ActiveBotInfo> MidnightBotMgr::ListActiveBotInfo(Player* viewer) const
{
    std::vector<BotRef> snapshot;
    {
        std::lock_guard<std::mutex> lock(rosterMutex);
        snapshot = activeBots;
    }

    std::vector<ActiveBotInfo> infos;
    infos.reserve(snapshot.size());
    for (BotRef const& ref : snapshot)
        infos.push_back(BuildBotInfo(ref, viewer));

    return infos;
}

bool MidnightBotMgr::DumpDebug(std::string const& version, std::string const& issuer, std::string const& onlyBot, std::string& outPath, std::string& err)
{
    std::vector<BotRef> snapshot;
    std::vector<std::string> rosterNames;
    std::string fileName;
    bool enabled;
    bool spawning;
    bool autoSpawnEnabled;
    bool summonOnSpawnEnabled;
    bool autoPartyEnabled;
    bool combatLogEnabled;

    {
        std::lock_guard<std::mutex> lock(rosterMutex);
        snapshot = activeBots;
        for (auto const& entry : roster)
            rosterNames.push_back(entry.second);
        fileName = debugFile;
        enabled = this->enabled;
        spawning = this->spawning;
        autoSpawnEnabled = autoSpawn;
        summonOnSpawnEnabled = summonOnSpawn;
        autoPartyEnabled = autoParty;
        combatLogEnabled = combatLog;
    }

    std::error_code absError;
    std::filesystem::path const absolutePath = std::filesystem::absolute(fileName, absError);
    outPath = absError ? fileName : absolutePath.string();

    std::ofstream out(fileName, std::ios::trunc);
    if (!out.is_open())
    {
        err = "could not open '" + fileName + "' for writing (check the worldserver working directory)";
        return false;
    }

    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);

    out << "MidnightBotAI " << version << " debug dump\n";
    out << "generated: ";
    if (tm)
        out << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
    else
        out << "?";
    out << ", issuer: " << issuer << "\n";
    out << "config: enabled=" << (enabled ? 1 : 0)
        << " spawning=" << (spawning ? 1 : 0)
        << " autospawn=" << (autoSpawnEnabled ? 1 : 0)
        << " summonOnSpawn=" << (summonOnSpawnEnabled ? 1 : 0)
        << " autoParty=" << (autoPartyEnabled ? 1 : 0)
        << " combatLog=" << (combatLogEnabled ? 1 : 0)
        << " debugFile='" << fileName << "'\n";
    out << "active bots: " << snapshot.size() << "\n";
    out << "roster (" << rosterNames.size() << "):";
    for (std::string const& name : rosterNames)
        out << " " << name;
    out << "\n";

    for (BotRef const& ref : snapshot)
    {
        if (!onlyBot.empty() && ref.name != onlyBot)
            continue;

        Player* bot = ObjectAccessor::FindConnectedPlayer(ref.playerGuid);
        Player* owner = FindInWorldPlayer(ref.ownerGuid);

        out << "\n=== Bot '" << ref.name << "' (guid " << ref.guid << ") ===\n";
        out << "role: " << RoleName(ref.role) << "\n";
        out << "follow: " << (ref.follow ? "on" : "off") << ", assist: " << (ref.assist ? "on" : "off") << "\n";

        if (ref.ownerGuid.IsEmpty())
            out << "owner: (none)\n";
        else
            out << "owner: " << ref.ownerGuid.ToString() << " ('" << (owner ? owner->GetName() : std::string("?"))
                << "'), owner found: " << (owner ? "yes" : "no") << "\n";

        out << "bot inWorld: " << (bot ? (bot->IsInWorld() ? "yes" : "no") : "not resolved") << "\n";

        if (bot)
        {
            out << "map: " << bot->GetMapId() << " instance: " << bot->GetInstanceId() << " zone: " << bot->GetZoneId() << "\n";
            out << "position: " << std::fixed << std::setprecision(1)
                << bot->GetPositionX() << " " << bot->GetPositionY() << " " << bot->GetPositionZ() << "\n";

            if (owner)
                out << "dist to owner: " << std::fixed << std::setprecision(1) << bot->GetExactDist(owner) << " yd\n";
            else
                out << "dist to owner: n/a\n";

            out << "InSamePhase(owner): " << ((owner && bot->InSamePhase(owner)) ? "yes" : "no") << "\n";
            out << "bot phases: [" << PhasingHandler::FormatPhases(bot->GetPhaseShift()) << "]\n";
            if (owner)
                out << "owner phases: [" << PhasingHandler::FormatPhases(owner->GetPhaseShift()) << "]\n";

            if (Unit* victim = bot->GetVictim())
            {
                Unit* mobVictim = victim->GetVictim();
                out << "victim: " << victim->GetName() << " (" << victim->GetGUID().ToString() << ", " << uint32(victim->GetHealthPct()) << "% hp)"
                    << ", threat bot=" << Fmt1(victim->GetThreatManager().GetThreat(bot))
                    << " owner=" << Fmt1(owner ? victim->GetThreatManager().GetThreat(owner) : 0.0f)
                    << ", mobVictim='" << (mobVictim ? mobVictim->GetName() : std::string("(none)")) << "'\n";
            }
            else
                out << "victim: (none)\n";

            out << "in combat: " << (bot->IsInCombat() ? "yes" : "no") << "\n";

            MovementGeneratorType motionType = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
            out << "motion: " << MovementTypeName(motionType);
            if (bot->HasUnitState(UNIT_STATE_FOLLOW))
                out << "(follow)";
            else if (bot->HasUnitState(UNIT_STATE_CHASE))
                out << "(chase)";
            out << "\n";
        }

        out << "AI log (last " << ref.aiLog.size() << "):\n";
        for (std::string const& line : ref.aiLog)
            out << "  " << line << "\n";
    }

    out.flush();
    return true;
}

// Full per-check visibility report for '.mb vis'. Also pushes three compact lines into the AI
// ring so the next auto debug dump keeps the result.
std::vector<std::string> MidnightBotMgr::VisibilityReport(std::string const& botName, Player* viewer, std::string& err)
{
    (void)viewer;

    std::vector<std::string> lines;

    Player* bot = ResolveActiveBotPlayer(botName, err);
    if (!bot)
        return lines;

    Player* owner = nullptr;
    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        auto itr = std::find_if(activeBots.begin(), activeBots.end(), [&botName](BotRef const& botRef)
        {
            return botRef.name == botName;
        });

        if (itr != activeBots.end())
            owner = ObjectAccessor::FindConnectedPlayer(itr->ownerGuid);
    }

    Unit* target = bot->GetVictim();
    if (!target && owner)
        target = owner->GetVictim();

    if (!target)
    {
        err = "no current victim (bot or owner) to inspect";
        return lines;
    }

    LogAi(bot->GetGUID(), botName + ": " + VisibilityReportCompact(bot, owner, target));
    LogAi(bot->GetGUID(), botName + ": " + VisibilityReportGeometry(bot, target));
    LogAi(bot->GetGUID(), botName + ": " + VisibilityReportMisc(bot, owner, target));

    return VisibilityReportLines(bot, owner, target);
}

void MidnightBotMgr::Update(uint32 diff)
{
    struct TickJob
    {
        ObjectGuid botGuid;
        ObjectGuid ownerGuid;
        bool follow = false;
        bool assist = false;
        BotRole role = BotRole::None;
        uint32 guid = 0;              // roster guid: stable unstack priority (higher guid yields)
        float spacingAngle = 0.0f;
        float spacingDist = 0.0f;
    };

    struct SyncJob
    {
        std::string name;
        ObjectGuid botGuid;
        ObjectGuid ownerGuid;
    };

    std::vector<std::string> autoSpawnTargets;
    std::vector<TickJob> tickJobs;
    std::vector<SyncJob> syncJobs;
    std::vector<ObjectGuid> talentJobs;
    bool checkTick = false;
    bool spacingDue = false;
    bool autoDump = false;
    bool sendStats = false;

    {
        std::lock_guard<std::mutex> lock(rosterMutex);

        uptime += diff;

        // One-shot auto-spawn: wait out the initial 15 seconds of uptime so the world and the
        // database are fully up before the socketless logins start pumping their callbacks.
        if (uptime >= AutoSpawnDelayMs && autoSpawn && !autoSpawnAttempted && enabled && spawning && !roster.empty())
        {
            autoSpawnAttempted = true;
            autoSpawnTargets.reserve(roster.size());
            for (auto const& bot : roster)
                autoSpawnTargets.push_back(bot.second);
        }

        if (enabled && spawning && !activeBots.empty())
        {
            for (auto itr = activeBots.begin(); itr != activeBots.end(); )
            {
                WorldSession* session = itr->session;
                if (!session)
                {
                    itr = activeBots.erase(itr);
                    continue;
                }

                WorldSession* registeredSession = sWorld->FindSession(itr->accountId);
                if (registeredSession == session)
                {
                    itr->inWorld = true;
                    itr->queued = false;
                    ++itr;
                    continue;
                }

                if (!registeredSession && sWorld->GetQueuePos(session) > 0)
                {
                    itr->queued = true;
                    ++itr;
                    continue;
                }

                if (registeredSession)
                {
                    TC_LOG_INFO("scripts.MidnightBotAI", "Bot '{}' session was replaced or closed by the core; dropping it from the active list", itr->name);
                    itr = activeBots.erase(itr);
                    continue;
                }

                ++itr;
            }
        }

        // Collect follow/assist candidates under the lock, but never move, phase or fight
        // anything while holding it.
        followTimer += diff;
        if (followTimer >= FollowRefreshMs)
        {
            followTimer = 0;
            checkTick = true;
            tickJobs.reserve(activeBots.size());
            for (BotRef const& bot : activeBots)
                tickJobs.push_back(TickJob{ bot.playerGuid, bot.ownerGuid, bot.follow, bot.assist, bot.role,
                    bot.guid, bot.spacingAngle, bot.spacingDist });
        }

        // Personal-space sweep (2 s cadence) rides along with the 500 ms follow tick.
        spacingTimer += diff;
        if (checkTick && spacingTimer >= SpacingCheckMs)
        {
            spacingTimer = 0;
            spacingDue = true;
        }

        // Periodic auto debug snapshot (no DB, overwrite) while bots are active.
        if (debugAuto && !activeBots.empty())
        {
            debugAutoTimer += diff;
            if (debugAutoTimer >= DebugAutoDumpMs)
            {
                debugAutoTimer = 0;
                autoDump = true;
            }
        }

        // Periodic damage stats block to the owners (only when bots were recently in combat).
        if (stats && !activeBots.empty())
        {
            statsTimer += diff;
            if (statsTimer >= 10000)
            {
                statsTimer = 0;
                sendStats = true;
            }
        }

        // Periodic level sync with the owner (10 s cadence, only when a level differs).
        if (levelSync && !activeBots.empty())
        {
            levelSyncTimer += diff;
            if (levelSyncTimer >= 10000)
            {
                levelSyncTimer = 0;
                for (BotRef const& bot : activeBots)
                    if (!bot.ownerGuid.IsEmpty())
                        syncJobs.push_back(SyncJob{ bot.name, bot.playerGuid, bot.ownerGuid });
            }
        }

        // Periodic talent sweep (10 s cadence; LearnAutoTalents no-ops when there are no free
        // points, e.g. below the first talent level).
        if (autoTalents && !activeBots.empty())
        {
            talentTimer += diff;
            if (talentTimer >= 10000)
            {
                talentTimer = 0;
                for (BotRef const& bot : activeBots)
                    if (!bot.playerGuid.IsEmpty())
                        talentJobs.push_back(bot.playerGuid);
            }
        }
    }

    // Spawn outside the roster lock: SpawnBot takes the lock itself and may block for a long
    // time loading maps/vmaps while the world thread pumps the socketless login callbacks.
    for (std::string const& name : autoSpawnTargets)
    {
        std::string err;
        if (SpawnBot(name, err))
            TC_LOG_INFO("scripts.MidnightBotAI", "AutoSpawn: spawned bot '{}'", name);
        else
            TC_LOG_WARN("scripts.MidnightBotAI", "AutoSpawn: could not spawn bot '{}': {}", name, err);
    }

    if (autoDump)
    {
        std::string outPath;
        std::string dumpErr;
        if (!DumpDebug(versionString, "auto", "", outPath, dumpErr))
            TC_LOG_WARN("scripts.MidnightBotAI", "AutoDebug: could not write '{}': {}", outPath, dumpErr);
    }

    if (sendStats)
        SendStatsToOwners();

    for (SyncJob const& sync : syncJobs)
    {
        Player* botPlayer = FindInWorldPlayer(sync.botGuid);
        Player* owner = FindInWorldPlayer(sync.ownerGuid);
        if (!botPlayer || !owner || botPlayer->GetLevel() == owner->GetLevel())
            continue;

        std::string syncErr;
        if (BoostBot(sync.name, owner, owner->GetLevel(), syncErr))
            LogAi(sync.botGuid, sync.name + ": sync: -> level " + std::to_string(uint32(owner->GetLevel())));
        else
            TC_LOG_WARN("scripts.MidnightBotAI", "LevelSync: could not sync bot '{}': {}", sync.name, syncErr);
    }

    for (ObjectGuid const& botGuid : talentJobs)
        if (Player* botPlayer = FindInWorldPlayer(botGuid))
            LearnAutoTalents(botPlayer);

    if (checkTick)
    {
        for (TickJob const& job : tickJobs)
        {
            Player* bot = FindInWorldPlayer(job.botGuid);
            if (!bot)
                continue; // bot still loading or gone

            std::string const tag = std::string(bot->GetName()) + "(" + RoleName(job.role) + ")";
            auto note = [&](std::string const& message)
            {
                LogAi(job.botGuid, tag + ": " + message);
            };

            // Bots must be alive to do anything; resurrect once per death event (no DB here).
            if (EnsureBotAlive(bot))
                note("resurrected (was dead)");

            // Cast tracking: report casts that vanish before their expected end (interrupts) and
            // prove/disprove damage for completed casts. Victim tracking keeps acquisition time.
            CheckBotCastState(job.botGuid, bot);
            CheckPendingCasts(job.botGuid, bot);
            TrackBotMeleeDamage(job.botGuid, bot);
            TrackBotVictim(job.botGuid, bot);
            TrackBotDamageTaken(job.botGuid, bot);

            Player* owner = FindInWorldPlayer(job.ownerGuid);
            bool const sameMap = owner && bot->GetMapId() == owner->GetMapId() && bot->GetInstanceId() == owner->GetInstanceId();

            if (job.ownerGuid.IsEmpty())
                note("no owner (use follow/assist/summon to bind)");
            else if (!owner)
                note("owner not in world");
            else if (owner->isDead())
                note("owner dead");

            // Stop conditions: dead/gone/phase-mismatched victim, or combat that is over.
            // A freshly acquired victim is kept for VictimGraceMs so melee can close in without
            // being dropped while neither side is officially in combat yet.
            if (Unit* victim = bot->GetVictim())
            {
                bool keep = victim->IsAlive() && victim->IsInWorld() && bot->InSamePhase(victim);

                if (keep && job.assist && owner && !BotVictimInGrace(job.botGuid, VictimGraceMs))
                {
                    if (job.role == BotRole::Tank)
                    {
                        // Tanks keep holding a mob while it is on a party member or anyone fights.
                        keep = IsPartyMember(bot, owner, victim->GetVictim()) || victim->GetVictim() == bot ||
                            owner->IsInCombat() || bot->IsInCombat() ||
                            bot->HasUnitState(UNIT_STATE_CHASE) || BotIsBusyCasting(bot);
                    }
                    else
                    {
                        // Sticky target: only drop it once both the bot and the owner are out of
                        // combat with it (and it is not attacking either of them) and there is no
                        // pending chase/cast.
                        keep = bot->IsInCombat() || owner->IsInCombat() ||
                            victim->GetVictim() == bot || victim->GetVictim() == owner ||
                            bot->HasUnitState(UNIT_STATE_CHASE) || BotIsBusyCasting(bot);
                    }
                }

                if (!keep)
                {
                    char const* releaseReason = "combat ended";
                    if (!victim->IsAlive())
                        releaseReason = "target dead";
                    else if (!victim->IsInWorld())
                        releaseReason = "target gone";
                    else if (!bot->InSamePhase(victim))
                        releaseReason = "phase mismatch";

                    note(std::string("released '") + victim->GetName() + "' (" + releaseReason + ")");
                    bot->AttackStop();
                    bot->SetSelection(ObjectGuid::Empty);
                }
            }

            if (!sameMap)
            {
                // Different map/instance or owner gone: stop an already running follow, the tick
                // restarts it automatically when both players share a map again.
                if (owner)
                    note("owner on another map/instance");

                if (bot->HasUnitState(UNIT_STATE_FOLLOW))
                {
                    bot->GetMotionMaster()->Remove(FOLLOW_MOTION_TYPE);
                    bot->GetMotionMaster()->MoveIdle();
                    bot->StopMoving();
                }
                continue;
            }

            if (InheritOwnerPhase(bot, owner))
                bot->UpdateObjectVisibility(true);

            if (!job.assist)
            {
                note("assist off (idle)");
            }
            else
            {
                // Movement priority: while a victim exists, chase movement beats follow. The
                // follow generator is removed before MoveChase and the follow block below never
                // runs with a victim. Stay mode (follow off) never chases.
                // No movement at all while the bot is casting (prevents cast cancellation).
                if (Unit* victim = bot->GetVictim(); victim && !BotIsBusyCasting(bot))
                {
                    float desiredRange = 0.0f;
                    if (job.role == BotRole::Dps)
                        desiredRange = GetNukeRange(bot);
                    else if (job.role == BotRole::Healer)
                        desiredRange = 25.0f;

                    bool const inRange = desiredRange > 0.0f
                        ? bot->IsWithinDist(victim, desiredRange)
                        : bot->IsWithinMeleeRange(victim);

                    if (!inRange && job.follow)
                    {
                        if (bot->HasUnitState(UNIT_STATE_FOLLOW))
                            bot->GetMotionMaster()->Remove(FOLLOW_MOTION_TYPE);

                        if (!bot->HasUnitState(UNIT_STATE_CHASE))
                            bot->GetMotionMaster()->MoveChase(victim);
                    }
                    else if (inRange && bot->HasUnitState(UNIT_STATE_CHASE))
                    {
                        bot->GetMotionMaster()->Remove(CHASE_MOTION_TYPE);
                        bot->StopMoving();
                    }
                }

                // LOS recovery + caster spacing: reposition and pause swings/casts for this tick.
                bool combatAllowed = true;

                if (Unit* victim = bot->GetVictim(); victim && !BotIsBusyCasting(bot))
                {
                    // Ranged DPS being meleed backs away to ~15 yd instead of trading hits.
                    bool const casterDps = (job.role == BotRole::Dps)
                        && (bot->GetClass() == CLASS_MAGE || bot->GetClass() == CLASS_WARLOCK || bot->GetClass() == CLASS_PRIEST);

                    if (casterDps && victim->GetVictim() == bot && bot->GetDistance(victim) < 6.0f)
                    {
                        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
                        {
                            // Fan the retreat out by the bot's personal angle (cos of the
                            // golden-angle slot gives mage/warlock opposite lateral signs),
                            // with a small per-bot radius variance.
                            float const away = victim->GetAbsoluteAngle(bot);   // direction from mob to bot
                            float const heading = away + std::cos(job.spacingAngle) * 1.1f;
                            float const radius = 15.0f + job.spacingDist * 4.0f; // 13/15/17 yd
                            float const x = bot->GetPositionX() + std::cos(heading) * radius;
                            float const y = bot->GetPositionY() + std::sin(heading) * radius;
                            float const z = bot->GetPositionZ();
                            bot->GetMotionMaster()->MovePoint(0, x, y, z);
                            note("kite: backing away from '" + victim->GetName() + "'");
                        }

                        combatAllowed = false;
                    }

                    if (combatAllowed && !bot->IsWithinLOSInMap(victim))
                    {
                        if (!bot->HasUnitState(UNIT_STATE_CHASE))
                            bot->GetMotionMaster()->MoveChase(victim);

                        note("los: repositioning for '" + victim->GetName() + "'");
                        combatAllowed = false;
                    }
                }

                if (combatAllowed)
                {
                // Manual white swing: bypasses DoMeleeAttackIfReady's IsValidAttackTarget gate
                // (which blocks owner-visible targets), so melee bots actually hit and generate
                // threat. Rate-limited by the weapon timer; one ring line per swing.
                if (Unit* victim = bot->GetVictim(); victim && !BotIsBusyCasting(bot))
                {
                    bool meleeRole = (job.role == BotRole::Tank || job.role == BotRole::None);
                    if (!meleeRole && job.role == BotRole::Dps)
                    {
                        switch (bot->GetClass())
                        {
                            case CLASS_WARRIOR:
                            case CLASS_ROGUE:
                            case CLASS_DEATH_KNIGHT:
                            case CLASS_PALADIN:
                                meleeRole = true;
                                break;
                            default:
                                break;
                        }
                    }

                    if (meleeRole && bot->IsWithinMeleeRange(victim) && TryBotMeleeSwing(job.botGuid, bot))
                    {
                        uint64 const healthBefore = victim->GetHealth();
                        bot->AttackerStateUpdate(victim, BASE_ATTACK);   // Unit.h:964
                        bot->resetAttackTimer(BASE_ATTACK);              // Unit.h:711
                        bot->setAttackTimer(BASE_ATTACK, bot->GetBaseAttackTime(BASE_ATTACK)); // Unit.h:710

                        uint64 const healthAfter = victim->GetHealth();
                        uint64 const delta = healthBefore > healthAfter ? healthBefore - healthAfter : 0;

                        note("swing '" + victim->GetName() + "': hp " + std::to_string(healthBefore)
                            + " -> " + std::to_string(healthAfter) + " (delta " + std::to_string(delta) + ")");
                    }
                }

                // One ability per tick from the warrior kit (used by tank and melee dps roles).
                auto runWarriorRotation = [&](Unit* victim)
                {
                    BotAbilityChoice choice = ChooseWarriorAbility(bot, victim);
                    if (!choice.spellId)
                        return;

                    if (choice.selfCast)
                    {
                        bot->SetSelection(bot->GetGUID());
                        SpellCastResult result = BotCastSpell(bot, bot, choice.spellId, owner, false);
                        note(std::string("shout '") + bot->GetName() + "' " + choice.name + " spell "
                            + std::to_string(choice.spellId) + " => " + SpellResultName(result));
                        return;
                    }

                    uint32 const paceMs = (choice.spellId == 100) ? 15000u
                        : (choice.spellId == 772) ? 6000u
                        : (choice.spellId == 6673) ? 120000u
                        : (choice.spellId == 78) ? 2000u
                        : 3000u;

                    if (!TryBotSpellPace(job.botGuid, choice.spellId, paceMs))
                    {
                        note(std::string("strike blocked (pacing ") + choice.name + ") spell " + std::to_string(choice.spellId));
                        return;
                    }

                    if (!CanBotCast(bot, choice.spellId, victim, choice.spellId == 100 ? 25.0f : 5.0f))
                    {
                        note(std::string("strike blocked (") + choice.name + ") spell " + std::to_string(choice.spellId));
                        return;
                    }

                    bot->SetSelection(victim->GetGUID());
                    FreezeBotForCast(bot, victim);
                    SpellCastResult result = BotCastAndReport(job.botGuid, bot, victim, choice.spellId, owner, true);
                    if (choice.spellId == 100)
                        note(std::string("opener: charge '") + victim->GetName() + "' spell "
                            + std::to_string(choice.spellId) + " => " + SpellResultName(result));
                    else
                        note(std::string("strike '") + victim->GetName() + "' " + choice.name + " spell "
                            + std::to_string(choice.spellId) + " => " + SpellResultName(result));
                };

                // Paladin kit: keep Devotion Aura + Seal of Righteousness up, then Judgement.
                auto runPaladinRotation = [&](Unit* victim)
                {
                    BotAbilityChoice choice = ChoosePaladinAbility(bot, victim);
                    if (!choice.spellId)
                        return;

                    if (choice.selfCast)
                    {
                        if (!TryBotSpellPace(job.botGuid, choice.spellId, 30000u))
                            return; // buffs stay silent while on pace

                        bot->SetSelection(bot->GetGUID());
                        SpellCastResult result = BotCastSpell(bot, bot, choice.spellId, owner, false);
                        note(std::string("buff '") + bot->GetName() + "' " + choice.name + " spell "
                            + std::to_string(choice.spellId) + " => " + SpellResultName(result));
                        return;
                    }

                    // Judgement is a 10 yd ability in this data set; the ranged opener uses the
                    // full range, everything else stays melee-gated.
                    float const castRange = (choice.spellId == 20271) ? 10.0f : 5.0f;
                    if (!TryBotSpellPace(job.botGuid, choice.spellId, 8000u) || !CanBotCast(bot, choice.spellId, victim, castRange))
                        return;

                    bool const rangedOpener = (choice.spellId == 20271) && !bot->IsWithinMeleeRange(victim);
                    bot->SetSelection(victim->GetGUID());
                    FreezeBotForCast(bot, victim);
                    SpellCastResult result = BotCastAndReport(job.botGuid, bot, victim, choice.spellId, owner, true);
                    if (rangedOpener)
                        note(std::string("opener: judgement '") + victim->GetName() + "' spell "
                            + std::to_string(choice.spellId) + " => " + SpellResultName(result));
                    else
                        note(std::string("strike '") + victim->GetName() + "' " + choice.name + " spell "
                            + std::to_string(choice.spellId) + " => " + SpellResultName(result));
                };

                switch (job.role)
                {
                    case BotRole::Tank:
                    {
                        // Target priority: party attackers -> owner victim -> self attackers.
                        if (!bot->GetVictim())
                        {
                            std::vector<std::string> rejected;
                            char const* reason = "tank";
                            if (Unit* target = FindTankTarget(bot, owner, reason, rejected))
                            {
                                note(std::string("attacking '") + target->GetName() + "' (" + reason + ")");
                                if (std::string_view(reason) == "owner-visible")
                                    note(OwnerVisibleDiagnostics(bot, owner, target));
                                NoteAttackResult(job.botGuid, target, StartBotAttack(bot, target, job.follow, 0.0f));
                            }
                            else if (!rejected.empty())
                                note("no target; " + rejected.front());
                            else
                                note("no target (party/owner/self)");
                        }

                        if (Unit* victim = bot->GetVictim())
                        {
                            // Taunt a mob that peeled onto a party member when the ability is known.
                            if (victim->GetVictim() && victim->GetVictim() != bot && IsPartyMember(bot, owner, victim->GetVictim()))
                            {
                                if (uint32 taunt = GetTauntSpell(bot); taunt && CanBotCast(bot, taunt, victim, 30.0f))
                                {
                                    bot->SetSelection(victim->GetGUID());
                                    FreezeBotForCast(bot, victim);
                                    SpellCastResult result = BotCastAndReport(job.botGuid, bot, victim, taunt, owner, true);
                                    note(std::string("taunt '") + victim->GetName() + "' spell " + std::to_string(taunt) + " => " + SpellResultName(result));
                                }
                            }

                            // Class rotation: warriors use a small prioritized kit, other classes
                            // one offensive ability. One ability per tick.
                            if (bot->GetClass() == CLASS_WARRIOR)
                                runWarriorRotation(victim);
                            else if (bot->GetClass() == CLASS_PALADIN)
                                runPaladinRotation(victim);
                            else if (uint32 ability = GetOffensiveAbility(bot))
                            {
                                if (!bot->IsWithinMeleeRange(victim))
                                    note("strike blocked (out of melee range) spell " + std::to_string(ability));
                                else if (!CanBotCast(bot, ability, victim, 5.0f))
                                    note("strike blocked (cooldown/casting) spell " + std::to_string(ability));
                                else
                                {
                                    bot->SetSelection(victim->GetGUID());
                                    FreezeBotForCast(bot, victim);
                                    SpellCastResult result = BotCastAndReport(job.botGuid, bot, victim, ability, owner, true);
                                    note(std::string("strike '") + victim->GetName() + "' spell " + std::to_string(ability) + " => " + SpellResultName(result));
                                }
                            }
                        }
                        break;
                    }
                    case BotRole::Healer:
                    {
                        float healPct = 0.0f;
                        std::vector<ObjectGuid> const tankGuids = GetTankBotGuids();
                        Player* healTarget = FindHealTarget(bot, owner, healPct, tankGuids);
                        uint32 healSpell = GetBestHealSpell(bot, healTarget ? healPct : 0.0f);

                        if (healTarget && healSpell)
                        {
                            uint32 const healPace = (healPct < 40.0f) ? 800u : 1500u;
                            if (!TryBotSpellPace(job.botGuid, healSpell, healPace))
                                note("heal blocked (pacing) spell " + std::to_string(healSpell));
                            else if (CanBotCast(bot, healSpell, healTarget, 40.0f))
                            {
                                bot->SetSelection(healTarget->GetGUID());
                                FreezeBotForCast(bot, healTarget);
                                SpellCastResult result = BotCastAndReport(job.botGuid, bot, healTarget, healSpell, owner, false);
                                note("heal '" + healTarget->GetName() + "' (" + std::to_string(uint32(healPct)) + "%) spell " + std::to_string(healSpell) + " => " + SpellResultName(result));
                            }
                            else
                                note("heal blocked (cooldown/mana/range/being cast) spell " + std::to_string(healSpell));
                        }
                        else if (!bot->GetVictim())
                        {
                            note(healSpell ? "no heal target (all >= 70%)" : "no heal spell known");

                            // Nothing to heal: attack only in self-defense / when the owner is
                            // attacked, and never chase while following the owner.
                            Unit* target = nullptr;
                            char const* reason = "self defense";
                            std::vector<std::string> rejected;

                            if (Unit* selfAttacker = bot->getAttackerForHelper())
                            {
                                TargetCheck check = CheckAttackTarget(bot, owner, selfAttacker);
                                if (check.rejection)
                                    rejected.push_back(std::string("rejected '") + selfAttacker->GetName() + "' (" + check.rejection + ")");
                                else
                                {
                                    target = selfAttacker;
                                    reason = check.ownerVisible ? "owner-visible" : "self defense";
                                }
                            }

                            if (!target && owner)
                                if (Unit* ownerAttacker = owner->getAttackerForHelper())
                                {
                                    TargetCheck check = CheckAttackTarget(bot, owner, ownerAttacker);
                                    if (check.rejection)
                                        rejected.push_back(std::string("rejected '") + ownerAttacker->GetName() + "' (" + check.rejection + ")");
                                    else
                                    {
                                        target = ownerAttacker;
                                        reason = check.ownerVisible ? "owner-visible" : "defend owner";
                                    }
                                }

                            if (target)
                            {
                                note(std::string("attacking '") + target->GetName() + "' (" + reason + ")");
                                if (std::string_view(reason) == "owner-visible")
                                    note(OwnerVisibleDiagnostics(bot, owner, target));
                                NoteAttackResult(job.botGuid, target, StartBotAttack(bot, target, false, 0.0f));
                            }
                            else if (!rejected.empty())
                                note("no target; " + rejected.front());
                        }

                        // Priest adds Smite when there is nothing to heal; Paladin uses Judgement
                        // on the defensive target. Offensive casts go through BotCastAndReport.
                        if (!(healTarget && healSpell))
                        {
                            uint32 offensiveSpell = 0;
                            Unit* offensiveTarget = nullptr;

                            if (bot->GetClass() == CLASS_PRIEST && bot->HasSpell(585))
                            {
                                offensiveSpell = 585;
                                offensiveTarget = bot->GetVictim();
                                if (!offensiveTarget && owner)
                                    offensiveTarget = owner->GetVictim();
                            }
                            else if (bot->GetClass() == CLASS_PALADIN)
                            {
                                offensiveSpell = GetOffensiveAbility(bot);
                                offensiveTarget = bot->GetVictim();
                            }

                            if (offensiveSpell && offensiveTarget && IsSafeAttackTarget(bot, owner, offensiveTarget))
                            {
                                if (!TryBotSpellPace(job.botGuid, offensiveSpell, 1500))
                                    note("nuke blocked (pacing) spell " + std::to_string(offensiveSpell));
                                else if (CanBotCast(bot, offensiveSpell, offensiveTarget, offensiveSpell == 585 ? 30.0f : 5.0f))
                                {
                                    bot->SetSelection(offensiveTarget->GetGUID());
                                    FreezeBotForCast(bot, offensiveTarget);
                                    SpellCastResult result = BotCastAndReport(job.botGuid, bot, offensiveTarget, offensiveSpell, owner, true);
                                    note(std::string("nuke '") + offensiveTarget->GetName() + "' spell " + std::to_string(offensiveSpell) + " => " + SpellResultName(result));
                                }
                            }
                        }
                        break;
                    }
                    case BotRole::Dps:
                    case BotRole::None:
                    default:
                    {
                        if (!bot->GetVictim())
                        {
                            Unit* target = nullptr;
                            char const* reason = "assist";
                            std::vector<std::string> rejected;

                            if (job.role == BotRole::Dps)
                            {
                                // Focus fire, but only once the tank owns the fight: with a
                                // tank-role bot active, DPS waits for the tank's threat on its
                                // victim unless the bot or the owner is attacked.
                                Player* tankBot = FindTankBot(bot);
                                bool const selfOrOwnerAttacked = bot->getAttackerForHelper()
                                    || (owner && owner->getAttackerForHelper());

                                if (!tankBot)
                                    target = FindDpsTarget(bot, owner, reason, rejected);
                                else
                                {
                                    Unit* tankVictim = tankBot->GetVictim();
                                    bool const tankHasThreat = tankVictim &&
                                        (tankVictim->GetVictim() == tankBot
                                            || tankVictim->GetThreatManager().GetThreat(tankBot) > 0.0f);

                                    if (tankVictim && tankHasThreat && IsSafeAttackTarget(bot, owner, tankVictim))
                                    {
                                        target = tankVictim;
                                        reason = "tank target";
                                    }
                                    else if (selfOrOwnerAttacked)
                                        target = FindDpsTarget(bot, owner, reason, rejected);
                                    else if (DpsWaitLogDue(job.botGuid))
                                        note("dps waiting for tank threat");
                                }
                            }
                            else
                            {
                                // Legacy flat assist order: own attackers, owner attackers, owner victim.
                                if (Unit* selfAttacker = bot->getAttackerForHelper())
                                {
                                    TargetCheck check = CheckAttackTarget(bot, owner, selfAttacker);
                                    if (check.rejection)
                                        rejected.push_back(std::string("rejected '") + selfAttacker->GetName() + "' (" + check.rejection + ")");
                                    else
                                    {
                                        target = selfAttacker;
                                        reason = check.ownerVisible ? "owner-visible" : "self defense";
                                    }
                                }

                                if (!target && owner)
                                    if (Unit* ownerAttacker = owner->getAttackerForHelper())
                                    {
                                        TargetCheck check = CheckAttackTarget(bot, owner, ownerAttacker);
                                        if (check.rejection)
                                            rejected.push_back(std::string("rejected '") + ownerAttacker->GetName() + "' (" + check.rejection + ")");
                                        else
                                        {
                                            target = ownerAttacker;
                                            reason = check.ownerVisible ? "owner-visible" : "defend owner";
                                        }
                                    }

                                if (!target && owner)
                                    if (Unit* ownerVictim = owner->GetVictim())
                                    {
                                        TargetCheck check = CheckAttackTarget(bot, owner, ownerVictim);
                                        if (check.rejection)
                                            rejected.push_back(std::string("rejected '") + ownerVictim->GetName() + "' (" + check.rejection + ")");
                                        else
                                        {
                                            target = ownerVictim;
                                            reason = check.ownerVisible ? "owner-visible" : "owner victim";
                                        }
                                    }
                            }

                            if (target)
                            {
                                note(std::string("attacking '") + target->GetName() + "' (" + reason + ")");
                                if (std::string_view(reason) == "owner-visible")
                                    note(OwnerVisibleDiagnostics(bot, owner, target));
                                NoteAttackResult(job.botGuid, target, StartBotAttack(bot, target, job.follow, job.role == BotRole::Dps ? GetNukeRange(bot) : 0.0f));
                            }
                            else if (!rejected.empty())
                                note("no target; " + rejected.front());
                            else if (job.role == BotRole::Dps)
                                note("no target (owner victim/self attacks)");
                        }

                        // One rank-1 nuke when in range, off cooldown and with mana. Warlocks
                        // apply Immolate first while the victim lacks its DoT, then Shadow Bolt.
                        if (Unit* victim = bot->GetVictim())
                        {
                            uint32 nuke = GetNukeSpell(bot);
                            if (bot->GetClass() == CLASS_WARLOCK && bot->HasSpell(348) && !victim->HasAura(348))
                                nuke = 348;

                            if (nuke)
                            {
                                if (!TryBotSpellPace(job.botGuid, nuke, 1500))
                                    note("nuke blocked (pacing) spell " + std::to_string(nuke));
                                else if (CanBotCast(bot, nuke, victim, GetNukeRange(bot)))
                                {
                                    bot->SetSelection(victim->GetGUID());
                                    FreezeBotForCast(bot, victim);
                                    SpellCastResult result = BotCastAndReport(job.botGuid, bot, victim, nuke, owner, true);
                                    note(std::string("nuke '") + victim->GetName() + "' spell " + std::to_string(nuke) + " => " + SpellResultName(result));
                                }
                                else
                                    note("nuke blocked (cooldown/mana/range/casting) spell " + std::to_string(nuke));
                            }
                        }

                        // Warrior dps/none roles use the melee kit instead of a nuke.
                        if (bot->GetClass() == CLASS_WARRIOR)
                            if (Unit* victim = bot->GetVictim())
                                runWarriorRotation(victim);
                        break;
                    }
                }
                }
            }

            // Follow only while not fighting, not casting, and only when map/instance/phase all
            // match; the role movement (chase or attack-in-place) takes over during combat.
            if (job.follow && !bot->GetVictim() && !BotIsBusyCasting(bot) && bot->InSamePhase(owner))
            {
                if (!bot->HasUnitState(UNIT_STATE_FOLLOW))
                    bot->GetMotionMaster()->MoveFollow(owner, FollowDistanceForRole(job.role) + job.spacingDist, job.spacingAngle);
            }

            // Personal-space maintenance: when a lower-guid bound bot is standing on top of this
            // idle/following bot (both parked), the higher guid yields and steps to its own slot.
            if (spacingDue && job.follow && owner && !owner->isDead() && sameMap &&
                !bot->GetVictim() && !BotIsBusyCasting(bot) && bot->InSamePhase(owner))
            {
                MovementGeneratorType const motion = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
                if (motion == FOLLOW_MOTION_TYPE || motion == IDLE_MOTION_TYPE)
                {
                    for (TickJob const& other : tickJobs)
                    {
                        if (other.guid >= job.guid)
                            continue;

                        Player* otherBot = FindInWorldPlayer(other.botGuid);
                        if (!otherBot || otherBot == bot || !otherBot->IsInWorld())
                            continue;

                        if (otherBot->GetMapId() != bot->GetMapId() || otherBot->GetInstanceId() != bot->GetInstanceId())
                            continue;

                        if (bot->GetDistance(otherBot) > 1.2f)
                            continue;

                        if (otherBot->GetVictim() || BotIsBusyCasting(otherBot))
                            continue;

                        MovementGeneratorType const otherMotion = otherBot->GetMotionMaster()->GetCurrentMovementGeneratorType();
                        if (otherMotion != FOLLOW_MOTION_TYPE && otherMotion != IDLE_MOTION_TYPE && otherMotion != POINT_MOTION_TYPE)
                            continue;

                        float const dist = FollowDistanceForRole(job.role) + job.spacingDist;
                        float const x = owner->GetPositionX() + std::cos(job.spacingAngle) * dist;
                        float const y = owner->GetPositionY() + std::sin(job.spacingAngle) * dist;
                        float const z = owner->GetPositionZ();
                        bot->GetMotionMaster()->MovePoint(0, x, y, z);
                        note("spacing: unstacking from '" + std::string(otherBot->GetName()) + "'");
                        break;
                    }
                }
            }
        }
    }
}
