/*
 * Black Legion Loot Roach - a passive pet that follows its owner and collects
 * loot from nearby corpses directly into the owner's bags.
 *
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright
 * information. Licensed under the GNU General Public License v2.
 */

#include "Creature.h"
#include "CreatureAI.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "LootMgr.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SpellMgr.h"

#include <algorithm>

class npc_black_legion_roach : public CreatureAI
{
public:
    npc_black_legion_roach(Creature* creature)
        : CreatureAI(creature)
        , m_lootTimer(0)
        , m_following(false)
        , m_sampleTimer(0)
        , m_healTimer(0)
        , m_lastOwnerHealth(0)
        , m_damageWindow{}
        , m_damageIdx(0)
        , m_damageTotal(0)
    { }

    void JustSummoned(Creature* summoner) override
    {
        if (Player* owner = summoner->ToPlayer())
        {
            // One roach per owner - despawn any existing roach they summoned.
            std::list<Creature*> roaches;
            owner->GetCreatureListWithEntryInGrid(roaches, 91019, 200.0f);
            for (Creature* other : roaches)
            {
                if (other != me && other->IsSummon() && other->ToTempSummon()->GetSummonerGUID() == owner->GetGUID())
                {
                    other->ToTempSummon()->UnSummon();
                    break;
                }
            }

            me->SetOwnerGUID(owner->GetGUID());
            me->SetFaction(owner->GetFaction());
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!me->IsAlive())
            return;

        Player* owner = me->GetOwner() ? me->GetOwner()->ToPlayer() : nullptr;
        if (!owner || !owner->IsInWorld() || owner->IsAlive() == false)
        {
            if (TempSummon* summon = me->ToTempSummon())
                summon->UnSummon();
            return;
        }

        // Follow the owner when they walk away, idle otherwise.
        float const dist = me->GetDistance(owner);
        if (dist > 12.0f)
        {
            if (!m_following)
            {
                me->GetMotionMaster()->MoveFollow(owner, 2.0f, M_PI / 2.0f);
                m_following = true;
            }
            else if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
            {
                // Re-apply follow if the generator was broken by collision or displacement.
                me->GetMotionMaster()->MoveFollow(owner, 2.0f, M_PI / 2.0f);
            }
        }
        else if (m_following)
        {
            me->GetMotionMaster()->Clear();
            m_following = false;
        }

        // Loot corpses nearby.
        m_lootTimer += diff;
        if (m_lootTimer >= 500)
        {
            m_lootTimer = 0;
            TryLootCorpses(owner);
        }

        // Adaptive healing: sense the owner's damage intake and heal accordingly.
        m_sampleTimer += diff;
        if (m_sampleTimer >= HEAL_SAMPLE_INTERVAL)
        {
            m_sampleTimer = 0;
            SampleOwnerHealth(owner);
        }

        m_healTimer += diff;
        if (m_healTimer >= HEAL_INTERVAL)
        {
            m_healTimer = 0;
            TryHealOwner(owner);
        }
    }

private:
    // ------------------------------------------------------------------
    // Adaptive healing
    // ------------------------------------------------------------------

    // Sample the owner's health each second and keep a rolling window of
    // damage taken so we know their real damage intake rate.
    void SampleOwnerHealth(Player* owner)
    {
        uint32 const cur = owner->GetHealth();
        uint32 const dmg = m_lastOwnerHealth > cur ? m_lastOwnerHealth - cur : 0;

        m_damageTotal -= m_damageWindow[m_damageIdx];
        m_damageWindow[m_damageIdx] = dmg;
        m_damageTotal += dmg;
        m_damageIdx = (m_damageIdx + 1) % HEAL_WINDOW_SIZE;

        m_lastOwnerHealth = cur;
    }

    void TryHealOwner(Player* owner)
    {
        if (!owner->IsInCombat())
            return;

        uint32 const maxHealth = owner->GetMaxHealth();
        uint32 const cur = owner->GetHealth();
        if (cur >= maxHealth)
            return;

        uint32 const missing = maxHealth - cur;
        float const missingPct = float(missing) / float(maxHealth);
        float const dmgPerSec = float(m_damageTotal) / float(HEAL_WINDOW_SIZE);

        // Base heal (the guaranteed minimum, just in case).
        uint32 heal = owner->CountPctFromMaxHealth(HEAL_BASE_PCT);

        // Adaptive part: keep pace with the observed damage rate.
        heal += uint32(dmgPerSec * HEAL_RATE_FACTOR);

        // Emergency bonus when the owner is getting low.
        if (missingPct > HEAL_LOW_HP_THRESHOLD)
            heal += owner->CountPctFromMaxHealth(HEAL_LOW_HP_BONUS_PCT);

        // Never overheal, and cap per tick to avoid immortality.
        heal = std::min(heal, missing);
        heal = std::min(heal, owner->CountPctFromMaxHealth(HEAL_MAX_PCT));
        if (!heal)
            return;

        // A real heal spell is required - the heal log dereferences the spell
        // info, so a null spell would crash the world server.
        SpellInfo const* healSpell = sSpellMgr->GetSpellInfo(2061); // Flash Heal
        if (!healSpell)
            return;

        HealInfo healInfo(me, owner, heal, healSpell, SPELL_SCHOOL_MASK_NORMAL);
        owner->HealBySpell(healInfo);
    }

    // ------------------------------------------------------------------
    // Looting
    // ------------------------------------------------------------------
    void TryLootCorpses(Player* owner)
    {
        std::list<Creature*> corpses;
        Acore::AllWorldObjectsInRange check(me, 15.0f);
        Acore::CreatureListSearcher<Acore::AllWorldObjectsInRange> searcher(me, corpses, check);
        Cell::VisitObjects(me, searcher, 15.0f);

        for (Creature* corpse : corpses)
        {
            if (!corpse || corpse == me)
                continue;
            if (corpse->IsAlive())
                continue;

            // Only loot corpses we have rights to: owner's own taps, or taps by
            // a member of the owner's raid/party.
            if (Player* recipient = corpse->GetLootRecipient())
                if (recipient != owner && !recipient->IsInSameRaidWith(owner))
                    continue;

            // Only corpses that still hold loot (or gold) are worth touching.
            Loot& loot = corpse->loot;
            if (loot.empty() && loot.gold == 0)
                continue;

            bool took = false;

            // Items
            for (LootItem& item : loot.items)
            {
                if (item.is_looted || item.itemid == 0)
                    continue;

                uint32 const count = item.count ? item.count : 1;

                ItemPosCountVec dest;
                InventoryResult msg = owner->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item.itemid, count);
                if (msg != EQUIP_ERR_OK)
                {
                    owner->SendEquipError(msg, nullptr, nullptr);
                    break;
                }

                if (Item* stored = owner->StoreNewItem(dest, item.itemid, true, item.randomPropertyId))
                {
                    owner->SendNewItem(stored, count, true, false);
                    item.is_looted = true;
                    loot.unlootedCount = loot.unlootedCount > 0 ? loot.unlootedCount - 1 : 0;
                    took = true;
                }
            }

            // Gold
            if (loot.gold > 0)
            {
                owner->ModifyMoney(loot.gold);
                loot.gold = 0;
                took = true;
            }

            if (took)
                corpse->RemoveCorpse();
        }
    }

    // ------------------------------------------------------------------
    // Adaptive heal tuning
    // ------------------------------------------------------------------
    static constexpr uint32 HEAL_SAMPLE_INTERVAL = 1000;    // damage sampling cadence (ms)
    static constexpr uint32 HEAL_INTERVAL        = 3000;    // heal tick cadence (ms)
    static constexpr uint32 HEAL_WINDOW_SIZE     = 6;       // rolling damage window (seconds)
    static constexpr int32  HEAL_BASE_PCT        = 4;       // guaranteed base heal (% of max hp)
    static constexpr float  HEAL_RATE_FACTOR     = 1.2f;    // heal = 120% of observed damage rate
    static constexpr float  HEAL_LOW_HP_THRESHOLD = 0.65f;  // emergency bonus when missing > 65%
    static constexpr int32  HEAL_LOW_HP_BONUS_PCT = 10;     // emergency bonus (% of max hp)
    static constexpr int32  HEAL_MAX_PCT         = 40;      // per-tick heal cap (% of max hp)

    uint32 m_lootTimer;
    bool m_following;
    uint32 m_sampleTimer;
    uint32 m_healTimer;
    uint32 m_lastOwnerHealth;
    uint32 m_damageWindow[HEAL_WINDOW_SIZE];
    uint32 m_damageIdx;
    uint32 m_damageTotal;
};

void AddSC_black_legion_roach()
{
    RegisterCreatureAI(npc_black_legion_roach);
}
