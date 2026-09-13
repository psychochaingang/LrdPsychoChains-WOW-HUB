/*
 * The Sanguinith (The Bloodbinder) - self-bot foundation.
 * guid 5000 "Vaelith": Darkfallen morph, Sanguine Aura, Vitae (mana) meter,
 * Vampiric Thrall minion and the core kit spell scripts.
 *
 * This file is part of the AzerothCore Project. Licensed under GPL v2.
 */

#include "Cell.h"
#include "CellImpl.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DatabaseEnv.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"
#include "SpellScript.h"

#include <map>
#include <vector>

namespace
{
    constexpr uint32 SANGUINITH_GUID   = 5000;
    constexpr uint32 THRALL_ENTRY      = 91100;
    constexpr uint32 VITAE_AURA        = 91035; // Sanguine Aura (visual)
    constexpr uint32 BLOOD_STRIKE      = 91031;
    constexpr uint32 REND              = 91032;
    constexpr uint32 BLOOD_BOIL        = 91033;

    // Script-managed bleed: target guid -> (owner guid, remaining duration)
    std::map<ObjectGuid, std::pair<ObjectGuid, uint32>> s_bleeds;
}

// ---------------------------------------------------------------------------
// Player script: morph, scale, aura, thrall, Vitae decay, bleed ticks
// ---------------------------------------------------------------------------
class sanguinith_player_script : public PlayerScript
{
public:
    sanguinith_player_script() : PlayerScript("sanguinith_player_script", {PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_UPDATE}) { }

    void OnPlayerLogin(Player* player) override
    {
        uint32 const guid = player->GetGUID().GetCounter();

        if (guid == 1001)
        {
            // The Black Legion core party re-assembles under the leader.
            EnsureBlackLegionParty();
            return;
        }

        if (guid != SANGUINITH_GUID)
            return;

        // Darkfallen vampire form, scaled to match the party bots.
        player->SetDisplayId(31415);
        player->SetObjectScale(0.8f);

        // Sanguine Aura (red body aura).
        if (!player->HasAura(VITAE_AURA))
            player->CastSpell(player, VITAE_AURA, true);
    }

    void OnPlayerUpdate(Player* player, uint32 /*diff*/) override
    {
        // Vaelith's combat automation (thrall upkeep, kit rotation, bleed
        // ticks) is disabled - it caused repeated worldserver crashes in the
        // world bot environment. She remains a cosmetic/party character only.
        (void)player;
    }

private:
    // Re-assembles the Black Legion core party in the database so the group
    // forms correctly at every world boot / leader login.
    // Core roster: leader 1001 + Tanklord/Lovley/Ember/Death/Tankqueenz/Iong.
    void EnsureBlackLegionParty()
    {
        std::vector<uint32> roster = { 1001, 1002, 1003, 1004, 1006, 1007, 1008 };

        // Remove Vaelith from any party group membership.
        CharacterDatabase.DirectExecute("DELETE FROM group_member WHERE memberGuid = 5000");

        // Find (or create) the leader's group.
        QueryResult groupRes = CharacterDatabase.Query(
            "SELECT guid FROM groups WHERE leaderGuid = 1001 LIMIT 1");
        uint32 groupId = 0;
        if (groupRes)
        {
            groupId = (*groupRes)[0].Get<uint32>();
        }
        else
        {
            QueryResult maxRes = CharacterDatabase.Query("SELECT MAX(guid) FROM groups");
            groupId = (maxRes && (*maxRes)[0].Get<uint32>()) ? (*maxRes)[0].Get<uint32>() + 1 : 1;
            CharacterDatabase.DirectExecute(
                "INSERT INTO groups (guid, leaderGuid, lootMethod, looterGuid, lootThreshold, icon1, icon2, icon3, icon4, icon5, icon6, icon7, icon8, groupType, difficulty, raidDifficulty) "
                "VALUES ({}, 1001, 2, 1001, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1)", groupId);
        }

        if (!groupId)
            return;

        // Ensure every core member is in the group.
        for (uint32 member : roster)
            CharacterDatabase.DirectExecute(
                "INSERT IGNORE INTO group_member (guid, memberGuid, memberFlags, subgroup, roles) "
                "VALUES ({}, {}, 0, 0, 0)", groupId, member);

        // Ensure no other group keeps a core member (avoid duplicate groups).
        CharacterDatabase.DirectExecute(
            "DELETE gm FROM group_member gm "
            "WHERE gm.guid <> {} AND gm.memberGuid IN (1001, 1002, 1003, 1004, 1006, 1007, 1008)", groupId);
    }
    // Summons the given minion for the player if they are missing it.
    // Optional item entry: only summon while the player still carries it.
    void MaybeSummonMinion(Player* player, uint32 entry, uint32 itemEntry)
    {
        if (!player->IsInWorld() || player->IsDuringRemoveFromWorld() || !player->IsAlive())
            return;

        if (itemEntry && player->HasItemCount(itemEntry, 1, true) < 1)
            return;

        std::list<Creature*> minions;
        player->GetCreatureListWithEntryInGrid(minions, entry, 50.0f);
        for (Creature* c : minions)
            if (c->IsSummon() && c->ToTempSummon() && c->ToTempSummon()->GetSummonerGUID() == player->GetGUID())
                return; // already present

        player->SummonCreature(entry,
            player->GetPositionX() + 2.0f, player->GetPositionY() - 2.0f,
            player->GetPositionZ(), player->GetOrientation(),
            TEMPSUMMON_MANUAL_DESPAWN, 0);
    }

    uint32 m_thrallTimer = 0;
    uint32 m_kitTimer = 0;
    uint32 m_kitIdx = 0;
    void TickBleeds(Player* player, uint32 diff)
    {
        if (s_bleeds.empty())
            return;

        for (auto it = s_bleeds.begin(); it != s_bleeds.end();)
        {
            Unit* target = ObjectAccessor::GetUnit(*player, it->first);
            uint32& remaining = it->second.second;
            if (!target || !target->IsAlive() || remaining == 0)
            {
                it = s_bleeds.erase(it);
                continue;
            }

            // Tick once per second.
            remaining = remaining > diff ? remaining - diff : 0;
            if (remaining % 1000 > (remaining + diff) % 1000)
                Unit::DealDamage(player, target, uint32(player->GetTotalAttackPowerValue(BASE_ATTACK) * 0.12f), nullptr, DOT, SPELL_SCHOOL_MASK_SHADOW, sSpellMgr->GetSpellInfo(REND));

            ++it;
        }
    }
};

// ---------------------------------------------------------------------------
// Kit spell scripts
// ---------------------------------------------------------------------------
class sanguinith_blood_strike : public SpellScript
{
    PrepareSpellScript(sanguinith_blood_strike);

    void HandleHit()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target)
            return;

        Unit::DealDamage(caster, target, uint32(caster->GetTotalAttackPowerValue(BASE_ATTACK) * 0.6f), nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, GetSpellInfo());

        if (Player* plr = caster->ToPlayer())
            plr->ModifyPower(POWER_MANA, 10); // +Vitae
    }

    void Register() override
    {
        OnHit += SpellHitFn(sanguinith_blood_strike::HandleHit);
    }
};

class sanguinith_rend : public SpellScript
{
    PrepareSpellScript(sanguinith_rend);

    void HandleHit()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target || target == caster)
            return;

        Unit::DealDamage(caster, target, uint32(caster->GetTotalAttackPowerValue(BASE_ATTACK) * 0.5f), nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, GetSpellInfo());
        s_bleeds[target->GetGUID()] = { caster->GetGUID(), 12000 }; // 12s bleed

        if (Player* plr = caster->ToPlayer())
            plr->ModifyPower(POWER_MANA, 5); // +Vitae
    }

    void Register() override
    {
        OnHit += SpellHitFn(sanguinith_rend::HandleHit);
    }
};

class sanguinith_blood_boil : public SpellScript
{
    PrepareSpellScript(sanguinith_blood_boil);

    void HandleHit()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        std::list<Unit*> targets;
        Acore::AnyUnfriendlyUnitInObjectRangeCheck check(caster, caster, 10.0f);
        Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(caster, targets, check);
        Cell::VisitObjects(caster, searcher, 10.0f);

        uint32 dmg = uint32(caster->GetTotalAttackPowerValue(BASE_ATTACK) * 0.4f);
        for (Unit* t : targets)
            Unit::DealDamage(caster, t, dmg, nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_SHADOW, GetSpellInfo());

        if (Player* plr = caster->ToPlayer())
            plr->ModifyPower(POWER_MANA, 15); // +Vitae
    }

    void Register() override
    {
        OnHit += SpellHitFn(sanguinith_blood_boil::HandleHit);
    }
};

// ---------------------------------------------------------------------------
// Vampiric Thrall AI
// ---------------------------------------------------------------------------
class npc_sanguinith_thrall : public CreatureAI
{
public:
    npc_sanguinith_thrall(Creature* creature) : CreatureAI(creature) { }

    void JustSummoned(Creature* summoner) override
    {
        if (Player* owner = summoner->ToPlayer())
        {
            me->SetOwnerGUID(owner->GetGUID());
            me->SetFaction(owner->GetFaction());
        }
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        if (!me->IsAlive())
            return;

        Player* owner = me->GetOwner() ? me->GetOwner()->ToPlayer() : nullptr;
        if (!owner || !owner->IsInWorld())
        {
            if (TempSummon* summon = me->ToTempSummon())
                summon->UnSummon();
            return;
        }

        if (Unit* victim = owner->GetVictim())
        {
            if (me->GetVictim() != victim)
                me->Attack(victim, true);

            if (me->GetDistance(victim) > 5.0f)
                me->GetMotionMaster()->MoveChase(victim);
            else if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
                me->GetMotionMaster()->MoveChase(victim);

            DoMeleeAttackIfReady();
            return;
        }

        if (me->GetDistance(owner) > 8.0f)
            me->GetMotionMaster()->MoveFollow(owner, 2.0f, M_PI / 2.0f);
        else if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
            me->GetMotionMaster()->Clear();
    }
};

void AddSC_sanguinith()
{
    // Party management + login cosmetics only. The kit spell scripts and the
    // thrall AI are disabled - they caused repeated worldserver crashes.
    new sanguinith_player_script();
}
