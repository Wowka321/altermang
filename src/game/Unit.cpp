/*
 * Copyright (C) 2005-2010 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Common.h"
#include "Log.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "World.h"
#include "ObjectMgr.h"
#include "ObjectDefines.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "QuestDef.h"
#include "Player.h"
#include "Creature.h"
#include "Spell.h"
#include "Group.h"
#include "SpellAuras.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "CreatureAI.h"
#include "TemporarySummon.h"
#include "Formulas.h"
#include "Pet.h"
#include "Util.h"
#include "Totem.h"
#include "BattleGround.h"
#include "InstanceSaveMgr.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Path.h"
#include "Traveller.h"
#include "VMapFactory.h"
#include "MovementGenerator.h"

#include <math.h>

float baseMoveSpeed[MAX_MOVE_TYPE] =
{
    2.5f,                                                   // MOVE_WALK
    7.0f,                                                   // MOVE_RUN
    1.25f,                                                  // MOVE_RUN_BACK
    4.722222f,                                              // MOVE_SWIM
    4.5f,                                                   // MOVE_SWIM_BACK
    3.141594f,                                              // MOVE_TURN_RATE
    7.0f,                                                   // MOVE_FLIGHT
    4.5f,                                                   // MOVE_FLIGHT_BACK
    3.14f                                                   // MOVE_PITCH_RATE
};

// Used for prepare can/can`t trigger aura
static bool InitTriggerAuraData();
// Define can trigger auras
static bool isTriggerAura[TOTAL_AURAS];
// Define can`t trigger auras (need for disable second trigger)
static bool isNonTriggerAura[TOTAL_AURAS];
// Prepare lists
static bool procPrepared = InitTriggerAuraData();

MovementInfo::MovementInfo(WorldPacket &data)
{
    // Init fields
    moveFlags = MOVEFLAG_NONE;
    moveFlags2 = MOVEFLAG2_NONE;
    time = 0;
    t_guid = 0;
    t_time = 0;
    t_seat = -1;
    t_time2 = 0;
    s_pitch = 0.0f;
    fallTime = 0;
    j_velocity = j_sinAngle = j_cosAngle = j_xyspeed = 0.0f;
    u_unk1 = 0.0f;

    // Read actual data
    Read(data);
}

void MovementInfo::Read(ByteBuffer &data)
{
    data >> moveFlags;
    data >> moveFlags2;
    data >> time;
    data >> pos.x;
    data >> pos.y;
    data >> pos.z;
    data >> pos.o;

    if(HasMovementFlag(MOVEFLAG_ONTRANSPORT))
    {
        if(!data.readPackGUID(t_guid))
            return;

        data >> t_pos.x;
        data >> t_pos.y;
        data >> t_pos.z;
        data >> t_pos.o;
        data >> t_time;
        data >> t_seat;

        if(moveFlags2 & MOVEFLAG2_UNK1)
            data >> t_time2;
    }

    if((HasMovementFlag(MovementFlags(MOVEFLAG_SWIMMING | MOVEFLAG_FLYING))) || (moveFlags2 & MOVEFLAG2_ALLOW_PITCHING))
    {
        data >> s_pitch;
    }

    data >> fallTime;

    if(HasMovementFlag(MOVEFLAG_FALLING))
    {
        data >> j_velocity;
        data >> j_sinAngle;
        data >> j_cosAngle;
        data >> j_xyspeed;
    }

    if(HasMovementFlag(MOVEFLAG_SPLINE_ELEVATION))
    {
        data >> u_unk1;
    }
}

void MovementInfo::Write(ByteBuffer &data)
{
    data << moveFlags;
    data << moveFlags2;
    data << time;
    data << pos.x;
    data << pos.y;
    data << pos.z;
    data << pos.o;

    if(HasMovementFlag(MOVEFLAG_ONTRANSPORT))
    {
        data.appendPackGUID(t_guid);

        data << t_pos.x;
        data << t_pos.y;
        data << t_pos.z;
        data << t_pos.o;
        data << t_time;
        data << t_seat;

        if(moveFlags2 & MOVEFLAG2_UNK1)
            data << t_time2;
    }

    if((HasMovementFlag(MovementFlags(MOVEFLAG_SWIMMING | MOVEFLAG_FLYING))) || (moveFlags2 & MOVEFLAG2_ALLOW_PITCHING))
    {
        data << s_pitch;
    }

    data << fallTime;

    if(HasMovementFlag(MOVEFLAG_FALLING))
    {
        data << j_velocity;
        data << j_sinAngle;
        data << j_cosAngle;
        data << j_xyspeed;
    }

    if(HasMovementFlag(MOVEFLAG_SPLINE_ELEVATION))
    {
        data << u_unk1;
    }
}

Unit::Unit()
: WorldObject(), i_motionMaster(this), m_ThreatManager(this), m_HostileRefManager(this)
{
    m_objectType |= TYPEMASK_UNIT;
    m_objectTypeId = TYPEID_UNIT;

    m_updateFlag = (UPDATEFLAG_HIGHGUID | UPDATEFLAG_LIVING | UPDATEFLAG_HAS_POSITION);

    m_attackTimer[BASE_ATTACK]   = 0;
    m_attackTimer[OFF_ATTACK]    = 0;
    m_attackTimer[RANGED_ATTACK] = 0;
    m_modAttackSpeedPct[BASE_ATTACK] = 1.0f;
    m_modAttackSpeedPct[OFF_ATTACK] = 1.0f;
    m_modAttackSpeedPct[RANGED_ATTACK] = 1.0f;

    m_extraAttacks = 0;

    m_state = 0;
    m_form = FORM_NONE;
    m_deathState = ALIVE;

    for (uint32 i = 0; i < CURRENT_MAX_SPELL; ++i)
        m_currentSpells[i] = NULL;

    m_castCounter = 0;

    m_addDmgOnce = 0;

    for(int i = 0; i < MAX_TOTEM; ++i)
        m_TotemSlot[i] = 0;

    m_ObjectSlot[0] = m_ObjectSlot[1] = m_ObjectSlot[2] = m_ObjectSlot[3] = 0;
    //m_Aura = NULL;
    //m_AurasCheck = 2000;
    //m_removeAuraTimer = 4;
    m_AurasUpdateIterator = m_Auras.end();
    m_AuraFlags = 0;

    m_Visibility = VISIBILITY_ON;

    m_detectInvisibilityMask = 0;
    m_invisibilityMask = 0;
    m_transform = 0;
    m_ShapeShiftFormSpellId = 0;
    m_canModifyStats = false;

    for (int i = 0; i < MAX_SPELL_IMMUNITY; ++i)
        m_spellImmune[i].clear();
    for (int i = 0; i < UNIT_MOD_END; ++i)
    {
        m_auraModifiersGroup[i][BASE_VALUE] = 0.0f;
        m_auraModifiersGroup[i][BASE_PCT] = 1.0f;
        m_auraModifiersGroup[i][TOTAL_VALUE] = 0.0f;
        m_auraModifiersGroup[i][TOTAL_PCT] = 1.0f;
    }
                                                            // implement 50% base damage from offhand
    m_auraModifiersGroup[UNIT_MOD_DAMAGE_OFFHAND][TOTAL_PCT] = 0.5f;

    for (int i = 0; i < MAX_ATTACK; ++i)
    {
        m_weaponDamage[i][MINDAMAGE] = BASE_MINDAMAGE;
        m_weaponDamage[i][MAXDAMAGE] = BASE_MAXDAMAGE;
    }
    for (int i = 0; i < MAX_STATS; ++i)
        m_createStats[i] = 0.0f;

    m_attacking = NULL;
    m_modMeleeHitChance = 0.0f;
    m_modRangedHitChance = 0.0f;
    m_modSpellHitChance = 0.0f;
    m_baseSpellCritChance = 5;

    m_CombatTimer = 0;
    m_lastManaUseTimer = 0;

    //m_victimThreat = 0.0f;
    for (int i = 0; i < MAX_SPELL_SCHOOL; ++i)
        m_threatModifier[i] = 1.0f;
    m_isSorted = true;
    for (int i = 0; i < MAX_MOVE_TYPE; ++i)
        m_speed_rate[i] = 1.0f;

    m_charmInfo = NULL;

    // remove aurastates allowing special moves
    for(int i=0; i < MAX_REACTIVE; ++i)
        m_reactiveTimer[i] = 0;
}

Unit::~Unit()
{
    // set current spells as deletable
    for (uint32 i = 0; i < CURRENT_MAX_SPELL; ++i)
    {
        if (m_currentSpells[i])
        {
            m_currentSpells[i]->SetReferencedFromCurrent(false);
            m_currentSpells[i] = NULL;
        }
    }

    if (m_charmInfo)
        delete m_charmInfo;

    // those should be already removed at "RemoveFromWorld()" call
    assert(m_gameObj.size() == 0);
    assert(m_dynObjGUIDs.size() == 0);
    assert(m_deletedAuras.size() == 0);
}

void Unit::Update( uint32 p_time )
{
    if(!IsInWorld())
        return;

    /*if(p_time > m_AurasCheck)
    {
    m_AurasCheck = 2000;
    _UpdateAura();
    }else
    m_AurasCheck -= p_time;*/

    // WARNING! Order of execution here is important, do not change.
    // Spells must be processed with event system BEFORE they go to _UpdateSpells.
    // Or else we may have some SPELL_STATE_FINISHED spells stalled in pointers, that is bad.
    m_Events.Update( p_time );
    _UpdateSpells( p_time );

    CleanupDeletedAuras();

    if (m_lastManaUseTimer)
    {
        if (p_time >= m_lastManaUseTimer)
            m_lastManaUseTimer = 0;
        else
            m_lastManaUseTimer -= p_time;
    }

    if (CanHaveThreatList())
        getThreatManager().UpdateForClient(p_time);

    // update combat timer only for players and pets
    if (isInCombat() && (GetTypeId() == TYPEID_PLAYER || ((Creature*)this)->isPet() || ((Creature*)this)->isCharmed()))
    {
        // Check UNIT_STAT_MELEE_ATTACKING or UNIT_STAT_CHASE (without UNIT_STAT_FOLLOW in this case) so pets can reach far away
        // targets without stopping half way there and running off.
        // These flags are reset after target dies or another command is given.
        if (m_HostileRefManager.isEmpty())
        {
            // m_CombatTimer set at aura start and it will be freeze until aura removing
            if (m_CombatTimer <= p_time)
                CombatStop();
            else
                m_CombatTimer -= p_time;
        }
    }

    if (uint32 base_att = getAttackTimer(BASE_ATTACK))
    {
        setAttackTimer(BASE_ATTACK, (p_time >= base_att ? 0 : base_att - p_time) );
    }

    // update abilities available only for fraction of time
    UpdateReactives( p_time );

    ModifyAuraState(AURA_STATE_HEALTHLESS_20_PERCENT, GetHealth() < GetMaxHealth()*0.20f);
    ModifyAuraState(AURA_STATE_HEALTHLESS_35_PERCENT, GetHealth() < GetMaxHealth()*0.35f);
    ModifyAuraState(AURA_STATE_HEALTH_ABOVE_75_PERCENT, GetHealth() > GetMaxHealth()*0.75f);

    i_motionMaster.UpdateMotion(p_time);
}

bool Unit::haveOffhandWeapon() const
{
    if(GetTypeId() == TYPEID_PLAYER)
        return ((Player*)this)->GetWeaponForAttack(OFF_ATTACK,true,true);
    else
        return false;
}

void Unit::SendMonsterMove(float NewPosX, float NewPosY, float NewPosZ, SplineType type, SplineFlags flags, uint32 Time, Player* player)
{
    float moveTime = (float)Time;

    WorldPacket data( SMSG_MONSTER_MOVE, (41 + GetPackGUID().size()) );
    data.append(GetPackGUID());
    data << uint8(0);                                       // new in 3.1
    data << GetPositionX() << GetPositionY() << GetPositionZ();
    data << uint32(getMSTime());

    data << uint8(type);                                    // unknown
    switch(type)
    {
        case SPLINETYPE_NORMAL:                             // normal packet
            break;
        case SPLINETYPE_STOP:                               // stop packet (raw pos?)
            SendMessageToSet( &data, true );
            return;
        case SPLINETYPE_FACINGSPOT:                         // facing spot, not used currently
            data << float(0);
            data << float(0);
            data << float(0);
            break;
        case SPLINETYPE_FACINGTARGET:                       // not used currently
            data << uint64(0);                              // probably target guid (facing target?)
            break;
        case SPLINETYPE_FACINGANGLE:                        // not used currently
            data << float(0);                               // facing angle
            break;
    }

    data << uint32(flags);

    if(flags & SPLINEFLAG_WALKMODE)
        moveTime *= 1.05f;

    data << uint32(moveTime);                               // Time in between points
    data << uint32(1);                                      // 1 single waypoint
    data << NewPosX << NewPosY << NewPosZ;                  // the single waypoint Point B

    if(player)
        player->GetSession()->SendPacket(&data);
    else
        SendMessageToSet( &data, true );
}

void Unit::SendMonsterMoveByPath(Path const& path, uint32 start, uint32 end, SplineFlags flags)
{
    uint32 traveltime = uint32(path.GetTotalLength(start, end) * 32);

    uint32 pathSize = end - start;

    WorldPacket data( SMSG_MONSTER_MOVE, (GetPackGUID().size()+1+4+4+4+4+1+4+4+4+pathSize*4*3) );
    data.append(GetPackGUID());
    data << uint8(0);
    data << GetPositionX();
    data << GetPositionY();
    data << GetPositionZ();
    data << uint32(getMSTime());
    data << uint8(SPLINETYPE_NORMAL);
    data << uint32(flags);
    data << uint32(traveltime);
    data << uint32(pathSize);
    data.append((char*)path.GetNodes(start), pathSize * 4 * 3);
    SendMessageToSet(&data, true);
}

void Unit::SendMonsterMoveWithSpeed(float x, float y, float z, uint32 transitTime, Player* player)
{
    if (!transitTime)
    {
        if(GetTypeId()==TYPEID_PLAYER)
        {
            Traveller<Player> traveller(*(Player*)this);
            transitTime = traveller.GetTotalTrevelTimeTo(x, y, z);
        }
        else
        {
            Traveller<Creature> traveller(*(Creature*)this);
            transitTime = traveller.GetTotalTrevelTimeTo(x, y, z);
        }
    }
    //float orientation = (float)atan2((double)dy, (double)dx);
    SplineFlags flags = GetTypeId() == TYPEID_PLAYER ? SPLINEFLAG_WALKMODE : ((Creature*)this)->GetSplineFlags();
    SendMonsterMove(x, y, z, SPLINETYPE_NORMAL, flags, transitTime, player);
}

void Unit::BuildHeartBeatMsg(WorldPacket *data) const
{
    MovementFlags move_flags = GetTypeId()==TYPEID_PLAYER
        ? ((Player const*)this)->m_movementInfo.GetMovementFlags()
        : MOVEFLAG_NONE;

    data->Initialize(MSG_MOVE_HEARTBEAT, 32);
    data->append(GetPackGUID());
    *data << uint32(move_flags);                            // movement flags
    *data << uint16(0);                                     // 2.3.0
    *data << uint32(getMSTime());                           // time
    *data << float(GetPositionX());
    *data << float(GetPositionY());
    *data << float(GetPositionZ());
    *data << float(GetOrientation());
    *data << uint32(0);
}

void Unit::resetAttackTimer(WeaponAttackType type)
{
    m_attackTimer[type] = uint32(GetAttackTime(type) * m_modAttackSpeedPct[type]);
}

bool Unit::canReachWithAttack(Unit *pVictim) const
{
    assert(pVictim);
    float reach = GetFloatValue(UNIT_FIELD_COMBATREACH);
    if( reach <= 0.0f )
        reach = 1.0f;
    return IsWithinDistInMap(pVictim, reach);
}

void Unit::RemoveSpellsCausingAura(AuraType auraType)
{
    if (auraType >= TOTAL_AURAS) return;
    AuraList::const_iterator iter, next;
    for (iter = m_modAuras[auraType].begin(); iter != m_modAuras[auraType].end(); iter = next)
    {
        next = iter;
        ++next;

        if (*iter)
        {
            RemoveAurasDueToSpell((*iter)->GetId());
            if (!m_modAuras[auraType].empty())
                next = m_modAuras[auraType].begin();
            else
                return;
        }
    }
}

bool Unit::HasAuraType(AuraType auraType) const
{
    return (!m_modAuras[auraType].empty());
}

/* Called by DealDamage for auras that have a chance to be dispelled on damage taken. */
void Unit::RemoveSpellbyDamageTaken(AuraType auraType, uint32 damage)
{
    if(!HasAuraType(auraType))
        return;

    // The chance to dispel an aura depends on the damage taken with respect to the casters level.
    uint32 max_dmg = getLevel() > 8 ? 25 * getLevel() - 150 : 50;
    float chance = float(damage) / max_dmg * 100.0f;
    if (roll_chance_f(chance))
        RemoveSpellsCausingAura(auraType);
}

void Unit::DealDamageMods(Unit *pVictim, uint32 &damage, uint32* absorb)
{
    if (!pVictim->isAlive() || pVictim->isInFlight() || pVictim->GetTypeId() == TYPEID_UNIT && ((Creature*)pVictim)->IsInEvadeMode())
    {
        if(absorb)
            absorb += damage;
        damage = 0;
        return;
    }

    //You don't lose health from damage taken from another player while in a sanctuary
    //You still see it in the combat log though
    if(pVictim != this && GetTypeId() == TYPEID_PLAYER && pVictim->GetTypeId() == TYPEID_PLAYER)
    {
        const AreaTableEntry *area = GetAreaEntryByAreaID(pVictim->GetAreaId());
        if(area && area->flags & AREA_FLAG_SANCTUARY)       //sanctuary
        {
            if(absorb)
                absorb += damage;
            damage = 0;
        }
    }

    uint32 originalDamage = damage;

    //Script Event damage Deal
    if( GetTypeId()== TYPEID_UNIT && ((Creature *)this)->AI())
        ((Creature *)this)->AI()->DamageDeal(pVictim, damage);
    //Script Event damage taken
    if( pVictim->GetTypeId()== TYPEID_UNIT && ((Creature *)pVictim)->AI() )
        ((Creature *)pVictim)->AI()->DamageTaken(this, damage);

    if(absorb && originalDamage > damage)
        absorb += (originalDamage - damage);
}

uint32 Unit::DealDamage(Unit *pVictim, uint32 damage, CleanDamage const* cleanDamage, DamageEffectType damagetype, SpellSchoolMask damageSchoolMask, SpellEntry const *spellProto, bool durabilityLoss)
{
    // remove affects from victim (including from 0 damage and DoTs)
    if(pVictim != this)
        pVictim->RemoveSpellsCausingAura(SPELL_AURA_MOD_STEALTH);

    // remove affects from attacker at any non-DoT damage (including 0 damage)
    if( damagetype != DOT)
    {
        RemoveSpellsCausingAura(SPELL_AURA_MOD_STEALTH);
        RemoveSpellsCausingAura(SPELL_AURA_FEIGN_DEATH);

        if(pVictim != this)
            RemoveSpellsCausingAura(SPELL_AURA_MOD_INVISIBILITY);

        if(pVictim->GetTypeId() == TYPEID_PLAYER && !pVictim->IsStandState() && !pVictim->hasUnitState(UNIT_STAT_STUNNED))
            pVictim->SetStandState(UNIT_STAND_STATE_STAND);
    }

    if(!damage)
    {
        // Rage from physical damage received .
        if(cleanDamage && cleanDamage->damage && (damageSchoolMask & SPELL_SCHOOL_MASK_NORMAL) && pVictim->GetTypeId() == TYPEID_PLAYER && (pVictim->getPowerType() == POWER_RAGE))
            ((Player*)pVictim)->RewardRage(cleanDamage->damage, 0, false);

        return 0;
    }
    if (!spellProto || !IsSpellHaveAura(spellProto,SPELL_AURA_MOD_FEAR))
        pVictim->RemoveSpellbyDamageTaken(SPELL_AURA_MOD_FEAR, damage);
    // root type spells do not dispel the root effect
    if (!spellProto || !(spellProto->Mechanic == MECHANIC_ROOT || IsSpellHaveAura(spellProto,SPELL_AURA_MOD_ROOT)))
        pVictim->RemoveSpellbyDamageTaken(SPELL_AURA_MOD_ROOT, damage);

    // no xp,health if type 8 /critters/
    if(pVictim->GetTypeId() != TYPEID_PLAYER && pVictim->GetCreatureType() == CREATURE_TYPE_CRITTER)
    {
        pVictim->setDeathState(JUST_DIED);
        pVictim->SetHealth(0);

        // allow loot only if has loot_id in creature_template
        CreatureInfo const* cInfo = ((Creature*)pVictim)->GetCreatureInfo();
        if(cInfo && cInfo->lootid)
            pVictim->SetUInt32Value(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE);

        // some critters required for quests (need normal entry instead possible heroic in any cases)
        if(GetTypeId() == TYPEID_PLAYER)
            if(CreatureInfo const* normalInfo = ObjectMgr::GetCreatureTemplate(pVictim->GetEntry()))
                ((Player*)this)->KilledMonster(normalInfo,pVictim->GetGUID());

        return damage;
    }

    DEBUG_LOG("DealDamageStart");

    uint32 health = pVictim->GetHealth();
    sLog.outDetail("deal dmg:%d to health:%d ",damage,health);

    // duel ends when player has 1 or less hp
    bool duel_hasEnded = false;
    if(pVictim->GetTypeId() == TYPEID_PLAYER && ((Player*)pVictim)->duel && damage >= (health-1))
    {
        // prevent kill only if killed in duel and killed by opponent or opponent controlled creature
        if(((Player*)pVictim)->duel->opponent==this || ((Player*)pVictim)->duel->opponent->GetGUID() == GetOwnerGUID())
            damage = health-1;

        duel_hasEnded = true;
    }
    //Get in CombatState
    if(pVictim != this && damagetype != DOT)
    {
        SetInCombatWith(pVictim);
        pVictim->SetInCombatWith(this);

        if(Player* attackedPlayer = pVictim->GetCharmerOrOwnerPlayerOrPlayerItself())
            SetContestedPvP(attackedPlayer);
    }

    // Rage from Damage made (only from direct weapon damage)
    if( cleanDamage && damagetype==DIRECT_DAMAGE && this != pVictim && GetTypeId() == TYPEID_PLAYER && (getPowerType() == POWER_RAGE))
    {
        uint32 weaponSpeedHitFactor;

        switch(cleanDamage->attackType)
        {
            case BASE_ATTACK:
            {
                if(cleanDamage->hitOutCome == MELEE_HIT_CRIT)
                    weaponSpeedHitFactor = uint32(GetAttackTime(cleanDamage->attackType)/1000.0f * 7);
                else
                    weaponSpeedHitFactor = uint32(GetAttackTime(cleanDamage->attackType)/1000.0f * 3.5f);

                ((Player*)this)->RewardRage(damage, weaponSpeedHitFactor, true);

                break;
            }
            case OFF_ATTACK:
            {
                if(cleanDamage->hitOutCome == MELEE_HIT_CRIT)
                    weaponSpeedHitFactor = uint32(GetAttackTime(cleanDamage->attackType)/1000.0f * 3.5f);
                else
                    weaponSpeedHitFactor = uint32(GetAttackTime(cleanDamage->attackType)/1000.0f * 1.75f);

                ((Player*)this)->RewardRage(damage, weaponSpeedHitFactor, true);

                break;
            }
            case RANGED_ATTACK:
                break;
        }
    }

    if (GetTypeId() == TYPEID_PLAYER && this != pVictim)
    {
        Player *killer = ((Player*)this);

        // in bg, count dmg if victim is also a player
        if (pVictim->GetTypeId()==TYPEID_PLAYER)
        {
            if (BattleGround *bg = killer->GetBattleGround())
            {
                // FIXME: kept by compatibility. don't know in BG if the restriction apply.
                bg->UpdatePlayerScore(killer, SCORE_DAMAGE_DONE, damage);
            }
        }

        killer->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_DAMAGE_DONE, damage, 0, pVictim);
        killer->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HIT_DEALT, damage);
    }

    if (pVictim->GetTypeId() == TYPEID_PLAYER)
        ((Player*)pVictim)->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HIT_RECEIVED, damage);

    if (pVictim->GetTypeId() == TYPEID_UNIT && !((Creature*)pVictim)->isPet() && !((Creature*)pVictim)->hasLootRecipient())
        ((Creature*)pVictim)->SetLootRecipient(this);

    if (health <= damage)
    {
        DEBUG_LOG("DealDamage: victim just died");

        // find player: owner of controlled `this` or `this` itself maybe
        Player *player = GetCharmerOrOwnerPlayerOrPlayerItself();
        if(pVictim->GetTypeId() == TYPEID_UNIT && ((Creature*)pVictim)->GetLootRecipient())
            player = ((Creature*)pVictim)->GetLootRecipient();

        if (pVictim->GetTypeId() == TYPEID_PLAYER)
        {
            ((Player*)pVictim)->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_TOTAL_DAMAGE_RECEIVED, health);
            if (player)
                player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_SPECIAL_PVP_KILL,1,0,pVictim);
        }

        // Reward player, his pets, and group/raid members
        // call kill spell proc event (before real die and combat stop to triggering auras removed at death/combat stop)
        if(player && player!=pVictim)
        {
            player->RewardPlayerAndGroupAtKill(pVictim);
            player->ProcDamageAndSpell(pVictim, PROC_FLAG_KILL, PROC_FLAG_KILLED, PROC_EX_NONE, 0);

            WorldPacket data(SMSG_PARTYKILLLOG, (8+8)); //send event PARTY_KILL
            data << uint64(player->GetGUID()); //player with killing blow
            data << uint64(pVictim->GetGUID()); //victim
            if (Group *group =  player->GetGroup())
                group->BroadcastPacket(&data, group->GetMemberGroup(player->GetGUID()));
            else
                player->SendDirectMessage(&data);
        }

        DEBUG_LOG("DealDamageAttackStop");

        // stop combat
        pVictim->CombatStop();
        pVictim->getHostileRefManager().deleteReferences();

        bool damageFromSpiritOfRedemtionTalent = spellProto && spellProto->Id == 27795;

        // if talent known but not triggered (check priest class for speedup check)
        Aura* spiritOfRedemtionTalentReady = NULL;
        if( !damageFromSpiritOfRedemtionTalent &&           // not called from SPELL_AURA_SPIRIT_OF_REDEMPTION
            pVictim->GetTypeId()==TYPEID_PLAYER && pVictim->getClass()==CLASS_PRIEST )
        {
            AuraList const& vDummyAuras = pVictim->GetAurasByType(SPELL_AURA_DUMMY);
            for(AuraList::const_iterator itr = vDummyAuras.begin(); itr != vDummyAuras.end(); ++itr)
            {
                if((*itr)->GetSpellProto()->SpellIconID==1654)
                {
                    spiritOfRedemtionTalentReady = *itr;
                    break;
                }
            }
        }

        DEBUG_LOG("SET JUST_DIED");
        if(!spiritOfRedemtionTalentReady)
            pVictim->setDeathState(JUST_DIED);

        DEBUG_LOG("DealDamageHealth1");

        if(spiritOfRedemtionTalentReady)
        {
            // save value before aura remove
            uint32 ressSpellId = pVictim->GetUInt32Value(PLAYER_SELF_RES_SPELL);
            if(!ressSpellId)
                ressSpellId = ((Player*)pVictim)->GetResurrectionSpellId();

            //Remove all expected to remove at death auras (most important negative case like DoT or periodic triggers)
            pVictim->RemoveAllAurasOnDeath();

            // restore for use at real death
            pVictim->SetUInt32Value(PLAYER_SELF_RES_SPELL,ressSpellId);

            // FORM_SPIRITOFREDEMPTION and related auras
            pVictim->CastSpell(pVictim,27827,true,NULL,spiritOfRedemtionTalentReady);
        }
        else
            pVictim->SetHealth(0);

        // remember victim PvP death for corpse type and corpse reclaim delay
        // at original death (not at SpiritOfRedemtionTalent timeout)
        if( pVictim->GetTypeId()==TYPEID_PLAYER && !damageFromSpiritOfRedemtionTalent )
            ((Player*)pVictim)->SetPvPDeath(player!=NULL);

        // Call KilledUnit for creatures
        if (GetTypeId() == TYPEID_UNIT && ((Creature*)this)->AI())
            ((Creature*)this)->AI()->KilledUnit(pVictim);

        // achievement stuff
        if (pVictim->GetTypeId() == TYPEID_PLAYER)
        {
            if (GetTypeId() == TYPEID_UNIT)
                ((Player*)pVictim)->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_KILLED_BY_CREATURE, GetEntry());
            else if(GetTypeId() == TYPEID_PLAYER && pVictim != this)
                ((Player*)pVictim)->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_KILLED_BY_PLAYER, 1, ((Player*)this)->GetTeam());
        }

        // 10% durability loss on death
        // clean InHateListOf
        if (pVictim->GetTypeId() == TYPEID_PLAYER)
        {
            // only if not player and not controlled by player pet. And not at BG
            if (durabilityLoss && !player && !((Player*)pVictim)->InBattleGround())
            {
                DEBUG_LOG("We are dead, loosing 10 percents durability");
                ((Player*)pVictim)->DurabilityLossAll(0.10f,false);
                // durability lost message
                WorldPacket data(SMSG_DURABILITY_DAMAGE_DEATH, 0);
                ((Player*)pVictim)->GetSession()->SendPacket(&data);
            }
        }
        else                                                // creature died
        {
            DEBUG_LOG("DealDamageNotPlayer");
            Creature *cVictim = (Creature*)pVictim;

            if(!cVictim->isPet())
            {
                cVictim->DeleteThreatList();
                // only lootable if it has loot or can drop gold
                if(cVictim->GetCreatureInfo()->lootid || cVictim->GetCreatureInfo()->maxgold > 0)
                    cVictim->SetUInt32Value(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE);
                else
                    cVictim->lootForBody = true;            // needed for skinning
            }
            // Call creature just died function
            if (cVictim->AI())
                cVictim->AI()->JustDied(this);

            if (cVictim->isTemporarySummon())
            {
                TemporarySummon* pSummon = (TemporarySummon*)cVictim;
                if (IS_CREATURE_GUID(pSummon->GetSummonerGUID()))
                    if(Creature* pSummoner = cVictim->GetMap()->GetCreature(pSummon->GetSummonerGUID()))
                        if (pSummoner->AI())
                            pSummoner->AI()->SummonedCreatureJustDied(cVictim);
            }


            // Dungeon specific stuff, only applies to players killing creatures
            if(cVictim->GetInstanceId())
            {
                Map *m = cVictim->GetMap();
                Player *creditedPlayer = GetCharmerOrOwnerPlayerOrPlayerItself();
                // TODO: do instance binding anyway if the charmer/owner is offline

                if(m->IsDungeon() && creditedPlayer)
                {
                    if (m->IsRaidOrHeroicDungeon())
                    {
                        if(cVictim->GetCreatureInfo()->flags_extra & CREATURE_FLAG_EXTRA_INSTANCE_BIND)
                            ((InstanceMap *)m)->PermBindAllPlayers(creditedPlayer);
                    }
                    else
                    {
                        // the reset time is set but not added to the scheduler
                        // until the players leave the instance
                        time_t resettime = cVictim->GetRespawnTimeEx() + 2 * HOUR;
                        if(InstanceSave *save = sInstanceSaveMgr.GetInstanceSave(cVictim->GetInstanceId()))
                            if(save->GetResetTime() < resettime) save->SetResetTime(resettime);
                    }
                }
            }
        }

        // last damage from non duel opponent or opponent controlled creature
        if(duel_hasEnded)
        {
            assert(pVictim->GetTypeId()==TYPEID_PLAYER);
            Player *he = (Player*)pVictim;

            assert(he->duel);

            he->duel->opponent->CombatStopWithPets(true);
            he->CombatStopWithPets(true);

            he->DuelComplete(DUEL_INTERUPTED);
        }

        // battleground things (do this at the end, so the death state flag will be properly set to handle in the bg->handlekill)
        if(pVictim->GetTypeId() == TYPEID_PLAYER && ((Player*)pVictim)->InBattleGround())
        {
            Player *killed = ((Player*)pVictim);
            if(BattleGround *bg = killed->GetBattleGround())
                if(player)
                    bg->HandleKillPlayer(killed, player);
        }
        else if(pVictim->GetTypeId() == TYPEID_UNIT)
        {
            if (player)
                if (BattleGround *bg = player->GetBattleGround())
                    bg->HandleKillUnit((Creature*)pVictim, player);
        }
    }
    else                                                    // if (health <= damage)
    {
        DEBUG_LOG("DealDamageAlive");

        if (pVictim->GetTypeId() == TYPEID_PLAYER)
            ((Player*)pVictim)->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_TOTAL_DAMAGE_RECEIVED, damage);

        pVictim->ModifyHealth(- (int32)damage);

        if(damagetype != DOT)
        {
            if(!getVictim())
            {
                // if not have main target then attack state with target (including AI call)
                //start melee attacks only after melee hit
                Attack(pVictim,(damagetype == DIRECT_DAMAGE));
            }

            // if damage pVictim call AI reaction
            if(pVictim->GetTypeId()==TYPEID_UNIT && ((Creature*)pVictim)->AI())
                ((Creature*)pVictim)->AI()->AttackedBy(this);
        }

        // polymorphed, hex and other negative transformed cases
        uint32 morphSpell = pVictim->getTransForm();
        if (morphSpell && !IsPositiveSpell(morphSpell))
        {
            if (SpellEntry const* morphEntry = sSpellStore.LookupEntry(morphSpell))
            {
                if (IsSpellHaveAura(morphEntry, SPELL_AURA_MOD_CONFUSE))
                    pVictim->RemoveAurasDueToSpell(morphSpell);
                else if (IsSpellHaveAura(morphEntry, SPELL_AURA_MOD_PACIFY_SILENCE))
                    pVictim->RemoveSpellbyDamageTaken(SPELL_AURA_MOD_PACIFY_SILENCE, damage);
            }
        }

        if(damagetype == DIRECT_DAMAGE || damagetype == SPELL_DIRECT_DAMAGE)
        {
            if (!spellProto || !(spellProto->AuraInterruptFlags&AURA_INTERRUPT_FLAG_DIRECT_DAMAGE))
                pVictim->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_DIRECT_DAMAGE);
        }
        if (pVictim->GetTypeId() != TYPEID_PLAYER)
        {
            if(spellProto && IsDamageToThreatSpell(spellProto))
                pVictim->AddThreat(this, float(damage*2), (cleanDamage && cleanDamage->hitOutCome == MELEE_HIT_CRIT), damageSchoolMask, spellProto);
            else
                pVictim->AddThreat(this, float(damage), (cleanDamage && cleanDamage->hitOutCome == MELEE_HIT_CRIT), damageSchoolMask, spellProto);
        }
        else                                                // victim is a player
        {
            // Rage from damage received
            if(this != pVictim && pVictim->getPowerType() == POWER_RAGE)
            {
                uint32 rage_damage = damage + (cleanDamage ? cleanDamage->damage : 0);
                ((Player*)pVictim)->RewardRage(rage_damage, 0, false);
            }

            // random durability for items (HIT TAKEN)
            if (roll_chance_f(sWorld.getConfig(CONFIG_FLOAT_RATE_DURABILITY_LOSS_DAMAGE)))
            {
                EquipmentSlots slot = EquipmentSlots(urand(0,EQUIPMENT_SLOT_END-1));
                ((Player*)pVictim)->DurabilityPointLossForEquipSlot(slot);
            }
        }

        if(GetTypeId()==TYPEID_PLAYER)
        {
            // random durability for items (HIT DONE)
            if (roll_chance_f(sWorld.getConfig(CONFIG_FLOAT_RATE_DURABILITY_LOSS_DAMAGE)))
            {
                EquipmentSlots slot = EquipmentSlots(urand(0,EQUIPMENT_SLOT_END-1));
                ((Player*)this)->DurabilityPointLossForEquipSlot(slot);
            }
        }

        // TODO: Store auras by interrupt flag to speed this up.
        AuraMap& vAuras = pVictim->GetAuras();
        for (AuraMap::const_iterator i = vAuras.begin(), next; i != vAuras.end(); i = next)
        {
            const SpellEntry *se = i->second->GetSpellProto();
            next = i; ++next;
            if (spellProto && spellProto->Id == se->Id) // Not drop auras added by self
                continue;
            if( se->AuraInterruptFlags & AURA_INTERRUPT_FLAG_DAMAGE )
            {
                bool remove = true;
                if (se->procFlags & (1<<3))
                {
                    if (!roll_chance_i(se->procChance))
                        remove = false;
                }
                if (remove)
                {
                    pVictim->RemoveAurasDueToSpell(i->second->GetId());
                    // FIXME: this may cause the auras with proc chance to be rerolled several times
                    next = vAuras.begin();
                }
            }
        }

        if (damagetype != NODAMAGE && damage && pVictim->GetTypeId() == TYPEID_PLAYER)
        {
            if( damagetype != DOT )
            {
                for (uint32 i = CURRENT_FIRST_NON_MELEE_SPELL; i < CURRENT_MAX_SPELL; ++i)
                {
                    // skip channeled spell (processed differently below)
                    if (i == CURRENT_CHANNELED_SPELL)
                        continue;

                    if(Spell* spell = pVictim->GetCurrentSpell(CurrentSpellTypes(i)))
                    {
                        if(spell->getState() == SPELL_STATE_PREPARING)
                        {
                            if(spell->m_spellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_ABORT_ON_DMG)
                                pVictim->InterruptSpell(CurrentSpellTypes(i));
                            else
                                spell->Delayed();
                        }
                    }
                }
            }

            if(Spell* spell = pVictim->m_currentSpells[CURRENT_CHANNELED_SPELL])
            {
                if (spell->getState() == SPELL_STATE_CASTING)
                {
                    uint32 channelInterruptFlags = spell->m_spellInfo->ChannelInterruptFlags;
                    if( channelInterruptFlags & CHANNEL_FLAG_DELAY )
                    {
                        if(pVictim!=this)                   //don't shorten the duration of channeling if you damage yourself
                            spell->DelayedChannel();
                    }
                    else if( (channelInterruptFlags & (CHANNEL_FLAG_DAMAGE | CHANNEL_FLAG_DAMAGE2)) )
                    {
                        sLog.outDetail("Spell %u canceled at damage!",spell->m_spellInfo->Id);
                        pVictim->InterruptSpell(CURRENT_CHANNELED_SPELL);
                    }
                }
                else if (spell->getState() == SPELL_STATE_DELAYED)
                    // break channeled spell in delayed state on damage
                {
                    sLog.outDetail("Spell %u canceled at damage!",spell->m_spellInfo->Id);
                    pVictim->InterruptSpell(CURRENT_CHANNELED_SPELL);
                }
            }
        }

        // last damage from duel opponent
        if(duel_hasEnded)
        {
            assert(pVictim->GetTypeId()==TYPEID_PLAYER);
            Player *he = (Player*)pVictim;

            assert(he->duel);

            he->SetHealth(1);

            he->duel->opponent->CombatStopWithPets(true);
            he->CombatStopWithPets(true);

            he->CastSpell(he, 7267, true);                  // beg
            he->DuelComplete(DUEL_WON);
        }
    }

    DEBUG_LOG("DealDamageEnd returned %d damage", damage);

    return damage;
}

void Unit::CastStop(uint32 except_spellid)
{
    for (uint32 i = CURRENT_FIRST_NON_MELEE_SPELL; i < CURRENT_MAX_SPELL; ++i)
        if (m_currentSpells[i] && m_currentSpells[i]->m_spellInfo->Id!=except_spellid)
            InterruptSpell(CurrentSpellTypes(i),false);
}

void Unit::CastSpell(Unit* Victim, uint32 spellId, bool triggered, Item *castItem, Aura* triggeredByAura, uint64 originalCaster)
{
    SpellEntry const *spellInfo = sSpellStore.LookupEntry(spellId );

    if(!spellInfo)
    {
        sLog.outError("CastSpell: unknown spell id %i by caster: %s %u)", spellId,(GetTypeId()==TYPEID_PLAYER ? "player (GUID:" : "creature (Entry:"),(GetTypeId()==TYPEID_PLAYER ? GetGUIDLow() : GetEntry()));
        return;
    }

    CastSpell(Victim,spellInfo,triggered,castItem,triggeredByAura, originalCaster);
}

void Unit::CastSpell(Unit* Victim,SpellEntry const *spellInfo, bool triggered, Item *castItem, Aura* triggeredByAura, uint64 originalCaster)
{
    if(!spellInfo)
    {
        sLog.outError("CastSpell: unknown spell by caster: %s %u)", (GetTypeId()==TYPEID_PLAYER ? "player (GUID:" : "creature (Entry:"),(GetTypeId()==TYPEID_PLAYER ? GetGUIDLow() : GetEntry()));
        return;
    }

    if (castItem)
        DEBUG_LOG("WORLD: cast Item spellId - %i", spellInfo->Id);

    if(!originalCaster && triggeredByAura)
        originalCaster = triggeredByAura->GetCasterGUID();

    Spell *spell = new Spell(this, spellInfo, triggered, originalCaster );

    SpellCastTargets targets;
    targets.setUnitTarget( Victim );
    spell->m_CastItem = castItem;
    spell->prepare(&targets, triggeredByAura);
}

void Unit::CastCustomSpell(Unit* Victim,uint32 spellId, int32 const* bp0, int32 const* bp1, int32 const* bp2, bool triggered, Item *castItem, Aura* triggeredByAura, uint64 originalCaster)
{
    SpellEntry const *spellInfo = sSpellStore.LookupEntry(spellId );

    if(!spellInfo)
    {
        sLog.outError("CastCustomSpell: unknown spell id %i", spellId);
        return;
    }

    CastCustomSpell(Victim,spellInfo,bp0,bp1,bp2,triggered,castItem,triggeredByAura, originalCaster);
}

void Unit::CastCustomSpell(Unit* Victim,SpellEntry const *spellInfo, int32 const* bp0, int32 const* bp1, int32 const* bp2, bool triggered, Item *castItem, Aura* triggeredByAura, uint64 originalCaster)
{
    if(!spellInfo)
    {
        sLog.outError("CastCustomSpell: unknown spell");
        return;
    }

    if (castItem)
        DEBUG_LOG("WORLD: cast Item spellId - %i", spellInfo->Id);

    if(!originalCaster && triggeredByAura)
        originalCaster = triggeredByAura->GetCasterGUID();

    Spell *spell = new Spell(this, spellInfo, triggered, originalCaster);

    if(bp0)
        spell->m_currentBasePoints[EFFECT_INDEX_0] = *bp0-int32(spellInfo->EffectBaseDice[EFFECT_INDEX_0]);

    if(bp1)
        spell->m_currentBasePoints[EFFECT_INDEX_1] = *bp1-int32(spellInfo->EffectBaseDice[EFFECT_INDEX_1]);

    if(bp2)
        spell->m_currentBasePoints[EFFECT_INDEX_2] = *bp2-int32(spellInfo->EffectBaseDice[EFFECT_INDEX_2]);

    SpellCastTargets targets;
    targets.setUnitTarget( Victim );
    spell->m_CastItem = castItem;
    spell->prepare(&targets, triggeredByAura);
}

// used for scripting
void Unit::CastSpell(float x, float y, float z, uint32 spellId, bool triggered, Item *castItem, Aura* triggeredByAura, uint64 originalCaster)
{
    SpellEntry const *spellInfo = sSpellStore.LookupEntry(spellId );

    if(!spellInfo)
    {
        sLog.outError("CastSpell(x,y,z): unknown spell id %i by caster: %s %u)", spellId,(GetTypeId()==TYPEID_PLAYER ? "player (GUID:" : "creature (Entry:"),(GetTypeId()==TYPEID_PLAYER ? GetGUIDLow() : GetEntry()));
        return;
    }

    CastSpell(x, y, z,spellInfo,triggered,castItem,triggeredByAura, originalCaster);
}

// used for scripting
void Unit::CastSpell(float x, float y, float z, SpellEntry const *spellInfo, bool triggered, Item *castItem, Aura* triggeredByAura, uint64 originalCaster)
{
    if(!spellInfo)
    {
        sLog.outError("CastSpell(x,y,z): unknown spell by caster: %s %u)", (GetTypeId()==TYPEID_PLAYER ? "player (GUID:" : "creature (Entry:"),(GetTypeId()==TYPEID_PLAYER ? GetGUIDLow() : GetEntry()));
        return;
    }

    if (castItem)
        DEBUG_LOG("WORLD: cast Item spellId - %i", spellInfo->Id);

    if(!originalCaster && triggeredByAura)
        originalCaster = triggeredByAura->GetCasterGUID();

    Spell *spell = new Spell(this, spellInfo, triggered, originalCaster );

    SpellCastTargets targets;
    targets.setDestination(x, y, z);
    spell->m_CastItem = castItem;
    spell->prepare(&targets, triggeredByAura);
}

// Obsolete func need remove, here only for comotability vs another patches
uint32 Unit::SpellNonMeleeDamageLog(Unit *pVictim, uint32 spellID, uint32 damage)
{
    SpellEntry const *spellInfo = sSpellStore.LookupEntry(spellID);
    SpellNonMeleeDamage damageInfo(this, pVictim, spellInfo->Id, spellInfo->SchoolMask);
    CalculateSpellDamage(&damageInfo, damage, spellInfo);
    DealDamageMods(damageInfo.target,damageInfo.damage,&damageInfo.absorb);
    SendSpellNonMeleeDamageLog(&damageInfo);
    DealSpellDamage(&damageInfo, true);
    return damageInfo.damage;
}

void Unit::CalculateSpellDamage(SpellNonMeleeDamage *damageInfo, int32 damage, SpellEntry const *spellInfo, WeaponAttackType attackType)
{
    SpellSchoolMask damageSchoolMask = SpellSchoolMask(damageInfo->schoolMask);
    Unit *pVictim = damageInfo->target;

    if (damage < 0)
        return;

    if(!this || !pVictim)
        return;
    if(!this->isAlive() || !pVictim->isAlive())
        return;

    // Check spell crit chance
    bool crit = isSpellCrit(pVictim, spellInfo, damageSchoolMask, attackType);
    bool blocked = false;

    // damage bonus (per damage class)
    switch (spellInfo->DmgClass)
    {
        // Melee and Ranged Spells
        case SPELL_DAMAGE_CLASS_RANGED:
        case SPELL_DAMAGE_CLASS_MELEE:
        {
            //Calculate damage bonus
            damage = MeleeDamageBonus(pVictim, damage, attackType, spellInfo, SPELL_DIRECT_DAMAGE);
            // Get blocked status
            blocked = isSpellBlocked(pVictim, spellInfo, attackType);

            // if crit add critical bonus
            if (crit)
            {
                damageInfo->HitInfo|= SPELL_HIT_TYPE_CRIT;
                damage = SpellCriticalDamageBonus(spellInfo, damage, pVictim);
                // Resilience - reduce crit damage
                uint32 redunction_affected_damage = CalcNotIgnoreDamageRedunction(damage,damageSchoolMask);
                if (attackType != RANGED_ATTACK)
                    damage -= pVictim->GetMeleeCritDamageReduction(redunction_affected_damage);
                else
                    damage -= pVictim->GetRangedCritDamageReduction(redunction_affected_damage);
            }
        }
        break;
        // Magical Attacks
        case SPELL_DAMAGE_CLASS_NONE:
        case SPELL_DAMAGE_CLASS_MAGIC:
        {
            // Calculate damage bonus
            damage = SpellDamageBonus(pVictim, spellInfo, damage, SPELL_DIRECT_DAMAGE);
            // If crit add critical bonus
            if (crit)
            {
                damageInfo->HitInfo|= SPELL_HIT_TYPE_CRIT;
                damage = SpellCriticalDamageBonus(spellInfo, damage, pVictim);
                // Resilience - reduce crit damage
                uint32 redunction_affected_damage = CalcNotIgnoreDamageRedunction(damage,damageSchoolMask);
                damage -= pVictim->GetSpellCritDamageReduction(redunction_affected_damage);
            }
        }
        break;
    }

    // only from players
    if (GetTypeId() == TYPEID_PLAYER)
    {
        uint32 redunction_affected_damage = CalcNotIgnoreDamageRedunction(damage,damageSchoolMask);
        damage -= pVictim->GetSpellDamageReduction(redunction_affected_damage);
    }

    // damage mitigation
    if (damage > 0)
    {
        // physical damage => armor
        if (damageSchoolMask & SPELL_SCHOOL_MASK_NORMAL)
        {
            uint32 armor_affected_damage = CalcNotIgnoreDamageRedunction(damage,damageSchoolMask);
            damage = damage - armor_affected_damage + CalcArmorReducedDamage(pVictim, armor_affected_damage);
        }

        // block (only for damage class ranged and -melee, also non-physical damage possible)
        if (blocked)
        {
            damageInfo->blocked = pVictim->GetShieldBlockValue();
            if (damage < (int32)damageInfo->blocked)
                damageInfo->blocked = damage;
            damage-=damageInfo->blocked;
        }

        uint32 absorb_affected_damage = CalcNotIgnoreAbsorbDamage(damage,damageSchoolMask,spellInfo);
        CalcAbsorbResist(pVictim, damageSchoolMask, SPELL_DIRECT_DAMAGE, absorb_affected_damage, &damageInfo->absorb, &damageInfo->resist, !(spellInfo->AttributesEx2 & SPELL_ATTR_EX2_CANT_REFLECTED));
        damage-= damageInfo->absorb + damageInfo->resist;
    }
    else
        damage = 0;
    damageInfo->damage = damage;
}

void Unit::DealSpellDamage(SpellNonMeleeDamage *damageInfo, bool durabilityLoss)
{
    if (!damageInfo)
        return;

    Unit *pVictim = damageInfo->target;

    if(!this || !pVictim)
        return;

    if (!pVictim->isAlive() || pVictim->isInFlight() || pVictim->GetTypeId() == TYPEID_UNIT && ((Creature*)pVictim)->IsInEvadeMode())
        return;

    SpellEntry const *spellProto = sSpellStore.LookupEntry(damageInfo->SpellID);
    if (spellProto == NULL)
    {
        sLog.outDebug("Unit::DealSpellDamage have wrong damageInfo->SpellID: %u", damageInfo->SpellID);
        return;
    }

    //You don't lose health from damage taken from another player while in a sanctuary
    //You still see it in the combat log though
    if(pVictim != this && GetTypeId() == TYPEID_PLAYER && pVictim->GetTypeId() == TYPEID_PLAYER)
    {
        const AreaTableEntry *area = GetAreaEntryByAreaID(pVictim->GetAreaId());
        if(area && area->flags & AREA_FLAG_SANCTUARY)       // sanctuary
            return;
    }

    // Call default DealDamage (send critical in hit info for threat calculation)
    CleanDamage cleanDamage(damageInfo->cleanDamage, BASE_ATTACK, damageInfo->HitInfo & SPELL_HIT_TYPE_CRIT ? MELEE_HIT_CRIT : MELEE_HIT_NORMAL);
    DealDamage(pVictim, damageInfo->damage, &cleanDamage, SPELL_DIRECT_DAMAGE, SpellSchoolMask(damageInfo->schoolMask), spellProto, durabilityLoss);
}

//TODO for melee need create structure as in
void Unit::CalculateMeleeDamage(Unit *pVictim, uint32 damage, CalcDamageInfo *damageInfo, WeaponAttackType attackType)
{
    damageInfo->attacker         = this;
    damageInfo->target           = pVictim;
    damageInfo->damageSchoolMask = GetMeleeDamageSchoolMask();
    damageInfo->attackType       = attackType;
    damageInfo->damage           = 0;
    damageInfo->cleanDamage      = 0;
    damageInfo->absorb           = 0;
    damageInfo->resist           = 0;
    damageInfo->blocked_amount   = 0;

    damageInfo->TargetState      = 0;
    damageInfo->HitInfo          = 0;
    damageInfo->procAttacker     = PROC_FLAG_NONE;
    damageInfo->procVictim       = PROC_FLAG_NONE;
    damageInfo->procEx           = PROC_EX_NONE;
    damageInfo->hitOutCome       = MELEE_HIT_EVADE;

    if(!this || !pVictim)
        return;
    if(!this->isAlive() || !pVictim->isAlive())
        return;

    // Select HitInfo/procAttacker/procVictim flag based on attack type
    switch (attackType)
    {
        case BASE_ATTACK:
            damageInfo->procAttacker = PROC_FLAG_SUCCESSFUL_MELEE_HIT;
            damageInfo->procVictim   = PROC_FLAG_TAKEN_MELEE_HIT;
            damageInfo->HitInfo      = HITINFO_NORMALSWING2;
            break;
        case OFF_ATTACK:
            damageInfo->procAttacker = PROC_FLAG_SUCCESSFUL_MELEE_HIT | PROC_FLAG_SUCCESSFUL_OFFHAND_HIT;
            damageInfo->procVictim   = PROC_FLAG_TAKEN_MELEE_HIT;//|PROC_FLAG_TAKEN_OFFHAND_HIT // not used
            damageInfo->HitInfo = HITINFO_LEFTSWING;
            break;
        case RANGED_ATTACK:
            damageInfo->procAttacker = PROC_FLAG_SUCCESSFUL_RANGED_HIT;
            damageInfo->procVictim   = PROC_FLAG_TAKEN_RANGED_HIT;
            damageInfo->HitInfo = 0x08;// test
            break;
        default:
            break;
    }

    // Physical Immune check
    if (damageInfo->target->IsImmunedToDamage(damageInfo->damageSchoolMask))
    {
       damageInfo->HitInfo       |= HITINFO_NORMALSWING;
       damageInfo->TargetState    = VICTIMSTATE_IS_IMMUNE;

       damageInfo->procEx |=PROC_EX_IMMUNE;
       damageInfo->damage         = 0;
       damageInfo->cleanDamage    = 0;
       return;
    }
    damage += CalculateDamage (damageInfo->attackType, false);
    // Add melee damage bonus
    damage = MeleeDamageBonus(damageInfo->target, damage, damageInfo->attackType);
    // Calculate armor reduction

    uint32 armor_affected_damage = CalcNotIgnoreDamageRedunction(damage,damageInfo->damageSchoolMask);
    damageInfo->damage = damage - armor_affected_damage + CalcArmorReducedDamage(damageInfo->target, armor_affected_damage);
    damageInfo->cleanDamage += damage - damageInfo->damage;

    damageInfo->hitOutCome = RollMeleeOutcomeAgainst(damageInfo->target, damageInfo->attackType);

    // Disable parry or dodge for ranged attack
    if (damageInfo->attackType == RANGED_ATTACK)
    {
        if (damageInfo->hitOutCome == MELEE_HIT_PARRY) damageInfo->hitOutCome = MELEE_HIT_NORMAL;
        if (damageInfo->hitOutCome == MELEE_HIT_DODGE) damageInfo->hitOutCome = MELEE_HIT_MISS;
    }

    switch(damageInfo->hitOutCome)
    {
        case MELEE_HIT_EVADE:
        {
            damageInfo->HitInfo    |= HITINFO_MISS|HITINFO_SWINGNOHITSOUND;
            damageInfo->TargetState = VICTIMSTATE_EVADES;

            damageInfo->procEx|=PROC_EX_EVADE;
            damageInfo->damage = 0;
            damageInfo->cleanDamage = 0;
            return;
        }
        case MELEE_HIT_MISS:
        {
            damageInfo->HitInfo    |= HITINFO_MISS;
            damageInfo->TargetState = VICTIMSTATE_NORMAL;

            damageInfo->procEx|=PROC_EX_MISS;
            damageInfo->damage = 0;
            damageInfo->cleanDamage = 0;
            break;
        }
        case MELEE_HIT_NORMAL:
            damageInfo->TargetState = VICTIMSTATE_NORMAL;
            damageInfo->procEx|=PROC_EX_NORMAL_HIT;
            break;
        case MELEE_HIT_CRIT:
        {
            damageInfo->HitInfo     |= HITINFO_CRITICALHIT;
            damageInfo->TargetState  = VICTIMSTATE_NORMAL;

            damageInfo->procEx|=PROC_EX_CRITICAL_HIT;
            // Crit bonus calc
            damageInfo->damage += damageInfo->damage;
            int32 mod=0;
            // Apply SPELL_AURA_MOD_ATTACKER_RANGED_CRIT_DAMAGE or SPELL_AURA_MOD_ATTACKER_MELEE_CRIT_DAMAGE
            if(damageInfo->attackType == RANGED_ATTACK)
                mod += damageInfo->target->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_RANGED_CRIT_DAMAGE);
            else
                mod += damageInfo->target->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_MELEE_CRIT_DAMAGE);

            mod += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_CRIT_DAMAGE_BONUS, SPELL_SCHOOL_MASK_NORMAL);

            uint32 crTypeMask = damageInfo->target->GetCreatureTypeMask();

            // Increase crit damage from SPELL_AURA_MOD_CRIT_PERCENT_VERSUS
            mod += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_CRIT_PERCENT_VERSUS, crTypeMask);
            if (mod!=0)
                damageInfo->damage = int32((damageInfo->damage) * float((100.0f + mod)/100.0f));

            // Resilience - reduce crit damage
            uint32 redunction_affected_damage = CalcNotIgnoreDamageRedunction(damageInfo->damage,damageInfo->damageSchoolMask);
            uint32 resilienceReduction;
            if (attackType != RANGED_ATTACK)
                resilienceReduction = pVictim->GetMeleeCritDamageReduction(redunction_affected_damage);
            else
                resilienceReduction = pVictim->GetRangedCritDamageReduction(redunction_affected_damage);

            damageInfo->damage      -= resilienceReduction;
            damageInfo->cleanDamage += resilienceReduction;
            break;
        }
        case MELEE_HIT_PARRY:
            damageInfo->TargetState  = VICTIMSTATE_PARRY;
            damageInfo->procEx |= PROC_EX_PARRY;
            damageInfo->cleanDamage += damageInfo->damage;
            damageInfo->damage = 0;
            break;

        case MELEE_HIT_DODGE:
            damageInfo->TargetState  = VICTIMSTATE_DODGE;
            damageInfo->procEx|=PROC_EX_DODGE;
            damageInfo->cleanDamage += damageInfo->damage;
            damageInfo->damage = 0;
            break;
        case MELEE_HIT_BLOCK:
        {
            damageInfo->TargetState = VICTIMSTATE_NORMAL;
            damageInfo->HitInfo |= HITINFO_BLOCK;
            damageInfo->procEx |= PROC_EX_BLOCK;
            damageInfo->blocked_amount = damageInfo->target->GetShieldBlockValue();

            // Target has a chance to double the blocked amount if it has SPELL_AURA_MOD_BLOCK_CRIT_CHANCE
            if (roll_chance_i(pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_BLOCK_CRIT_CHANCE)))
                damageInfo->blocked_amount *= 2;

            if (damageInfo->blocked_amount >= damageInfo->damage)
            {
                damageInfo->TargetState = VICTIMSTATE_BLOCKS;
                damageInfo->blocked_amount = damageInfo->damage;
                damageInfo->procEx |= PROC_EX_FULL_BLOCK;
            }
            else
                damageInfo->procEx |= PROC_EX_NORMAL_HIT;   // Partial blocks can still cause attacker procs

            damageInfo->damage      -= damageInfo->blocked_amount;
            damageInfo->cleanDamage += damageInfo->blocked_amount;
            break;
        }
        case MELEE_HIT_GLANCING:
        {
            damageInfo->HitInfo |= HITINFO_GLANCING;
            damageInfo->TargetState = VICTIMSTATE_NORMAL;
            damageInfo->procEx |= PROC_EX_NORMAL_HIT;
            float reducePercent = 1.0f;                     //damage factor
            // calculate base values and mods
            float baseLowEnd = 1.3f;
            float baseHighEnd = 1.2f;
            switch(getClass())                              // lowering base values for casters
            {
                case CLASS_SHAMAN:
                case CLASS_PRIEST:
                case CLASS_MAGE:
                case CLASS_WARLOCK:
                case CLASS_DRUID:
                    baseLowEnd  -= 0.7f;
                    baseHighEnd -= 0.3f;
                    break;
            }

            float maxLowEnd = 0.6f;
            switch(getClass())                              // upper for melee classes
            {
                case CLASS_WARRIOR:
                case CLASS_ROGUE:
                    maxLowEnd = 0.91f;                      //If the attacker is a melee class then instead the lower value of 0.91
            }

            // calculate values
            int32 diff = damageInfo->target->GetDefenseSkillValue() - GetWeaponSkillValue(damageInfo->attackType);
            float lowEnd  = baseLowEnd - ( 0.05f * diff );
            float highEnd = baseHighEnd - ( 0.03f * diff );

            // apply max/min bounds
            if ( lowEnd < 0.01f )                           //the low end must not go bellow 0.01f
                lowEnd = 0.01f;
            else if ( lowEnd > maxLowEnd )                  //the smaller value of this and 0.6 is kept as the low end
                lowEnd = maxLowEnd;

            if ( highEnd < 0.2f )                           //high end limits
                highEnd = 0.2f;
            if ( highEnd > 0.99f )
                highEnd = 0.99f;

            if(lowEnd > highEnd)                            // prevent negative range size
                lowEnd = highEnd;

            reducePercent = lowEnd + rand_norm_f() * ( highEnd - lowEnd );

            damageInfo->cleanDamage += damageInfo->damage-uint32(reducePercent *  damageInfo->damage);
            damageInfo->damage   = uint32(reducePercent *  damageInfo->damage);
            break;
        }
        case MELEE_HIT_CRUSHING:
        {
            damageInfo->HitInfo     |= HITINFO_CRUSHING;
            damageInfo->TargetState  = VICTIMSTATE_NORMAL;
            damageInfo->procEx|=PROC_EX_NORMAL_HIT;
            // 150% normal damage
            damageInfo->damage += (damageInfo->damage / 2);
            break;
        }
        default:

            break;
    }

    // only from players
    if (GetTypeId() == TYPEID_PLAYER)
    {
        uint32 redunction_affected_damage = CalcNotIgnoreDamageRedunction(damage,damageInfo->damageSchoolMask);
        if (attackType != RANGED_ATTACK)
            damage -= pVictim->GetMeleeDamageReduction(redunction_affected_damage);
        else
            damage -= pVictim->GetRangedDamageReduction(redunction_affected_damage);
    }

    // Calculate absorb resist
    if(int32(damageInfo->damage) > 0)
    {
        damageInfo->procVictim |= PROC_FLAG_TAKEN_ANY_DAMAGE;

        // Calculate absorb & resists
        uint32 absorb_affected_damage = CalcNotIgnoreAbsorbDamage(damageInfo->damage,damageInfo->damageSchoolMask);
        CalcAbsorbResist(damageInfo->target, damageInfo->damageSchoolMask, DIRECT_DAMAGE, absorb_affected_damage, &damageInfo->absorb, &damageInfo->resist, true);
        damageInfo->damage-=damageInfo->absorb + damageInfo->resist;
        if (damageInfo->absorb)
        {
            damageInfo->HitInfo|=HITINFO_ABSORB;
            damageInfo->procEx|=PROC_EX_ABSORB;
        }
        if (damageInfo->resist)
            damageInfo->HitInfo|=HITINFO_RESIST;

    }
    else // Umpossible get negative result but....
        damageInfo->damage = 0;
}

void Unit::DealMeleeDamage(CalcDamageInfo *damageInfo, bool durabilityLoss)
{
    if (damageInfo==0) return;
    Unit *pVictim = damageInfo->target;

    if(!this || !pVictim)
        return;

    if (!pVictim->isAlive() || pVictim->isInFlight() || pVictim->GetTypeId() == TYPEID_UNIT && ((Creature*)pVictim)->IsInEvadeMode())
        return;

    //You don't lose health from damage taken from another player while in a sanctuary
    //You still see it in the combat log though
    if(pVictim != this && GetTypeId() == TYPEID_PLAYER && pVictim->GetTypeId() == TYPEID_PLAYER)
    {
        const AreaTableEntry *area = GetAreaEntryByAreaID(pVictim->GetAreaId());
        if(area && area->flags & AREA_FLAG_SANCTUARY)       // sanctuary
            return;
    }

    // Hmmmm dont like this emotes client must by self do all animations
    if (damageInfo->HitInfo&HITINFO_CRITICALHIT)
        pVictim->HandleEmoteCommand(EMOTE_ONESHOT_WOUNDCRITICAL);
    if(damageInfo->blocked_amount && damageInfo->TargetState!=VICTIMSTATE_BLOCKS)
        pVictim->HandleEmoteCommand(EMOTE_ONESHOT_PARRYSHIELD);

    if(damageInfo->TargetState == VICTIMSTATE_PARRY)
    {
        // Get attack timers
        float offtime  = float(pVictim->getAttackTimer(OFF_ATTACK));
        float basetime = float(pVictim->getAttackTimer(BASE_ATTACK));
        // Reduce attack time
        if (pVictim->haveOffhandWeapon() && offtime < basetime)
        {
            float percent20 = pVictim->GetAttackTime(OFF_ATTACK) * 0.20f;
            float percent60 = 3.0f * percent20;
            if(offtime > percent20 && offtime <= percent60)
            {
                pVictim->setAttackTimer(OFF_ATTACK, uint32(percent20));
            }
            else if(offtime > percent60)
            {
                offtime -= 2.0f * percent20;
                pVictim->setAttackTimer(OFF_ATTACK, uint32(offtime));
            }
        }
        else
        {
            float percent20 = pVictim->GetAttackTime(BASE_ATTACK) * 0.20f;
            float percent60 = 3.0f * percent20;
            if(basetime > percent20 && basetime <= percent60)
            {
                pVictim->setAttackTimer(BASE_ATTACK, uint32(percent20));
            }
            else if(basetime > percent60)
            {
                basetime -= 2.0f * percent20;
                pVictim->setAttackTimer(BASE_ATTACK, uint32(basetime));
            }
        }
    }

    // Call default DealDamage
    CleanDamage cleanDamage(damageInfo->cleanDamage,damageInfo->attackType,damageInfo->hitOutCome);
    DealDamage(pVictim, damageInfo->damage, &cleanDamage, DIRECT_DAMAGE, damageInfo->damageSchoolMask, NULL, durabilityLoss);

    // If this is a creature and it attacks from behind it has a probability to daze it's victim
    if( (damageInfo->hitOutCome==MELEE_HIT_CRIT || damageInfo->hitOutCome==MELEE_HIT_CRUSHING || damageInfo->hitOutCome==MELEE_HIT_NORMAL || damageInfo->hitOutCome==MELEE_HIT_GLANCING) &&
        GetTypeId() != TYPEID_PLAYER && !((Creature*)this)->GetCharmerOrOwnerGUID() && !pVictim->HasInArc(M_PI_F, this) )
    {
        // -probability is between 0% and 40%
        // 20% base chance
        float Probability = 20.0f;

        //there is a newbie protection, at level 10 just 7% base chance; assuming linear function
        if( pVictim->getLevel() < 30 )
            Probability = 0.65f*pVictim->getLevel()+0.5f;

        uint32 VictimDefense=pVictim->GetDefenseSkillValue();
        uint32 AttackerMeleeSkill=GetUnitMeleeSkill();

        Probability *= AttackerMeleeSkill/(float)VictimDefense;

        if(Probability > 40.0f)
            Probability = 40.0f;

        if(roll_chance_f(Probability))
            CastSpell(pVictim, 1604, true);
    }

    // If not miss
    if (!(damageInfo->HitInfo & HITINFO_MISS))
    {
        // on weapon hit casts
        if(GetTypeId() == TYPEID_PLAYER && pVictim->isAlive())
            ((Player*)this)->CastItemCombatSpell(pVictim, damageInfo->attackType);

        // victim's damage shield
        std::set<Aura*> alreadyDone;
        AuraList const& vDamageShields = pVictim->GetAurasByType(SPELL_AURA_DAMAGE_SHIELD);
        for(AuraList::const_iterator i = vDamageShields.begin(); i != vDamageShields.end();)
        {
           if (alreadyDone.find(*i) == alreadyDone.end())
           {
               alreadyDone.insert(*i);
               uint32 damage=(*i)->GetModifier()->m_amount;
               SpellEntry const *i_spellProto = (*i)->GetSpellProto();
               //Calculate absorb resist ??? no data in opcode for this possibly unable to absorb or resist?
               //uint32 absorb;
               //uint32 resist;
               //CalcAbsorbResist(pVictim, SpellSchools(spellProto->School), SPELL_DIRECT_DAMAGE, damage, &absorb, &resist);
               //damage-=absorb + resist;

               pVictim->DealDamageMods(this,damage,NULL);

               WorldPacket data(SMSG_SPELLDAMAGESHIELD,(8+8+4+4+4+4));
               data << uint64(pVictim->GetGUID());
               data << uint64(GetGUID());
               data << uint32(i_spellProto->Id);
               data << uint32(damage);                  // Damage
               data << uint32(0);                       // Overkill
               data << uint32(i_spellProto->SchoolMask);
               pVictim->SendMessageToSet(&data, true );

               pVictim->DealDamage(this, damage, 0, SPELL_DIRECT_DAMAGE, GetSpellSchoolMask(i_spellProto), i_spellProto, true);

               i = vDamageShields.begin();
           }
           else
                ++i;
        }
    }
}


void Unit::HandleEmoteCommand(uint32 anim_id)
{
    WorldPacket data( SMSG_EMOTE, 4 + 8 );
    data << uint32(anim_id);
    data << uint64(GetGUID());
    SendMessageToSet(&data, true);
}

uint32 Unit::CalcNotIgnoreAbsorbDamage( uint32 damage, SpellSchoolMask damageSchoolMask, SpellEntry const* spellInfo /*= NULL*/)
{
    float absorb_affected_rate = 1.0f;
    Unit::AuraList const& ignoreAbsorbSchool = GetAurasByType(SPELL_AURA_MOD_IGNORE_ABSORB_SCHOOL);
    for(Unit::AuraList::const_iterator i = ignoreAbsorbSchool.begin(); i != ignoreAbsorbSchool.end(); ++i)
        if ((*i)->GetMiscValue() & damageSchoolMask)
            absorb_affected_rate *= (100.0f - (*i)->GetModifier()->m_amount)/100.0f;

    if(spellInfo)
    {
        Unit::AuraList const& ignoreAbsorbForSpell = GetAurasByType(SPELL_AURA_MOD_IGNORE_ABSORB_FOR_SPELL);
        for(Unit::AuraList::const_iterator citr = ignoreAbsorbForSpell.begin(); citr != ignoreAbsorbForSpell.end(); ++citr)
            if ((*citr)->isAffectedOnSpell(spellInfo))
                absorb_affected_rate *= (100.0f - (*citr)->GetModifier()->m_amount)/100.0f;
    }

    return absorb_affected_rate <= 0.0f ? 0 : (absorb_affected_rate < 1.0f  ? uint32(damage * absorb_affected_rate) : damage);
}

uint32 Unit::CalcNotIgnoreDamageRedunction( uint32 damage, SpellSchoolMask damageSchoolMask)
{
    float absorb_affected_rate = 1.0f;
    Unit::AuraList const& ignoreAbsorb = GetAurasByType(SPELL_AURA_MOD_IGNORE_DAMAGE_REDUCTION_SCHOOL);
    for(Unit::AuraList::const_iterator i = ignoreAbsorb.begin(); i != ignoreAbsorb.end(); ++i)
        if ((*i)->GetMiscValue() & damageSchoolMask)
            absorb_affected_rate *= (100.0f - (*i)->GetModifier()->m_amount)/100.0f;

    return absorb_affected_rate <= 0.0f ? 0 : (absorb_affected_rate < 1.0f  ? uint32(damage * absorb_affected_rate) : damage);
}

uint32 Unit::CalcArmorReducedDamage(Unit* pVictim, const uint32 damage)
{
    uint32 newdamage = 0;
    float armor = (float)pVictim->GetArmor();

    // Ignore enemy armor by SPELL_AURA_MOD_TARGET_RESISTANCE aura
    armor += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_TARGET_RESISTANCE, SPELL_SCHOOL_MASK_NORMAL);

    // Apply Player CR_ARMOR_PENETRATION rating and percent talents
    if (GetTypeId()==TYPEID_PLAYER)
        armor *= 1.0f - ((Player*)this)->GetArmorPenetrationPct() / 100.0f;

    if (armor < 0.0f)
        armor = 0.0f;

    float levelModifier = (float)getLevel();
    if (levelModifier > 59)
        levelModifier = levelModifier + (4.5f * (levelModifier-59));

    float tmpvalue = 0.1f * armor / (8.5f * levelModifier + 40);
    tmpvalue = tmpvalue/(1.0f + tmpvalue);

    if (tmpvalue < 0.0f)
        tmpvalue = 0.0f;
    if (tmpvalue > 0.75f)
        tmpvalue = 0.75f;

    newdamage = uint32(damage - (damage * tmpvalue));

    return (newdamage > 1) ? newdamage : 1;
}

void Unit::CalcAbsorbResist(Unit *pVictim,SpellSchoolMask schoolMask, DamageEffectType damagetype, const uint32 damage, uint32 *absorb, uint32 *resist, bool canReflect)
{
    if(!pVictim || !pVictim->isAlive() || !damage)
        return;

    // Magic damage, check for resists
    if ((schoolMask & SPELL_SCHOOL_MASK_NORMAL)==0)
    {
        // Get base victim resistance for school
        float tmpvalue2 = (float)pVictim->GetResistance(GetFirstSchoolInMask(schoolMask));
        // Ignore resistance by self SPELL_AURA_MOD_TARGET_RESISTANCE aura
        tmpvalue2 += (float)GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_TARGET_RESISTANCE, schoolMask);

        tmpvalue2 *= (float)(0.15f / getLevel());
        if (tmpvalue2 < 0.0f)
            tmpvalue2 = 0.0f;
        if (tmpvalue2 > 0.75f)
            tmpvalue2 = 0.75f;
        uint32 ran = urand(0, 100);
        float faq[4] = {24.0f,6.0f,4.0f,6.0f};
        uint8 m = 0;
        float Binom = 0.0f;
        for (uint8 i = 0; i < 4; ++i)
        {
            Binom += 2400 *( powf(tmpvalue2, float(i)) * powf( (1-tmpvalue2), float(4-i)))/faq[i];
            if (ran > Binom )
                ++m;
            else
                break;
        }
        if (damagetype == DOT && m == 4)
            *resist += uint32(damage - 1);
        else
            *resist += uint32(damage * m / 4);
        if(*resist > damage)
            *resist = damage;
    }
    else
        *resist = 0;

    int32 RemainingDamage = damage - *resist;

    // Get unit state (need for some absorb check)
    uint32 unitflag = pVictim->GetUInt32Value(UNIT_FIELD_FLAGS);
    // Reflect damage spells (not cast any damage spell in aura lookup)
    uint32 reflectSpell = 0;
    int32  reflectDamage = 0;
    Aura*  reflectTriggeredBy = NULL;                       // expected as not expired at reflect as in current cases
    // Death Prevention Aura
    SpellEntry const*  preventDeathSpell = NULL;
    int32  preventDeathAmount = 0;

    // full absorb cases (by chance)
    AuraList const& vAbsorb = pVictim->GetAurasByType(SPELL_AURA_SCHOOL_ABSORB);
    for(AuraList::const_iterator i = vAbsorb.begin(); i != vAbsorb.end() && RemainingDamage > 0; ++i)
    {
        // only work with proper school mask damage
        Modifier* i_mod = (*i)->GetModifier();
        if (!(i_mod->m_miscvalue & schoolMask))
            continue;

        SpellEntry const* i_spellProto = (*i)->GetSpellProto();
        // Fire Ward or Frost Ward
        if(i_spellProto->SpellFamilyName == SPELLFAMILY_MAGE && i_spellProto->SpellFamilyFlags & UI64LIT(0x0000000000000108))
        {
            int chance = 0;
            Unit::AuraList const& auras = pVictim->GetAurasByType(SPELL_AURA_ADD_PCT_MODIFIER);
            for (Unit::AuraList::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
            {
                SpellEntry const* itr_spellProto = (*itr)->GetSpellProto();
                // Frost Warding (chance full absorb)
                if (itr_spellProto->SpellFamilyName == SPELLFAMILY_MAGE && itr_spellProto->SpellIconID == 501)
                {
                    // chance stored in next dummy effect
                    chance = itr_spellProto->CalculateSimpleValue(EFFECT_INDEX_1);
                    break;
                }
            }
            if(roll_chance_i(chance))
            {
                int32 amount = RemainingDamage;
                RemainingDamage = 0;
                // Frost Warding (mana regen)
                pVictim->CastCustomSpell(pVictim, 57776, &amount, NULL, NULL, true, NULL, *i);
                break;
            }
        }
    }

    // Need remove expired auras after
    bool existExpired = false;

    // absorb without mana cost
    AuraList const& vSchoolAbsorb = pVictim->GetAurasByType(SPELL_AURA_SCHOOL_ABSORB);
    for(AuraList::const_iterator i = vSchoolAbsorb.begin(); i != vSchoolAbsorb.end() && RemainingDamage > 0; ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        if (!(mod->m_miscvalue & schoolMask))
            continue;

        SpellEntry const* spellProto = (*i)->GetSpellProto();

        // Max Amount can be absorbed by this aura
        int32  currentAbsorb = mod->m_amount;

        // Found empty aura (impossible but..)
        if (currentAbsorb <=0)
        {
            existExpired = true;
            continue;
        }
        // Handle custom absorb auras
        // TODO: try find better way
        switch(spellProto->SpellFamilyName)
        {
            case SPELLFAMILY_GENERIC:
            {
                // Astral Shift
                if (spellProto->SpellIconID == 3066)
                {
                    //reduces all damage taken while stun, fear or silence
                    if (unitflag & (UNIT_FLAG_STUNNED|UNIT_FLAG_FLEEING|UNIT_FLAG_SILENCED))
                        RemainingDamage -= RemainingDamage * currentAbsorb / 100;
                    continue;
                }
                // Nerves of Steel
                if (spellProto->SpellIconID == 2115)
                {
                    // while affected by Stun and Fear
                    if (unitflag&(UNIT_FLAG_STUNNED|UNIT_FLAG_FLEEING))
                        RemainingDamage -= RemainingDamage * currentAbsorb / 100;
                    continue;
                }
                // Spell Deflection
                if (spellProto->SpellIconID == 3006)
                {
                    // You have a chance equal to your Parry chance
                    if (damagetype == DIRECT_DAMAGE &&                   // Only for direct damage
                        roll_chance_f(pVictim->GetUnitParryChance()))    // Roll chance
                        RemainingDamage -= RemainingDamage * currentAbsorb / 100;
                    continue;
                }
                // Reflective Shield (Lady Malande boss)
                if (spellProto->Id == 41475 && canReflect)
                {
                    if(RemainingDamage < currentAbsorb)
                        reflectDamage = RemainingDamage / 2;
                    else
                        reflectDamage = currentAbsorb / 2;
                    reflectSpell = 33619;
                    reflectTriggeredBy = *i;
                    break;
                }
                if (spellProto->Id == 39228 || // Argussian Compass
                    spellProto->Id == 60218)   // Essence of Gossamer
                {
                    // Max absorb stored in 1 dummy effect
                    if (spellProto->EffectBasePoints[EFFECT_INDEX_1] < currentAbsorb)
                        currentAbsorb = spellProto->EffectBasePoints[EFFECT_INDEX_1];
                    break;
                }
                break;
            }
            case SPELLFAMILY_DRUID:
            {
                // Primal Tenacity
                if (spellProto->SpellIconID == 2253)
                {
                    //reduces all damage taken while Stunned
                    if (unitflag & UNIT_FLAG_STUNNED)
                        RemainingDamage -= RemainingDamage * currentAbsorb / 100;
                    continue;
                }
                break;
            }
            case SPELLFAMILY_ROGUE:
            {
                // Cheat Death (make less prio with Guardian Spirit case)
                if (!preventDeathSpell && spellProto->SpellIconID == 2109 &&
                    pVictim->GetTypeId()==TYPEID_PLAYER &&  // Only players
                    !((Player*)pVictim)->HasSpellCooldown(31231) &&
                                                            // Only if no cooldown
                    roll_chance_i((*i)->GetModifier()->m_amount))
                                                            // Only if roll
                {
                    preventDeathSpell = (*i)->GetSpellProto();
                    continue;
                }
                break;
            }
            case SPELLFAMILY_PRIEST:
            {
                // Guardian Spirit
                if (spellProto->SpellIconID == 2873)
                {
                    preventDeathSpell = (*i)->GetSpellProto();
                    preventDeathAmount = (*i)->GetModifier()->m_amount;
                    continue;
                }
                // Power Word: Shield
                if (spellProto->SpellFamilyFlags & UI64LIT(00000001) && spellProto->Mechanic == MECHANIC_SHIELD)
                {
                    // Glyph of Power Word: Shield
                    if (Aura *glyph = pVictim->GetAura(55672, EFFECT_INDEX_0))
                    {
                        int32 heal = int32(glyph->GetModifier()->m_amount *
                            (RemainingDamage >= currentAbsorb ? currentAbsorb : RemainingDamage) / 100);
                        pVictim->CastCustomSpell(pVictim, 56160, &heal, NULL, NULL, true, 0, *i);
                    }
                }
                // Reflective Shield
                if (spellProto->SpellFamilyFlags == 0x1 && canReflect)
                {
                    if (pVictim == this)
                        break;
                    Unit* caster = (*i)->GetCaster();
                    if (!caster)
                        break;
                    AuraList const& vOverRideCS = caster->GetAurasByType(SPELL_AURA_DUMMY);
                    for(AuraList::const_iterator k = vOverRideCS.begin(); k != vOverRideCS.end(); ++k)
                    {
                        switch((*k)->GetModifier()->m_miscvalue)
                        {
                            case 5065:                      // Rank 1
                            case 5064:                      // Rank 2
                            {
                                if(RemainingDamage >= currentAbsorb)
                                    reflectDamage = (*k)->GetModifier()->m_amount * currentAbsorb/100;
                                else
                                    reflectDamage = (*k)->GetModifier()->m_amount * RemainingDamage/100;
                                reflectSpell = 33619;
                                reflectTriggeredBy = *i;
                            } break;
                            default: break;
                        }
                    }
                    break;
                }
                break;
            }
            case SPELLFAMILY_SHAMAN:
            {
                // Astral Shift
                if (spellProto->SpellIconID == 3066)
                {
                    //reduces all damage taken while stun, fear or silence
                    if (unitflag & (UNIT_FLAG_STUNNED|UNIT_FLAG_FLEEING|UNIT_FLAG_SILENCED))
                        RemainingDamage -= RemainingDamage * currentAbsorb / 100;
                    continue;
                }
                break;
            }
            case SPELLFAMILY_DEATHKNIGHT:
            {
                // Shadow of Death
                if (spellProto->SpellIconID == 1958)
                {
                    // TODO: absorb only while transform
                    continue;
                }
                // Anti-Magic Shell (on self)
                if (spellProto->Id == 48707)
                {
                    // damage absorbed by Anti-Magic Shell energizes the DK with additional runic power.
                    // This, if I'm not mistaken, shows that we get back ~2% of the absorbed damage as runic power.
                    int32 absorbed = RemainingDamage * currentAbsorb / 100;
                    int32 regen = absorbed * 2 / 10;
                    pVictim->CastCustomSpell(pVictim, 49088, &regen, NULL, NULL, true, NULL, *i);
                    RemainingDamage -= absorbed;
                    continue;
                }
                // Anti-Magic Shell (on single party/raid member)
                if (spellProto->Id == 50462)
                {
                    RemainingDamage -= RemainingDamage * currentAbsorb / 100;
                    continue;
                }
                // Anti-Magic Zone
                if (spellProto->Id == 50461)
                {
                    Unit* caster = (*i)->GetCaster();
                    if (!caster)
                        continue;
                    int32 absorbed = RemainingDamage * currentAbsorb / 100;
                    int32 canabsorb = caster->GetHealth();
                    if (canabsorb < absorbed)
                        absorbed = canabsorb;

                    RemainingDamage -= absorbed;

                    uint32 ab_damage = absorbed;
                    DealDamageMods(caster,ab_damage,NULL);
                    DealDamage(caster, ab_damage, NULL, damagetype, schoolMask, 0, false);
                    continue;
                }
                break;
            }
            default:
                break;
        }

        // currentAbsorb - damage can be absorbed by shield
        // If need absorb less damage
        if (RemainingDamage < currentAbsorb)
            currentAbsorb = RemainingDamage;

        RemainingDamage -= currentAbsorb;

        // Reduce shield amount
        mod->m_amount-=currentAbsorb;
        if((*i)->DropAuraCharge())
            mod->m_amount = 0;
        // Need remove it later
        if (mod->m_amount<=0)
            existExpired = true;
    }

    // Remove all expired absorb auras
    if (existExpired)
    {
        for(AuraList::const_iterator i = vSchoolAbsorb.begin(); i != vSchoolAbsorb.end();)
        {
            if ((*i)->GetModifier()->m_amount<=0)
            {
                pVictim->RemoveAurasDueToSpell((*i)->GetId());
                i = vSchoolAbsorb.begin();
            }
            else
                ++i;
        }
    }

    // Cast back reflect damage spell
    if (canReflect && reflectSpell)
        pVictim->CastCustomSpell(this,  reflectSpell, &reflectDamage, NULL, NULL, true, NULL, reflectTriggeredBy);

    // absorb by mana cost
    AuraList const& vManaShield = pVictim->GetAurasByType(SPELL_AURA_MANA_SHIELD);
    for(AuraList::const_iterator i = vManaShield.begin(), next; i != vManaShield.end() && RemainingDamage > 0; i = next)
    {
        next = i; ++next;

        // check damage school mask
        if(((*i)->GetModifier()->m_miscvalue & schoolMask)==0)
            continue;

        int32 currentAbsorb;
        if (RemainingDamage >= (*i)->GetModifier()->m_amount)
            currentAbsorb = (*i)->GetModifier()->m_amount;
        else
            currentAbsorb = RemainingDamage;

        if (float manaMultiplier = (*i)->GetSpellProto()->EffectMultipleValue[(*i)->GetEffIndex()])
        {
            if(Player *modOwner = pVictim->GetSpellModOwner())
                modOwner->ApplySpellMod((*i)->GetId(), SPELLMOD_MULTIPLE_VALUE, manaMultiplier);

            int32 maxAbsorb = int32(pVictim->GetPower(POWER_MANA) / manaMultiplier);
            if (currentAbsorb > maxAbsorb)
                currentAbsorb = maxAbsorb;

            int32 manaReduction = int32(currentAbsorb * manaMultiplier);
            pVictim->ApplyPowerMod(POWER_MANA, manaReduction, false);
        }

        (*i)->GetModifier()->m_amount -= currentAbsorb;
        if((*i)->GetModifier()->m_amount <= 0)
        {
            pVictim->RemoveAurasDueToSpell((*i)->GetId());
            next = vManaShield.begin();
        }

        RemainingDamage -= currentAbsorb;
    }

    // effects dependent from full absorb amount
    if (int32 full_absorb = damage - RemainingDamage - *resist)
    {
        Unit::AuraList const& auras = pVictim->GetAurasByType(SPELL_AURA_DUMMY);
        for (Unit::AuraList::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
        {
            SpellEntry const* itr_spellProto = (*itr)->GetSpellProto();

            // Incanter's Absorption
            if (itr_spellProto->SpellFamilyName == SPELLFAMILY_GENERIC &&
                itr_spellProto->SpellIconID == 2941)
            {

                int32 amount = int32(full_absorb * (*itr)->GetModifier()->m_amount / 100);

                // apply normalized part of already accumulated amount in aura
                if (Aura* spdAura = pVictim->GetAura(44413, EFFECT_INDEX_0))
                    amount += spdAura->GetModifier()->m_amount * spdAura->GetAuraDuration() / spdAura->GetAuraMaxDuration();

                // limit 5 health percents
                int32 health_5percent = pVictim->GetMaxHealth()*5/100;
                if(amount > health_5percent)
                    amount = health_5percent;

                // Incanter's Absorption (triggered absorb based spell power, will replace existed if any)
                pVictim->CastCustomSpell(pVictim, 44413, &amount, NULL, NULL, true);
                break;
            }
        }
    }

    // only split damage if not damaging yourself
    if(pVictim != this)
    {
        AuraList const& vSplitDamageFlat = pVictim->GetAurasByType(SPELL_AURA_SPLIT_DAMAGE_FLAT);
        for(AuraList::const_iterator i = vSplitDamageFlat.begin(), next; i != vSplitDamageFlat.end() && RemainingDamage >= 0; i = next)
        {
            next = i; ++next;

            // check damage school mask
            if(((*i)->GetModifier()->m_miscvalue & schoolMask)==0)
                continue;

            // Damage can be splitted only if aura has an alive caster
            Unit *caster = (*i)->GetCaster();
            if(!caster || caster == pVictim || !caster->IsInWorld() || !caster->isAlive())
                continue;

            int32 currentAbsorb;
            if (RemainingDamage >= (*i)->GetModifier()->m_amount)
                currentAbsorb = (*i)->GetModifier()->m_amount;
            else
                currentAbsorb = RemainingDamage;

            RemainingDamage -= currentAbsorb;


            uint32 splitted = currentAbsorb;
            uint32 splitted_absorb = 0;
            DealDamageMods(caster,splitted,&splitted_absorb);

            SendSpellNonMeleeDamageLog(caster, (*i)->GetSpellProto()->Id, splitted, schoolMask, splitted_absorb, 0, false, 0, false);

            CleanDamage cleanDamage = CleanDamage(splitted, BASE_ATTACK, MELEE_HIT_NORMAL);
            DealDamage(caster, splitted, &cleanDamage, DIRECT_DAMAGE, schoolMask, (*i)->GetSpellProto(), false);
        }

        AuraList const& vSplitDamagePct = pVictim->GetAurasByType(SPELL_AURA_SPLIT_DAMAGE_PCT);
        for(AuraList::const_iterator i = vSplitDamagePct.begin(), next; i != vSplitDamagePct.end() && RemainingDamage >= 0; i = next)
        {
            next = i; ++next;

            // check damage school mask
            if(((*i)->GetModifier()->m_miscvalue & schoolMask)==0)
                continue;

            // Damage can be splitted only if aura has an alive caster
            Unit *caster = (*i)->GetCaster();
            if(!caster || caster == pVictim || !caster->IsInWorld() || !caster->isAlive())
                continue;

            uint32 splitted = uint32(RemainingDamage * (*i)->GetModifier()->m_amount / 100.0f);

            RemainingDamage -=  int32(splitted);

            uint32 split_absorb = 0;
            DealDamageMods(caster,splitted,&split_absorb);

            SendSpellNonMeleeDamageLog(caster, (*i)->GetSpellProto()->Id, splitted, schoolMask, split_absorb, 0, false, 0, false);

            CleanDamage cleanDamage = CleanDamage(splitted, BASE_ATTACK, MELEE_HIT_NORMAL);
            DealDamage(caster, splitted, &cleanDamage, DIRECT_DAMAGE, schoolMask, (*i)->GetSpellProto(), false);
        }
    }

    // Apply death prevention spells effects
    if (preventDeathSpell && RemainingDamage >= (int32)pVictim->GetHealth())
    {
        switch(preventDeathSpell->SpellFamilyName)
        {
            // Cheat Death
            case SPELLFAMILY_ROGUE:
            {
                // Cheat Death
                if (preventDeathSpell->SpellIconID == 2109)
                {
                    pVictim->CastSpell(pVictim,31231,true);
                    ((Player*)pVictim)->AddSpellCooldown(31231,0,time(NULL)+60);
                    // with health > 10% lost health until health==10%, in other case no losses
                    uint32 health10 = pVictim->GetMaxHealth()/10;
                    RemainingDamage = pVictim->GetHealth() > health10 ? pVictim->GetHealth() - health10 : 0;
                }
                break;
            }
            // Guardian Spirit
            case SPELLFAMILY_PRIEST:
            {
                // Guardian Spirit
                if (preventDeathSpell->SpellIconID == 2873)
                {
                    int32 healAmount = pVictim->GetMaxHealth() * preventDeathAmount / 100;
                    pVictim->CastCustomSpell(pVictim, 48153, &healAmount, NULL, NULL, true);
                    pVictim->RemoveAurasDueToSpell(preventDeathSpell->Id);
                    RemainingDamage = 0;
                }
                break;
            }
        }
    }

    *absorb = damage - RemainingDamage - *resist;
}

void Unit::AttackerStateUpdate (Unit *pVictim, WeaponAttackType attType, bool extra )
{
    if(hasUnitState(UNIT_STAT_CAN_NOT_REACT) || HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PACIFIED) )
        return;

    if (!pVictim->isAlive())
        return;

    if(IsNonMeleeSpellCasted(false))
        return;

    uint32 hitInfo;
    if (attType == BASE_ATTACK)
        hitInfo = HITINFO_NORMALSWING2;
    else if (attType == OFF_ATTACK)
        hitInfo = HITINFO_LEFTSWING;
    else
        return;                                             // ignore ranged case

    uint32 extraAttacks = m_extraAttacks;

    // melee attack spell casted at main hand attack only
    if (attType == BASE_ATTACK && m_currentSpells[CURRENT_MELEE_SPELL])
    {
        m_currentSpells[CURRENT_MELEE_SPELL]->cast();

        // not recent extra attack only at any non extra attack (melee spell case)
        if(!extra && extraAttacks)
        {
            while(m_extraAttacks)
            {
                AttackerStateUpdate(pVictim, BASE_ATTACK, true);
                if(m_extraAttacks > 0)
                    --m_extraAttacks;
            }
        }
        return;
    }

    // attack can be redirected to another target
    pVictim = SelectMagnetTarget(pVictim);

    CalcDamageInfo damageInfo;
    CalculateMeleeDamage(pVictim, 0, &damageInfo, attType);
    // Send log damage message to client
    DealDamageMods(pVictim,damageInfo.damage,&damageInfo.absorb);
    SendAttackStateUpdate(&damageInfo);
    ProcDamageAndSpell(damageInfo.target, damageInfo.procAttacker, damageInfo.procVictim, damageInfo.procEx, damageInfo.damage, damageInfo.attackType);
    DealMeleeDamage(&damageInfo,true);

    if (GetTypeId() == TYPEID_PLAYER)
        DEBUG_LOG("AttackerStateUpdate: (Player) %u attacked %u (TypeId: %u) for %u dmg, absorbed %u, blocked %u, resisted %u.",
            GetGUIDLow(), pVictim->GetGUIDLow(), pVictim->GetTypeId(), damageInfo.damage, damageInfo.absorb, damageInfo.blocked_amount, damageInfo.resist);
    else
        DEBUG_LOG("AttackerStateUpdate: (NPC)    %u attacked %u (TypeId: %u) for %u dmg, absorbed %u, blocked %u, resisted %u.",
            GetGUIDLow(), pVictim->GetGUIDLow(), pVictim->GetTypeId(), damageInfo.damage, damageInfo.absorb, damageInfo.blocked_amount, damageInfo.resist);

    // if damage pVictim call AI reaction
    if(pVictim->GetTypeId()==TYPEID_UNIT && ((Creature*)pVictim)->AI())
        ((Creature*)pVictim)->AI()->AttackedBy(this);

    // extra attack only at any non extra attack (normal case)
    if(!extra && extraAttacks)
    {
        while(m_extraAttacks)
        {
            AttackerStateUpdate(pVictim, BASE_ATTACK, true);
            if(m_extraAttacks > 0)
                --m_extraAttacks;
        }
    }
}

MeleeHitOutcome Unit::RollMeleeOutcomeAgainst(const Unit *pVictim, WeaponAttackType attType) const
{
    // This is only wrapper

    // Miss chance based on melee
    float miss_chance = MeleeMissChanceCalc(pVictim, attType);

    // Critical hit chance
    float crit_chance = GetUnitCriticalChance(attType, pVictim);

    // stunned target cannot dodge and this is check in GetUnitDodgeChance() (returned 0 in this case)
    float dodge_chance = pVictim->GetUnitDodgeChance();
    float block_chance = pVictim->GetUnitBlockChance();
    float parry_chance = pVictim->GetUnitParryChance();

    // Useful if want to specify crit & miss chances for melee, else it could be removed
    DEBUG_LOG("MELEE OUTCOME: miss %f crit %f dodge %f parry %f block %f", miss_chance,crit_chance,dodge_chance,parry_chance,block_chance);

    return RollMeleeOutcomeAgainst(pVictim, attType, int32(crit_chance*100), int32(miss_chance*100), int32(dodge_chance*100),int32(parry_chance*100),int32(block_chance*100));
}

MeleeHitOutcome Unit::RollMeleeOutcomeAgainst (const Unit *pVictim, WeaponAttackType attType, int32 crit_chance, int32 miss_chance, int32 dodge_chance, int32 parry_chance, int32 block_chance) const
{
    if(pVictim->GetTypeId()==TYPEID_UNIT && ((Creature*)pVictim)->IsInEvadeMode())
        return MELEE_HIT_EVADE;

    int32 attackerMaxSkillValueForLevel = GetMaxSkillValueForLevel(pVictim);
    int32 victimMaxSkillValueForLevel = pVictim->GetMaxSkillValueForLevel(this);

    int32 attackerWeaponSkill = GetWeaponSkillValue(attType,pVictim);
    int32 victimDefenseSkill = pVictim->GetDefenseSkillValue(this);

    // bonus from skills is 0.04%
    int32    skillBonus  = 4 * ( attackerWeaponSkill - victimMaxSkillValueForLevel );
    int32    sum = 0, tmp = 0;
    int32    roll = urand (0, 10000);

    DEBUG_LOG ("RollMeleeOutcomeAgainst: skill bonus of %d for attacker", skillBonus);
    DEBUG_LOG ("RollMeleeOutcomeAgainst: rolled %d, miss %d, dodge %d, parry %d, block %d, crit %d",
        roll, miss_chance, dodge_chance, parry_chance, block_chance, crit_chance);

    tmp = miss_chance;

    if (tmp > 0 && roll < (sum += tmp ))
    {
        DEBUG_LOG ("RollMeleeOutcomeAgainst: MISS");
        return MELEE_HIT_MISS;
    }

    // always crit against a sitting target (except 0 crit chance)
    if( pVictim->GetTypeId() == TYPEID_PLAYER && crit_chance > 0 && !pVictim->IsStandState() )
    {
        DEBUG_LOG ("RollMeleeOutcomeAgainst: CRIT (sitting victim)");
        return MELEE_HIT_CRIT;
    }

    // Dodge chance

    // only players can't dodge if attacker is behind
    if (pVictim->GetTypeId() == TYPEID_PLAYER && !pVictim->HasInArc(M_PI_F,this))
    {
        DEBUG_LOG ("RollMeleeOutcomeAgainst: attack came from behind and victim was a player.");
    }
    else
    {
        // Reduce dodge chance by attacker expertise rating
        if (GetTypeId() == TYPEID_PLAYER)
            dodge_chance -= int32(((Player*)this)->GetExpertiseDodgeOrParryReduction(attType)*100);
        else
            dodge_chance -= GetTotalAuraModifier(SPELL_AURA_MOD_EXPERTISE)*25;

        // Modify dodge chance by attacker SPELL_AURA_MOD_COMBAT_RESULT_CHANCE
        dodge_chance+= GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_COMBAT_RESULT_CHANCE, VICTIMSTATE_DODGE);

        tmp = dodge_chance;
        if (   (tmp > 0)                                        // check if unit _can_ dodge
            && ((tmp -= skillBonus) > 0)
            && roll < (sum += tmp))
        {
            DEBUG_LOG ("RollMeleeOutcomeAgainst: DODGE <%d, %d)", sum-tmp, sum);
            return MELEE_HIT_DODGE;
        }
    }

    // parry & block chances

    // check if attack comes from behind, nobody can parry or block if attacker is behind
    if (!pVictim->HasInArc(M_PI_F,this))
    {
        DEBUG_LOG ("RollMeleeOutcomeAgainst: attack came from behind.");
    }
    else
    {
        // Reduce parry chance by attacker expertise rating
        if (GetTypeId() == TYPEID_PLAYER)
            parry_chance-= int32(((Player*)this)->GetExpertiseDodgeOrParryReduction(attType)*100);
        else
            parry_chance -= GetTotalAuraModifier(SPELL_AURA_MOD_EXPERTISE)*25;

        if(pVictim->GetTypeId()==TYPEID_PLAYER || !(((Creature*)pVictim)->GetCreatureInfo()->flags_extra & CREATURE_FLAG_EXTRA_NO_PARRY) )
        {
            int32 tmp2 = int32(parry_chance);
            if (   (tmp2 > 0)                                   // check if unit _can_ parry
                && ((tmp2 -= skillBonus) > 0)
                && (roll < (sum += tmp2)))
            {
                DEBUG_LOG ("RollMeleeOutcomeAgainst: PARRY <%d, %d)", sum-tmp2, sum);
                return MELEE_HIT_PARRY;
            }
        }

        if(pVictim->GetTypeId()==TYPEID_PLAYER || !(((Creature*)pVictim)->GetCreatureInfo()->flags_extra & CREATURE_FLAG_EXTRA_NO_BLOCK) )
        {
            tmp = block_chance;
            if (   (tmp > 0)                                    // check if unit _can_ block
                && ((tmp -= skillBonus) > 0)
                && (roll < (sum += tmp)))
            {
                DEBUG_LOG ("RollMeleeOutcomeAgainst: BLOCK <%d, %d)", sum-tmp, sum);
                return MELEE_HIT_BLOCK;
            }
        }
    }

    // Critical chance
    tmp = crit_chance;

    if (tmp > 0 && roll < (sum += tmp))
    {
        DEBUG_LOG ("RollMeleeOutcomeAgainst: CRIT <%d, %d)", sum-tmp, sum);
        return MELEE_HIT_CRIT;
    }

    // Max 40% chance to score a glancing blow against mobs that are higher level (can do only players and pets and not with ranged weapon)
    if( attType != RANGED_ATTACK &&
        (GetTypeId() == TYPEID_PLAYER || ((Creature*)this)->isPet()) &&
        pVictim->GetTypeId() != TYPEID_PLAYER && !((Creature*)pVictim)->isPet() &&
        getLevel() < pVictim->getLevelForTarget(this) )
    {
        // cap possible value (with bonuses > max skill)
        int32 skill = attackerWeaponSkill;
        int32 maxskill = attackerMaxSkillValueForLevel;
        skill = (skill > maxskill) ? maxskill : skill;

        tmp = (10 + (victimDefenseSkill - skill)) * 100;
        tmp = tmp > 4000 ? 4000 : tmp;
        if (roll < (sum += tmp))
        {
            DEBUG_LOG ("RollMeleeOutcomeAgainst: GLANCING <%d, %d)", sum-4000, sum);
            return MELEE_HIT_GLANCING;
        }
    }

    // mobs can score crushing blows if they're 4 or more levels above victim
    if (getLevelForTarget(pVictim) >= pVictim->getLevelForTarget(this) + 4 &&
        // can be from by creature (if can) or from controlled player that considered as creature
        (GetTypeId()!=TYPEID_PLAYER && !((Creature*)this)->isPet() &&
        !(((Creature*)this)->GetCreatureInfo()->flags_extra & CREATURE_FLAG_EXTRA_NO_CRUSH) ||
        GetTypeId()==TYPEID_PLAYER && GetCharmerOrOwnerGUID()))
    {
        // when their weapon skill is 15 or more above victim's defense skill
        tmp = victimDefenseSkill;
        int32 tmpmax = victimMaxSkillValueForLevel;
        // having defense above your maximum (from items, talents etc.) has no effect
        tmp = tmp > tmpmax ? tmpmax : tmp;
        // tmp = mob's level * 5 - player's current defense skill
        tmp = attackerMaxSkillValueForLevel - tmp;
        if(tmp >= 15)
        {
            // add 2% chance per lacking skill point, min. is 15%
            tmp = tmp * 200 - 1500;
            if (roll < (sum += tmp))
            {
                DEBUG_LOG ("RollMeleeOutcomeAgainst: CRUSHING <%d, %d)", sum-tmp, sum);
                return MELEE_HIT_CRUSHING;
            }
        }
    }

    DEBUG_LOG ("RollMeleeOutcomeAgainst: NORMAL");
    return MELEE_HIT_NORMAL;
}

uint32 Unit::CalculateDamage (WeaponAttackType attType, bool normalized)
{
    float min_damage, max_damage;

    if (normalized && GetTypeId()==TYPEID_PLAYER)
        ((Player*)this)->CalculateMinMaxDamage(attType,normalized,min_damage, max_damage);
    else
    {
        switch (attType)
        {
            case RANGED_ATTACK:
                min_damage = GetFloatValue(UNIT_FIELD_MINRANGEDDAMAGE);
                max_damage = GetFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE);
                break;
            case BASE_ATTACK:
                min_damage = GetFloatValue(UNIT_FIELD_MINDAMAGE);
                max_damage = GetFloatValue(UNIT_FIELD_MAXDAMAGE);
                break;
            case OFF_ATTACK:
                min_damage = GetFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE);
                max_damage = GetFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE);
                break;
                // Just for good manner
            default:
                min_damage = 0.0f;
                max_damage = 0.0f;
                break;
        }
    }

    if (min_damage > max_damage)
    {
        std::swap(min_damage,max_damage);
    }

    if(max_damage == 0.0f)
        max_damage = 5.0f;

    return urand((uint32)min_damage, (uint32)max_damage);
}

float Unit::CalculateLevelPenalty(SpellEntry const* spellProto) const
{
    if(spellProto->spellLevel <= 0)
        return 1.0f;

    float LvlPenalty = 0.0f;

    if(spellProto->spellLevel < 20)
        LvlPenalty = 20.0f - spellProto->spellLevel * 3.75f;
    float LvlFactor = (float(spellProto->spellLevel) + 6.0f) / float(getLevel());
    if(LvlFactor > 1.0f)
        LvlFactor = 1.0f;

    return (100.0f - LvlPenalty) * LvlFactor / 100.0f;
}

void Unit::SendMeleeAttackStart(Unit* pVictim)
{
    WorldPacket data( SMSG_ATTACKSTART, 8 + 8 );
    data << uint64(GetGUID());
    data << uint64(pVictim->GetGUID());

    SendMessageToSet(&data, true);
    DEBUG_LOG( "WORLD: Sent SMSG_ATTACKSTART" );
}

void Unit::SendMeleeAttackStop(Unit* victim)
{
    if(!victim)
        return;

    WorldPacket data( SMSG_ATTACKSTOP, (4+16) );            // we guess size
    data.append(GetPackGUID());
    data.append(victim->GetPackGUID());                     // can be 0x00...
    data << uint32(0);                                      // can be 0x1
    SendMessageToSet(&data, true);
    sLog.outDetail("%s %u stopped attacking %s %u", (GetTypeId()==TYPEID_PLAYER ? "player" : "creature"), GetGUIDLow(), (victim->GetTypeId()==TYPEID_PLAYER ? "player" : "creature"),victim->GetGUIDLow());

    /*if(victim->GetTypeId() == TYPEID_UNIT)
    ((Creature*)victim)->AI().EnterEvadeMode(this);*/
}

bool Unit::isSpellBlocked(Unit *pVictim, SpellEntry const * /*spellProto*/, WeaponAttackType attackType)
{
    if (pVictim->HasInArc(M_PI_F,this))
    {
        /* Currently not exist spells with ignore block
        // Ignore combat result aura (parry/dodge check on prepare)
        AuraList const& ignore = GetAurasByType(SPELL_AURA_IGNORE_COMBAT_RESULT);
        for(AuraList::const_iterator i = ignore.begin(); i != ignore.end(); ++i)
        {
            if (!(*i)->isAffectedOnSpell(spellProto))
                continue;
            if ((*i)->GetModifier()->m_miscvalue == )
                return false;
        }
        */

        // Check creatures flags_extra for disable block
        if(pVictim->GetTypeId()==TYPEID_UNIT &&
           ((Creature*)pVictim)->GetCreatureInfo()->flags_extra & CREATURE_FLAG_EXTRA_NO_BLOCK )
                return false;

        float blockChance = pVictim->GetUnitBlockChance();
        blockChance += (int32(GetWeaponSkillValue(attackType)) - int32(pVictim->GetMaxSkillValueForLevel()))*0.04f;
        if (roll_chance_f(blockChance))
            return true;
    }
    return false;
}

// Melee based spells can be miss, parry or dodge on this step
// Crit or block - determined on damage calculation phase! (and can be both in some time)
float Unit::MeleeSpellMissChance(Unit *pVictim, WeaponAttackType attType, int32 skillDiff, SpellEntry const *spell)
{
    // Calculate hit chance (more correct for chance mod)
    int32 HitChance;

    // PvP - PvE melee chances
    int32 lchance = pVictim->GetTypeId() == TYPEID_PLAYER ? 5 : 7;
    int32 leveldif = pVictim->getLevelForTarget(this) - getLevelForTarget(pVictim);
    if(leveldif < 3)
        HitChance = 95 - leveldif;
    else
        HitChance = 93 - (leveldif - 2) * lchance;

    // Hit chance depends from victim auras
    if(attType == RANGED_ATTACK)
        HitChance += pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_RANGED_HIT_CHANCE);
    else
        HitChance += pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_MELEE_HIT_CHANCE);

    // Spellmod from SPELLMOD_RESIST_MISS_CHANCE
    if(Player *modOwner = GetSpellModOwner())
        modOwner->ApplySpellMod(spell->Id, SPELLMOD_RESIST_MISS_CHANCE, HitChance);

    // Miss = 100 - hit
    float miss_chance= 100.0f - HitChance;

    // Bonuses from attacker aura and ratings
    if (attType == RANGED_ATTACK)
        miss_chance -= m_modRangedHitChance;
    else
        miss_chance -= m_modMeleeHitChance;

    // bonus from skills is 0.04%
    miss_chance -= skillDiff * 0.04f;

    // Limit miss chance from 0 to 60%
    if (miss_chance < 0.0f)
        return 0.0f;
    if (miss_chance > 60.0f)
        return 60.0f;
    return miss_chance;
}

// Melee based spells hit result calculations
SpellMissInfo Unit::MeleeSpellHitResult(Unit *pVictim, SpellEntry const *spell)
{
    WeaponAttackType attType = BASE_ATTACK;

    if (spell->DmgClass == SPELL_DAMAGE_CLASS_RANGED)
        attType = RANGED_ATTACK;

    // bonus from skills is 0.04% per skill Diff
    int32 attackerWeaponSkill = int32(GetWeaponSkillValue(attType,pVictim));
    int32 skillDiff = attackerWeaponSkill - int32(pVictim->GetMaxSkillValueForLevel(this));
    int32 fullSkillDiff = attackerWeaponSkill - int32(pVictim->GetDefenseSkillValue(this));

    uint32 roll = urand (0, 10000);

    uint32 missChance = uint32(MeleeSpellMissChance(pVictim, attType, fullSkillDiff, spell)*100.0f);
    // Roll miss
    uint32 tmp = missChance;
    if (roll < tmp)
        return SPELL_MISS_MISS;

    // Chance resist mechanic (select max value from every mechanic spell effect)
    int32 resist_mech = 0;
    // Get effects mechanic and chance
    for(int eff = 0; eff < MAX_EFFECT_INDEX; ++eff)
    {
        int32 effect_mech = GetEffectMechanic(spell, SpellEffectIndex(eff));
        if (effect_mech)
        {
            int32 temp = pVictim->GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_MECHANIC_RESISTANCE, effect_mech);
            if (resist_mech < temp*100)
                resist_mech = temp*100;
        }
    }
    // Roll chance
    tmp += resist_mech;
    if (roll < tmp)
        return SPELL_MISS_RESIST;

    bool canDodge = true;
    bool canParry = true;

    // Same spells cannot be parry/dodge
    if (spell->Attributes & SPELL_ATTR_IMPOSSIBLE_DODGE_PARRY_BLOCK)
        return SPELL_MISS_NONE;

    // Ranged attack cannot be parry/dodge only deflect
    if (attType == RANGED_ATTACK)
    {
        // only if in front
        if (pVictim->HasInArc(M_PI_F,this))
        {
            int32 deflect_chance = pVictim->GetTotalAuraModifier(SPELL_AURA_DEFLECT_SPELLS)*100;
            tmp+=deflect_chance;
            if (roll < tmp)
                return SPELL_MISS_DEFLECT;
        }
        return SPELL_MISS_NONE;
    }

    // Check for attack from behind
    if (!pVictim->HasInArc(M_PI_F,this))
    {
        // Can`t dodge from behind in PvP (but its possible in PvE)
        if (GetTypeId() == TYPEID_PLAYER && pVictim->GetTypeId() == TYPEID_PLAYER)
            canDodge = false;
        // Can`t parry
        canParry = false;
    }
    // Check creatures flags_extra for disable parry
    if(pVictim->GetTypeId()==TYPEID_UNIT)
    {
        uint32 flagEx = ((Creature*)pVictim)->GetCreatureInfo()->flags_extra;
        if( flagEx & CREATURE_FLAG_EXTRA_NO_PARRY )
            canParry = false;
    }
    // Ignore combat result aura
    AuraList const& ignore = GetAurasByType(SPELL_AURA_IGNORE_COMBAT_RESULT);
    for(AuraList::const_iterator i = ignore.begin(); i != ignore.end(); ++i)
    {
        if (!(*i)->isAffectedOnSpell(spell))
            continue;
        switch((*i)->GetModifier()->m_miscvalue)
        {
            case MELEE_HIT_DODGE: canDodge = false; break;
            case MELEE_HIT_BLOCK: break; // Block check in hit step
            case MELEE_HIT_PARRY: canParry = false; break;
            default:
                DEBUG_LOG("Spell %u SPELL_AURA_IGNORE_COMBAT_RESULT have unhandled state %d", (*i)->GetId(), (*i)->GetModifier()->m_miscvalue);
                break;
        }
    }

    if (canDodge)
    {
        // Roll dodge
        int32 dodgeChance = int32(pVictim->GetUnitDodgeChance()*100.0f) - skillDiff * 4;
        // Reduce enemy dodge chance by SPELL_AURA_MOD_COMBAT_RESULT_CHANCE
        dodgeChance+= GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_COMBAT_RESULT_CHANCE, VICTIMSTATE_DODGE)*100;
        // Reduce dodge chance by attacker expertise rating
        if (GetTypeId() == TYPEID_PLAYER)
            dodgeChance-=int32(((Player*)this)->GetExpertiseDodgeOrParryReduction(attType) * 100.0f);
        else
            dodgeChance -= GetTotalAuraModifier(SPELL_AURA_MOD_EXPERTISE)*25;
        if (dodgeChance < 0)
            dodgeChance = 0;

        tmp += dodgeChance;
        if (roll < tmp)
            return SPELL_MISS_DODGE;
    }

    if (canParry)
    {
        // Roll parry
        int32 parryChance = int32(pVictim->GetUnitParryChance()*100.0f)  - skillDiff * 4;
        // Reduce parry chance by attacker expertise rating
        if (GetTypeId() == TYPEID_PLAYER)
            parryChance-=int32(((Player*)this)->GetExpertiseDodgeOrParryReduction(attType) * 100.0f);
        else
            parryChance -= GetTotalAuraModifier(SPELL_AURA_MOD_EXPERTISE)*25;
        if (parryChance < 0)
            parryChance = 0;

        tmp += parryChance;
        if (roll < tmp)
            return SPELL_MISS_PARRY;
    }

    return SPELL_MISS_NONE;
}

// TODO need use unit spell resistances in calculations
SpellMissInfo Unit::MagicSpellHitResult(Unit *pVictim, SpellEntry const *spell)
{
    // Can`t miss on dead target (on skinning for example)
    if (!pVictim->isAlive())
        return SPELL_MISS_NONE;

    SpellSchoolMask schoolMask = GetSpellSchoolMask(spell);
    // PvP - PvE spell misschances per leveldif > 2
    int32 lchance = pVictim->GetTypeId() == TYPEID_PLAYER ? 7 : 11;
    int32 leveldif = int32(pVictim->getLevelForTarget(this)) - int32(getLevelForTarget(pVictim));

    // Base hit chance from attacker and victim levels
    int32 modHitChance;
    if(leveldif < 3)
        modHitChance = 96 - leveldif;
    else
        modHitChance = 94 - (leveldif - 2) * lchance;

    // Spellmod from SPELLMOD_RESIST_MISS_CHANCE
    if(Player *modOwner = GetSpellModOwner())
        modOwner->ApplySpellMod(spell->Id, SPELLMOD_RESIST_MISS_CHANCE, modHitChance);
    // Increase from attacker SPELL_AURA_MOD_INCREASES_SPELL_PCT_TO_HIT auras
    modHitChance+=GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_INCREASES_SPELL_PCT_TO_HIT, schoolMask);
    // Chance hit from victim SPELL_AURA_MOD_ATTACKER_SPELL_HIT_CHANCE auras
    modHitChance+= pVictim->GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_ATTACKER_SPELL_HIT_CHANCE, schoolMask);
    // Reduce spell hit chance for Area of effect spells from victim SPELL_AURA_MOD_AOE_AVOIDANCE aura
    if (IsAreaOfEffectSpell(spell))
        modHitChance-=pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_AOE_AVOIDANCE);
    // Reduce spell hit chance for dispel mechanic spells from victim SPELL_AURA_MOD_DISPEL_RESIST
    if (IsDispelSpell(spell))
        modHitChance-=pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_DISPEL_RESIST);
    // Chance resist mechanic (select max value from every mechanic spell effect)
    int32 resist_mech = 0;
    // Get effects mechanic and chance
    for(int eff = 0; eff < MAX_EFFECT_INDEX; ++eff)
    {
        int32 effect_mech = GetEffectMechanic(spell, SpellEffectIndex(eff));
        if (effect_mech)
        {
            int32 temp = pVictim->GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_MECHANIC_RESISTANCE, effect_mech);
            if (resist_mech < temp)
                resist_mech = temp;
        }
    }
    // Apply mod
    modHitChance-=resist_mech;

    // Chance resist debuff
    modHitChance-=pVictim->GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_DEBUFF_RESISTANCE, int32(spell->Dispel));

    int32 HitChance = modHitChance * 100;
    // Increase hit chance from attacker SPELL_AURA_MOD_SPELL_HIT_CHANCE and attacker ratings
    HitChance += int32(m_modSpellHitChance*100.0f);

    // Decrease hit chance from victim rating bonus
    if (pVictim->GetTypeId()==TYPEID_PLAYER)
        HitChance -= int32(((Player*)pVictim)->GetRatingBonusValue(CR_HIT_TAKEN_SPELL)*100.0f);

    if (HitChance <  100) HitChance =  100;
    if (HitChance > 10000) HitChance = 10000;

    int32 tmp = 10000 - HitChance;

    int32 rand = irand(0,10000);

    if (rand < tmp)
        return SPELL_MISS_MISS;

    // cast by caster in front of victim
    if (pVictim->HasInArc(M_PI_F,this))
    {
        int32 deflect_chance = pVictim->GetTotalAuraModifier(SPELL_AURA_DEFLECT_SPELLS)*100;
        tmp+=deflect_chance;
        if (rand < tmp)
            return SPELL_MISS_DEFLECT;
    }

    return SPELL_MISS_NONE;
}

// Calculate spell hit result can be:
// Every spell can: Evade/Immune/Reflect/Sucesful hit
// For melee based spells:
//   Miss
//   Dodge
//   Parry
// For spells
//   Resist
SpellMissInfo Unit::SpellHitResult(Unit *pVictim, SpellEntry const *spell, bool CanReflect)
{
    // Return evade for units in evade mode
    if (pVictim->GetTypeId()==TYPEID_UNIT && ((Creature*)pVictim)->IsInEvadeMode())
        return SPELL_MISS_EVADE;

    // Check for immune
    if (pVictim->IsImmunedToSpell(spell))
        return SPELL_MISS_IMMUNE;

    // All positive spells can`t miss
    // TODO: client not show miss log for this spells - so need find info for this in dbc and use it!
    if (IsPositiveSpell(spell->Id))
        return SPELL_MISS_NONE;

    // Check for immune
    if (pVictim->IsImmunedToDamage(GetSpellSchoolMask(spell)))
        return SPELL_MISS_IMMUNE;

    // Try victim reflect spell
    if (CanReflect)
    {
        int32 reflectchance = pVictim->GetTotalAuraModifier(SPELL_AURA_REFLECT_SPELLS);
        Unit::AuraList const& mReflectSpellsSchool = pVictim->GetAurasByType(SPELL_AURA_REFLECT_SPELLS_SCHOOL);
        for(Unit::AuraList::const_iterator i = mReflectSpellsSchool.begin(); i != mReflectSpellsSchool.end(); ++i)
            if((*i)->GetModifier()->m_miscvalue & GetSpellSchoolMask(spell))
                reflectchance += (*i)->GetModifier()->m_amount;
        if (reflectchance > 0 && roll_chance_i(reflectchance))
        {
            // Start triggers for remove charges if need (trigger only for victim, and mark as active spell)
            ProcDamageAndSpell(pVictim, PROC_FLAG_NONE, PROC_FLAG_TAKEN_NEGATIVE_SPELL_HIT, PROC_EX_REFLECT, 1, BASE_ATTACK, spell);
            return SPELL_MISS_REFLECT;
        }
    }

    switch (spell->DmgClass)
    {
        case SPELL_DAMAGE_CLASS_RANGED:
        case SPELL_DAMAGE_CLASS_MELEE:
            return MeleeSpellHitResult(pVictim, spell);
        case SPELL_DAMAGE_CLASS_NONE:
        case SPELL_DAMAGE_CLASS_MAGIC:
            return MagicSpellHitResult(pVictim, spell);
    }
    return SPELL_MISS_NONE;
}

float Unit::MeleeMissChanceCalc(const Unit *pVictim, WeaponAttackType attType) const
{
    if(!pVictim)
        return 0.0f;

    // Base misschance 5%
    float misschance = 5.0f;

    // DualWield - Melee spells and physical dmg spells - 5% , white damage 24%
    if (haveOffhandWeapon() && attType != RANGED_ATTACK)
    {
        bool isNormal = false;
        for (uint32 i = CURRENT_FIRST_NON_MELEE_SPELL; i < CURRENT_MAX_SPELL; ++i)
        {
            if( m_currentSpells[i] && (GetSpellSchoolMask(m_currentSpells[i]->m_spellInfo) & SPELL_SCHOOL_MASK_NORMAL) )
            {
                isNormal = true;
                break;
            }
        }
        if (isNormal || m_currentSpells[CURRENT_MELEE_SPELL])
            misschance = 5.0f;
        else
            misschance = 24.0f;
    }

    // PvP : PvE melee misschances per leveldif > 2
    int32 chance = pVictim->GetTypeId() == TYPEID_PLAYER ? 5 : 7;

    int32 leveldif = int32(pVictim->getLevelForTarget(this)) - int32(getLevelForTarget(pVictim));
    if(leveldif < 0)
        leveldif = 0;

    // Hit chance from attacker based on ratings and auras
    float m_modHitChance;
    if (attType == RANGED_ATTACK)
        m_modHitChance = m_modRangedHitChance;
    else
        m_modHitChance = m_modMeleeHitChance;

    if(leveldif < 3)
        misschance += (leveldif - m_modHitChance);
    else
        misschance += ((leveldif - 2) * chance - m_modHitChance);

    // Hit chance for victim based on ratings
    if (pVictim->GetTypeId()==TYPEID_PLAYER)
    {
        if (attType == RANGED_ATTACK)
            misschance += ((Player*)pVictim)->GetRatingBonusValue(CR_HIT_TAKEN_RANGED);
        else
            misschance += ((Player*)pVictim)->GetRatingBonusValue(CR_HIT_TAKEN_MELEE);
    }

    // Modify miss chance by victim auras
    if(attType == RANGED_ATTACK)
        misschance -= pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_RANGED_HIT_CHANCE);
    else
        misschance -= pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_MELEE_HIT_CHANCE);

    // Modify miss chance from skill difference ( bonus from skills is 0.04% )
    int32 skillBonus = int32(GetWeaponSkillValue(attType,pVictim)) - int32(pVictim->GetDefenseSkillValue(this));
    misschance -= skillBonus * 0.04f;

    // Limit miss chance from 0 to 60%
    if ( misschance < 0.0f)
        return 0.0f;
    if ( misschance > 60.0f)
        return 60.0f;

    return misschance;
}

uint32 Unit::GetDefenseSkillValue(Unit const* target) const
{
    if(GetTypeId() == TYPEID_PLAYER)
    {
        // in PvP use full skill instead current skill value
        uint32 value = (target && target->GetTypeId() == TYPEID_PLAYER)
            ? ((Player*)this)->GetMaxSkillValue(SKILL_DEFENSE)
            : ((Player*)this)->GetSkillValue(SKILL_DEFENSE);
        value += uint32(((Player*)this)->GetRatingBonusValue(CR_DEFENSE_SKILL));
        return value;
    }
    else
        return GetUnitMeleeSkill(target);
}

float Unit::GetUnitDodgeChance() const
{
    if(hasUnitState(UNIT_STAT_STUNNED))
        return 0.0f;
    if( GetTypeId() == TYPEID_PLAYER )
        return GetFloatValue(PLAYER_DODGE_PERCENTAGE);
    else
    {
        if(((Creature const*)this)->isTotem())
            return 0.0f;
        else
        {
            float dodge = 5.0f;
            dodge += GetTotalAuraModifier(SPELL_AURA_MOD_DODGE_PERCENT);
            return dodge > 0.0f ? dodge : 0.0f;
        }
    }
}

float Unit::GetUnitParryChance() const
{
    if ( IsNonMeleeSpellCasted(false) || hasUnitState(UNIT_STAT_STUNNED))
        return 0.0f;

    float chance = 0.0f;

    if(GetTypeId() == TYPEID_PLAYER)
    {
        Player const* player = (Player const*)this;
        if(player->CanParry() )
        {
            Item *tmpitem = player->GetWeaponForAttack(BASE_ATTACK,true,true);
            if(!tmpitem)
                tmpitem = player->GetWeaponForAttack(OFF_ATTACK,true,true);

            if(tmpitem)
                chance = GetFloatValue(PLAYER_PARRY_PERCENTAGE);
        }
    }
    else if(GetTypeId() == TYPEID_UNIT)
    {
        if(GetCreatureType() == CREATURE_TYPE_HUMANOID)
        {
            chance = 5.0f;
            chance += GetTotalAuraModifier(SPELL_AURA_MOD_PARRY_PERCENT);
        }
    }

    return chance > 0.0f ? chance : 0.0f;
}

float Unit::GetUnitBlockChance() const
{
    if ( IsNonMeleeSpellCasted(false) || hasUnitState(UNIT_STAT_STUNNED))
        return 0.0f;

    if(GetTypeId() == TYPEID_PLAYER)
    {
        Player const* player = (Player const*)this;
        if(player->CanBlock() )
        {
            Item *tmpitem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
            if(tmpitem && !tmpitem->IsBroken() && tmpitem->GetProto()->Block)
                return GetFloatValue(PLAYER_BLOCK_PERCENTAGE);
        }
        // is player but has no block ability or no not broken shield equipped
        return 0.0f;
    }
    else
    {
        if(((Creature const*)this)->isTotem())
            return 0.0f;
        else
        {
            float block = 5.0f;
            block += GetTotalAuraModifier(SPELL_AURA_MOD_BLOCK_PERCENT);
            return block > 0.0f ? block : 0.0f;
        }
    }
}

float Unit::GetUnitCriticalChance(WeaponAttackType attackType, const Unit *pVictim) const
{
    float crit;

    if(GetTypeId() == TYPEID_PLAYER)
    {
        switch(attackType)
        {
            case BASE_ATTACK:
                crit = GetFloatValue( PLAYER_CRIT_PERCENTAGE );
                break;
            case OFF_ATTACK:
                crit = GetFloatValue( PLAYER_OFFHAND_CRIT_PERCENTAGE );
                break;
            case RANGED_ATTACK:
                crit = GetFloatValue( PLAYER_RANGED_CRIT_PERCENTAGE );
                break;
                // Just for good manner
            default:
                crit = 0.0f;
                break;
        }
    }
    else
    {
        crit = 5.0f;
        crit += GetTotalAuraModifier(SPELL_AURA_MOD_CRIT_PERCENT);
    }

    // flat aura mods
    if(attackType == RANGED_ATTACK)
        crit += pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_RANGED_CRIT_CHANCE);
    else
        crit += pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_MELEE_CRIT_CHANCE);

    crit += pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_SPELL_AND_WEAPON_CRIT_CHANCE);

    // reduce crit chance from Rating for players
    if (attackType != RANGED_ATTACK)
        crit -= pVictim->GetMeleeCritChanceReduction();
    else
        crit -= pVictim->GetRangedCritChanceReduction();

    // Apply crit chance from defence skill
    crit += (int32(GetMaxSkillValueForLevel(pVictim)) - int32(pVictim->GetDefenseSkillValue(this))) * 0.04f;

    if (crit < 0.0f)
        crit = 0.0f;
    return crit;
}

uint32 Unit::GetWeaponSkillValue (WeaponAttackType attType, Unit const* target) const
{
    uint32 value = 0;
    if(GetTypeId() == TYPEID_PLAYER)
    {
        Item* item = ((Player*)this)->GetWeaponForAttack(attType,true,true);

        // feral or unarmed skill only for base attack
        if(attType != BASE_ATTACK && !item )
            return 0;

        if(IsInFeralForm())
            return GetMaxSkillValueForLevel();              // always maximized SKILL_FERAL_COMBAT in fact

        // weapon skill or (unarmed for base attack)
        uint32  skill = item ? item->GetSkill() : SKILL_UNARMED;

        // in PvP use full skill instead current skill value
        value = (target && target->GetTypeId() == TYPEID_PLAYER)
            ? ((Player*)this)->GetMaxSkillValue(skill)
            : ((Player*)this)->GetSkillValue(skill);
        // Modify value from ratings
        value += uint32(((Player*)this)->GetRatingBonusValue(CR_WEAPON_SKILL));
        switch (attType)
        {
            case BASE_ATTACK:   value+=uint32(((Player*)this)->GetRatingBonusValue(CR_WEAPON_SKILL_MAINHAND));break;
            case OFF_ATTACK:    value+=uint32(((Player*)this)->GetRatingBonusValue(CR_WEAPON_SKILL_OFFHAND));break;
            case RANGED_ATTACK: value+=uint32(((Player*)this)->GetRatingBonusValue(CR_WEAPON_SKILL_RANGED));break;
        }
    }
    else
        value = GetUnitMeleeSkill(target);
   return value;
}

void Unit::_UpdateSpells( uint32 time )
{
    if(m_currentSpells[CURRENT_AUTOREPEAT_SPELL])
        _UpdateAutoRepeatSpell();

    // remove finished spells from current pointers
    for (uint32 i = 0; i < CURRENT_MAX_SPELL; ++i)
    {
        if (m_currentSpells[i] && m_currentSpells[i]->getState() == SPELL_STATE_FINISHED)
        {
            m_currentSpells[i]->SetReferencedFromCurrent(false);
            m_currentSpells[i] = NULL;                      // remove pointer
        }
    }

    // update auras
    // m_AurasUpdateIterator can be updated in inderect called code at aura remove to skip next planned to update but removed auras
    for (m_AurasUpdateIterator = m_Auras.begin(); m_AurasUpdateIterator != m_Auras.end();)
    {
        Aura* i_aura = m_AurasUpdateIterator->second;
        ++m_AurasUpdateIterator;                            // need shift to next for allow update if need into aura update
        i_aura->UpdateAura(time);
    }

    // remove expired auras
    for (AuraMap::iterator i = m_Auras.begin(); i != m_Auras.end();)
    {
        if ((*i).second)
        {
            if ( !(*i).second->GetAuraDuration() && !((*i).second->IsPermanent() || ((*i).second->IsPassive())) )
                RemoveAura(i);
            else
                ++i;
        }
        else
            ++i;
    }

    if(!m_gameObj.empty())
    {
        GameObjectList::iterator ite1, dnext1;
        for (ite1 = m_gameObj.begin(); ite1 != m_gameObj.end(); ite1 = dnext1)
        {
            dnext1 = ite1;
            //(*i)->Update( difftime );
            if( !(*ite1)->isSpawned() )
            {
                (*ite1)->SetOwnerGUID(0);
                (*ite1)->SetRespawnTime(0);
                (*ite1)->Delete();
                dnext1 = m_gameObj.erase(ite1);
            }
            else
                ++dnext1;
        }
    }
}

void Unit::_UpdateAutoRepeatSpell()
{
    //check "realtime" interrupts
    if ( (GetTypeId() == TYPEID_PLAYER && ((Player*)this)->isMoving()) || IsNonMeleeSpellCasted(false,false,true) )
    {
        // cancel wand shoot
        if(m_currentSpells[CURRENT_AUTOREPEAT_SPELL]->m_spellInfo->Id != SPELL_ID_AUTOSHOT)
            InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
        m_AutoRepeatFirstCast = true;
        return;
    }

    //apply delay
    if ( m_AutoRepeatFirstCast && getAttackTimer(RANGED_ATTACK) < 500 )
        setAttackTimer(RANGED_ATTACK,500);
    m_AutoRepeatFirstCast = false;

    //castroutine
    if (isAttackReady(RANGED_ATTACK))
    {
        // Check if able to cast
        if(m_currentSpells[CURRENT_AUTOREPEAT_SPELL]->CheckCast(true) != SPELL_CAST_OK)
        {
            InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
            return;
        }

        // we want to shoot
        Spell* spell = new Spell(this, m_currentSpells[CURRENT_AUTOREPEAT_SPELL]->m_spellInfo, true, 0);
        spell->prepare(&(m_currentSpells[CURRENT_AUTOREPEAT_SPELL]->m_targets));

        // all went good, reset attack
        resetAttackTimer(RANGED_ATTACK);
    }
}

void Unit::SetCurrentCastedSpell( Spell * pSpell )
{
    assert(pSpell);                                         // NULL may be never passed here, use InterruptSpell or InterruptNonMeleeSpells

    CurrentSpellTypes CSpellType = pSpell->GetCurrentContainer();

    if (pSpell == m_currentSpells[CSpellType]) return;      // avoid breaking self

    // break same type spell if it is not delayed
    InterruptSpell(CSpellType,false);

    // special breakage effects:
    switch (CSpellType)
    {
        case CURRENT_GENERIC_SPELL:
        {
            // generic spells always break channeled not delayed spells
            InterruptSpell(CURRENT_CHANNELED_SPELL,false);

            // autorepeat breaking
            if ( m_currentSpells[CURRENT_AUTOREPEAT_SPELL] )
            {
                // break autorepeat if not Auto Shot
                if (m_currentSpells[CURRENT_AUTOREPEAT_SPELL]->m_spellInfo->Id != SPELL_ID_AUTOSHOT)
                    InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
                m_AutoRepeatFirstCast = true;
            }
        } break;

        case CURRENT_CHANNELED_SPELL:
        {
            // channel spells always break generic non-delayed and any channeled spells
            InterruptSpell(CURRENT_GENERIC_SPELL,false);
            InterruptSpell(CURRENT_CHANNELED_SPELL);

            // it also does break autorepeat if not Auto Shot
            if ( m_currentSpells[CURRENT_AUTOREPEAT_SPELL] &&
                m_currentSpells[CURRENT_AUTOREPEAT_SPELL]->m_spellInfo->Id != SPELL_ID_AUTOSHOT )
                InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
        } break;

        case CURRENT_AUTOREPEAT_SPELL:
        {
            // only Auto Shoot does not break anything
            if (pSpell->m_spellInfo->Id != SPELL_ID_AUTOSHOT)
            {
                // generic autorepeats break generic non-delayed and channeled non-delayed spells
                InterruptSpell(CURRENT_GENERIC_SPELL,false);
                InterruptSpell(CURRENT_CHANNELED_SPELL,false);
            }
            // special action: set first cast flag
            m_AutoRepeatFirstCast = true;
        } break;

        default:
        {
            // other spell types don't break anything now
        } break;
    }

    // current spell (if it is still here) may be safely deleted now
    if (m_currentSpells[CSpellType])
        m_currentSpells[CSpellType]->SetReferencedFromCurrent(false);

    // set new current spell
    m_currentSpells[CSpellType] = pSpell;
    pSpell->SetReferencedFromCurrent(true);

    pSpell->m_selfContainer = &(m_currentSpells[pSpell->GetCurrentContainer()]);
}

void Unit::InterruptSpell(CurrentSpellTypes spellType, bool withDelayed)
{
    assert(spellType < CURRENT_MAX_SPELL);

    if (m_currentSpells[spellType] && (withDelayed || m_currentSpells[spellType]->getState() != SPELL_STATE_DELAYED) )
    {
        // send autorepeat cancel message for autorepeat spells
        if (spellType == CURRENT_AUTOREPEAT_SPELL)
        {
            if(GetTypeId() == TYPEID_PLAYER)
                ((Player*)this)->SendAutoRepeatCancel(this);
        }

        if (m_currentSpells[spellType]->getState() != SPELL_STATE_FINISHED)
            m_currentSpells[spellType]->cancel();

        // cancel can interrupt spell already (caster cancel ->target aura remove -> caster iterrupt)
        if (m_currentSpells[spellType])
        {
            m_currentSpells[spellType]->SetReferencedFromCurrent(false);
            m_currentSpells[spellType] = NULL;
        }
    }
}

void Unit::FinishSpell(CurrentSpellTypes spellType, bool ok /*= true*/)
{
    Spell* spell = m_currentSpells[spellType];
    if (!spell)
        return;

    if (spellType == CURRENT_CHANNELED_SPELL)
        spell->SendChannelUpdate(0);

    spell->finish(ok);
}


bool Unit::IsNonMeleeSpellCasted(bool withDelayed, bool skipChanneled, bool skipAutorepeat) const
{
    // We don't do loop here to explicitly show that melee spell is excluded.
    // Maybe later some special spells will be excluded too.

    // generic spells are casted when they are not finished and not delayed
    if ( m_currentSpells[CURRENT_GENERIC_SPELL] &&
        (m_currentSpells[CURRENT_GENERIC_SPELL]->getState() != SPELL_STATE_FINISHED) &&
        (withDelayed || m_currentSpells[CURRENT_GENERIC_SPELL]->getState() != SPELL_STATE_DELAYED) )
        return(true);

    // channeled spells may be delayed, but they are still considered casted
    else if ( !skipChanneled && m_currentSpells[CURRENT_CHANNELED_SPELL] &&
        (m_currentSpells[CURRENT_CHANNELED_SPELL]->getState() != SPELL_STATE_FINISHED) )
        return(true);

    // autorepeat spells may be finished or delayed, but they are still considered casted
    else if ( !skipAutorepeat && m_currentSpells[CURRENT_AUTOREPEAT_SPELL] )
        return(true);

    return(false);
}

void Unit::InterruptNonMeleeSpells(bool withDelayed, uint32 spell_id)
{
    // generic spells are interrupted if they are not finished or delayed
    if (m_currentSpells[CURRENT_GENERIC_SPELL] && (!spell_id || m_currentSpells[CURRENT_GENERIC_SPELL]->m_spellInfo->Id==spell_id))
        InterruptSpell(CURRENT_GENERIC_SPELL,withDelayed);

    // autorepeat spells are interrupted if they are not finished or delayed
    if (m_currentSpells[CURRENT_AUTOREPEAT_SPELL] && (!spell_id || m_currentSpells[CURRENT_AUTOREPEAT_SPELL]->m_spellInfo->Id==spell_id))
        InterruptSpell(CURRENT_AUTOREPEAT_SPELL,withDelayed);

    // channeled spells are interrupted if they are not finished, even if they are delayed
    if (m_currentSpells[CURRENT_CHANNELED_SPELL] && (!spell_id || m_currentSpells[CURRENT_CHANNELED_SPELL]->m_spellInfo->Id==spell_id))
        InterruptSpell(CURRENT_CHANNELED_SPELL,true);
}

Spell* Unit::FindCurrentSpellBySpellId(uint32 spell_id) const
{
    for (uint32 i = 0; i < CURRENT_MAX_SPELL; ++i)
        if(m_currentSpells[i] && m_currentSpells[i]->m_spellInfo->Id==spell_id)
            return m_currentSpells[i];
    return NULL;
}

void Unit::SetInFront(Unit const* target)
{
    SetOrientation(GetAngle(target));
}

void Unit::SetFacingTo(float ori)
{
    // update orientation at server
    SetOrientation(ori);

    // and client
    WorldPacket data;
    BuildHeartBeatMsg(&data);
    SendMessageToSet(&data, false);
}

bool Unit::isInAccessablePlaceFor(Creature const* c) const
{
    if(IsInWater())
        return c->canSwim();
    else
        return c->canWalk() || c->canFly();
}

bool Unit::IsInWater() const
{
    return GetBaseMap()->IsInWater(GetPositionX(),GetPositionY(), GetPositionZ());
}

bool Unit::IsUnderWater() const
{
    return GetBaseMap()->IsUnderWater(GetPositionX(),GetPositionY(),GetPositionZ());
}

void Unit::DeMorph()
{
    SetDisplayId(GetNativeDisplayId());
}

int32 Unit::GetTotalAuraModifier(AuraType auratype) const
{
    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for(AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
        modifier += (*i)->GetModifier()->m_amount;

    return modifier;
}

float Unit::GetTotalAuraMultiplier(AuraType auratype) const
{
    float multiplier = 1.0f;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for(AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
        multiplier *= (100.0f + (*i)->GetModifier()->m_amount)/100.0f;

    return multiplier;
}

int32 Unit::GetMaxPositiveAuraModifier(AuraType auratype) const
{
    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for(AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
        if ((*i)->GetModifier()->m_amount > modifier)
            modifier = (*i)->GetModifier()->m_amount;

    return modifier;
}

int32 Unit::GetMaxNegativeAuraModifier(AuraType auratype) const
{
    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for(AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
        if ((*i)->GetModifier()->m_amount < modifier)
            modifier = (*i)->GetModifier()->m_amount;

    return modifier;
}

int32 Unit::GetTotalAuraModifierByMiscMask(AuraType auratype, uint32 misc_mask) const
{
    if(!misc_mask)
        return 0;

    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for(AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        if (mod->m_miscvalue & misc_mask)
            modifier += mod->m_amount;
    }
    return modifier;
}

float Unit::GetTotalAuraMultiplierByMiscMask(AuraType auratype, uint32 misc_mask) const
{
    if(!misc_mask)
        return 1.0f;

    float multiplier = 1.0f;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for(AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        if (mod->m_miscvalue & misc_mask)
            multiplier *= (100.0f + mod->m_amount)/100.0f;
    }
    return multiplier;
}

int32 Unit::GetMaxPositiveAuraModifierByMiscMask(AuraType auratype, uint32 misc_mask) const
{
    if(!misc_mask)
        return 0;

    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for(AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        if (mod->m_miscvalue & misc_mask && mod->m_amount > modifier)
            modifier = mod->m_amount;
    }

    return modifier;
}

int32 Unit::GetMaxNegativeAuraModifierByMiscMask(AuraType auratype, uint32 misc_mask) const
{
    if(!misc_mask)
        return 0;

    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for(AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        if (mod->m_miscvalue & misc_mask && mod->m_amount < modifier)
            modifier = mod->m_amount;
    }

    return modifier;
}

int32 Unit::GetTotalAuraModifierByMiscValue(AuraType auratype, int32 misc_value) const
{
    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for(AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        if (mod->m_miscvalue == misc_value)
            modifier += mod->m_amount;
    }
    return modifier;
}

float Unit::GetTotalAuraMultiplierByMiscValue(AuraType auratype, int32 misc_value) const
{
    float multiplier = 1.0f;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for(AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        if (mod->m_miscvalue == misc_value)
            multiplier *= (100.0f + mod->m_amount)/100.0f;
    }
    return multiplier;
}

int32 Unit::GetMaxPositiveAuraModifierByMiscValue(AuraType auratype, int32 misc_value) const
{
    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for(AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        if (mod->m_miscvalue == misc_value && mod->m_amount > modifier)
            modifier = mod->m_amount;
    }

    return modifier;
}

int32 Unit::GetMaxNegativeAuraModifierByMiscValue(AuraType auratype, int32 misc_value) const
{
    int32 modifier = 0;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for(AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        if (mod->m_miscvalue == misc_value && mod->m_amount < modifier)
            modifier = mod->m_amount;
    }

    return modifier;
}

float Unit::GetTotalAuraMultiplierByMiscValueForMask(AuraType auratype, uint32 mask) const
{
    if(!mask)
        return 1.0f;

    float multiplier = 1.0f;

    AuraList const& mTotalAuraList = GetAurasByType(auratype);
    for(AuraList::const_iterator i = mTotalAuraList.begin();i != mTotalAuraList.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        if (mask & (1 << (mod->m_miscvalue -1)))
            multiplier *= (100.0f + mod->m_amount)/100.0f;
    }
    return multiplier;
}

bool Unit::AddAura(Aura *Aur)
{
    SpellEntry const* aurSpellInfo = Aur->GetSpellProto();

    // ghost spell check, allow apply any auras at player loading in ghost mode (will be cleanup after load)
    if( !isAlive() && !IsDeathPersistentSpell(aurSpellInfo) &&
        !IsDeathOnlySpell(aurSpellInfo) &&
        (GetTypeId()!=TYPEID_PLAYER || !((Player*)this)->GetSession()->PlayerLoading()) )
    {
        delete Aur;
        return false;
    }

    if(Aur->GetTarget() != this)
    {
        sLog.outError("Aura (spell %u eff %u) add to aura list of %s (lowguid: %u) but Aura target is %s (lowguid: %u)",
            Aur->GetId(),Aur->GetEffIndex(),(GetTypeId()==TYPEID_PLAYER?"player":"creature"),GetGUIDLow(),
            (Aur->GetTarget()->GetTypeId()==TYPEID_PLAYER?"player":"creature"),Aur->GetTarget()->GetGUIDLow());
        delete Aur;
        return false;
    }

    // m_auraname can be modified to SPELL_AURA_NONE for area auras, this expected for this value
    AuraType aurName = Aur->GetModifier()->m_auraname;

    spellEffectPair spair = spellEffectPair(Aur->GetId(), Aur->GetEffIndex());
    AuraMap::iterator i = m_Auras.find( spair );

    // take out same spell
    if (i != m_Auras.end())
    {
        // passive and persistent auras can stack with themselves any number of times
        if (!Aur->IsPassive() && !Aur->IsPersistent())
        {
            for(AuraMap::iterator i2 = m_Auras.lower_bound(spair); i2 != m_Auras.upper_bound(spair); ++i2)
            {
                Aura* aur2 = i2->second;
                if(aur2->GetCasterGUID()==Aur->GetCasterGUID())
                {
                    // Aura can stack on self -> Stack it;
                    if(aurSpellInfo->StackAmount)
                    {
                        // can be created with >1 stack by some spell mods
                        aur2->modStackAmount(Aur->GetStackAmount());
                        delete Aur;
                        return false;
                    }
                    // Carry over removed Aura's remaining damage if Aura still has ticks remaining
                    else if (aur2->GetSpellProto()->AttributesEx4 & SPELL_ATTR_EX4_STACK_DOT_MODIFIER && aurName == SPELL_AURA_PERIODIC_DAMAGE && aur2->GetAuraDuration() > 0)
                    {
                        int32 remainingTicks = aur2->GetAuraMaxTicks() - aur2->GetAuraTicks();
                        int32 remainingDamage = aur2->GetModifier()->m_amount * remainingTicks;

                        Aur->GetModifier()->m_amount += int32(remainingDamage / Aur->GetAuraMaxTicks());
                    }
                    // can be only single (this check done at _each_ aura add
                    RemoveAura(i2,AURA_REMOVE_BY_STACK);
                    break;
                }

                bool stop = false;

                // m_auraname can be modified to SPELL_AURA_NONE for area auras, use original
                AuraType aurNameReal = AuraType(aurSpellInfo->EffectApplyAuraName[Aur->GetEffIndex()]);

                switch(aurNameReal)
                {
                    // DoT/HoT/etc
                    case SPELL_AURA_DUMMY:                  // allow stack
                    case SPELL_AURA_PERIODIC_DAMAGE:
                    case SPELL_AURA_PERIODIC_DAMAGE_PERCENT:
                    case SPELL_AURA_PERIODIC_LEECH:
                    case SPELL_AURA_PERIODIC_HEAL:
                    case SPELL_AURA_OBS_MOD_HEALTH:
                    case SPELL_AURA_PERIODIC_MANA_LEECH:
                    case SPELL_AURA_OBS_MOD_MANA:
                    case SPELL_AURA_POWER_BURN_MANA:
                        break;
                    case SPELL_AURA_PERIODIC_ENERGIZE:      // all or self or clear non-stackable
                    default:                                // not allow
                        // can be only single (this check done at _each_ aura add
                        RemoveAura(i2,AURA_REMOVE_BY_STACK);
                        stop = true;
                        break;
                }

                if(stop)
                    break;
            }
        }
    }

    // passive auras not stacable with other ranks
    if (!IsPassiveSpellStackableWithRanks(aurSpellInfo))
    {
        if (!RemoveNoStackAurasDueToAura(Aur))
        {
            delete Aur;
            return false;                                   // couldn't remove conflicting aura with higher rank
        }
    }

    // update single target auras list (before aura add to aura list, to prevent unexpected remove recently added aura)
    if (Aur->IsSingleTarget() && Aur->GetTarget())
    {
        // caster pointer can be deleted in time aura remove, find it by guid at each iteration
        for(;;)
        {
            Unit* caster = Aur->GetCaster();
            if(!caster)                                     // caster deleted and not required adding scAura
                break;

            bool restart = false;
            AuraList& scAuras = caster->GetSingleCastAuras();
            for(AuraList::const_iterator itr = scAuras.begin(); itr != scAuras.end(); ++itr)
            {
                if( (*itr)->GetTarget() != Aur->GetTarget() &&
                    IsSingleTargetSpells((*itr)->GetSpellProto(),aurSpellInfo) )
                {
                    if ((*itr)->IsInUse())
                    {
                        sLog.outError("Aura (Spell %u Effect %u) is in process but attempt removed at aura (Spell %u Effect %u) adding, need add stack rule for IsSingleTargetSpell", (*itr)->GetId(), (*itr)->GetEffIndex(),Aur->GetId(), Aur->GetEffIndex());
                        continue;
                    }
                    (*itr)->GetTarget()->RemoveAura((*itr)->GetId(), (*itr)->GetEffIndex());
                    restart = true;
                    break;
                }
            }

            if(!restart)
            {
                // done
                scAuras.push_back(Aur);
                break;
            }
        }
    }

    // add aura, register in lists and arrays
    Aur->_AddAura();
    m_Auras.insert(AuraMap::value_type(spellEffectPair(Aur->GetId(), Aur->GetEffIndex()), Aur));
    if (aurName < TOTAL_AURAS)
    {
        m_modAuras[aurName].push_back(Aur);
    }

    Aur->ApplyModifier(true,true);
    sLog.outDebug("Aura %u now is in use", aurName);

    // if aura deleted before boosts apply ignore
    // this can be possible it it removed indirectly by triggered spell effect at ApplyModifier
    if (Aur->IsDeleted())
        return false;

    if(IsSpellLastAuraEffect(aurSpellInfo,Aur->GetEffIndex()))
        Aur->HandleSpellSpecificBoosts(true);

    return true;
}

void Unit::RemoveRankAurasDueToSpell(uint32 spellId)
{
    SpellEntry const *spellInfo = sSpellStore.LookupEntry(spellId);
    if(!spellInfo)
        return;
    AuraMap::const_iterator i,next;
    for (i = m_Auras.begin(); i != m_Auras.end(); i = next)
    {
        next = i;
        ++next;
        uint32 i_spellId = (*i).second->GetId();
        if((*i).second && i_spellId && i_spellId != spellId)
        {
            if(sSpellMgr.IsRankSpellDueToSpell(spellInfo,i_spellId))
            {
                RemoveAurasDueToSpell(i_spellId);

                if( m_Auras.empty() )
                    break;
                else
                    next =  m_Auras.begin();
            }
        }
    }
}

bool Unit::RemoveNoStackAurasDueToAura(Aura *Aur)
{
    if (!Aur)
        return false;

    SpellEntry const* spellProto = Aur->GetSpellProto();
    if (!spellProto)
        return false;

    uint32 spellId = Aur->GetId();
    SpellEffectIndex effIndex = Aur->GetEffIndex();

    // passive spell special case (only non stackable with ranks)
    if(IsPassiveSpell(spellId))
    {
        if(IsPassiveSpellStackableWithRanks(spellProto))
            return true;
    }

    SpellSpecific spellId_spec = GetSpellSpecific(spellId);

    AuraMap::iterator i,next;
    for (i = m_Auras.begin(); i != m_Auras.end(); i = next)
    {
        next = i;
        ++next;
        if (!(*i).second) continue;

        SpellEntry const* i_spellProto = (*i).second->GetSpellProto();

        if (!i_spellProto)
            continue;

        uint32 i_spellId = i_spellProto->Id;

        // early checks that spellId is passive non stackable spell
        if(IsPassiveSpell(i_spellId))
        {
            // passive non-stackable spells not stackable only for same caster
            if(Aur->GetCasterGUID()!=i->second->GetCasterGUID())
                continue;

            // passive non-stackable spells not stackable only with another rank of same spell
            if (!sSpellMgr.IsRankSpellDueToSpell(spellProto, i_spellId))
                continue;
        }

        SpellEffectIndex i_effIndex = (*i).second->GetEffIndex();

        if(i_spellId == spellId) continue;

        bool is_triggered_by_spell = false;
        // prevent triggering aura of removing aura that triggered it
        for(int j = 0; j < MAX_EFFECT_INDEX; ++j)
            if (i_spellProto->EffectTriggerSpell[j] == spellId)
                is_triggered_by_spell = true;

        // prevent triggered aura of removing aura that triggering it (triggered effect early some aura of parent spell
        for(int j = 0; j < MAX_EFFECT_INDEX; ++j)
            if (spellProto->EffectTriggerSpell[j] == i_spellId)
                is_triggered_by_spell = true;

        if (is_triggered_by_spell)
            continue;

        SpellSpecific i_spellId_spec = GetSpellSpecific(i_spellId);

        // single allowed spell specific from same caster or from any caster at target
        bool is_spellSpecPerTargetPerCaster = IsSingleFromSpellSpecificPerTargetPerCaster(spellId_spec,i_spellId_spec);
        bool is_spellSpecPerTarget = IsSingleFromSpellSpecificPerTarget(spellId_spec,i_spellId_spec);
        if( is_spellSpecPerTarget || is_spellSpecPerTargetPerCaster && Aur->GetCasterGUID() == (*i).second->GetCasterGUID() )
        {
            // cannot remove higher rank
            if (sSpellMgr.IsRankSpellDueToSpell(spellProto, i_spellId))
                if(CompareAuraRanks(spellId, effIndex, i_spellId, i_effIndex) < 0)
                    return false;

            // Its a parent aura (create this aura in ApplyModifier)
            if ((*i).second->IsInUse())
            {
                sLog.outError("Aura (Spell %u Effect %u) is in process but attempt removed at aura (Spell %u Effect %u) adding, need add stack rule for Unit::RemoveNoStackAurasDueToAura", i->second->GetId(), i->second->GetEffIndex(),Aur->GetId(), Aur->GetEffIndex());
                continue;
            }
            RemoveAurasDueToSpell(i_spellId);

            if( m_Auras.empty() )
                break;
            else
                next =  m_Auras.begin();

            continue;
        }

        // spell with spell specific that allow single ranks for spell from diff caster
        // same caster case processed or early or later
        bool is_spellPerTarget = IsSingleFromSpellSpecificSpellRanksPerTarget(spellId_spec,i_spellId_spec);
        if ( is_spellPerTarget && Aur->GetCasterGUID() != (*i).second->GetCasterGUID() && sSpellMgr.IsRankSpellDueToSpell(spellProto, i_spellId))
        {
            // cannot remove higher rank
            if(CompareAuraRanks(spellId, effIndex, i_spellId, i_effIndex) < 0)
                return false;

            // Its a parent aura (create this aura in ApplyModifier)
            if ((*i).second->IsInUse())
            {
                sLog.outError("Aura (Spell %u Effect %u) is in process but attempt removed at aura (Spell %u Effect %u) adding, need add stack rule for Unit::RemoveNoStackAurasDueToAura", i->second->GetId(), i->second->GetEffIndex(),Aur->GetId(), Aur->GetEffIndex());
                continue;
            }
            RemoveAurasDueToSpell(i_spellId);

            if( m_Auras.empty() )
                break;
            else
                next =  m_Auras.begin();

            continue;
        }

        // non single (per caster) per target spell specific (possible single spell per target at caster)
        if( !is_spellSpecPerTargetPerCaster && !is_spellSpecPerTarget && sSpellMgr.IsNoStackSpellDueToSpell(spellId, i_spellId) )
        {
            // Its a parent aura (create this aura in ApplyModifier)
            if ((*i).second->IsInUse())
            {
                sLog.outError("Aura (Spell %u Effect %u) is in process but attempt removed at aura (Spell %u Effect %u) adding, need add stack rule for Unit::RemoveNoStackAurasDueToAura", i->second->GetId(), i->second->GetEffIndex(),Aur->GetId(), Aur->GetEffIndex());
                continue;
            }
            RemoveAurasDueToSpell(i_spellId);

            if( m_Auras.empty() )
                break;
            else
                next =  m_Auras.begin();

            continue;
        }

        // Potions stack aura by aura (elixirs/flask already checked)
        if( spellProto->SpellFamilyName == SPELLFAMILY_POTION && i_spellProto->SpellFamilyName == SPELLFAMILY_POTION )
        {
            if (IsNoStackAuraDueToAura(spellId, effIndex, i_spellId, i_effIndex))
            {
                if(CompareAuraRanks(spellId, effIndex, i_spellId, i_effIndex) < 0)
                    return false;                       // cannot remove higher rank

                // Its a parent aura (create this aura in ApplyModifier)
                if ((*i).second->IsInUse())
                {
                    sLog.outError("Aura (Spell %u Effect %u) is in process but attempt removed at aura (Spell %u Effect %u) adding, need add stack rule for Unit::RemoveNoStackAurasDueToAura", i->second->GetId(), i->second->GetEffIndex(),Aur->GetId(), Aur->GetEffIndex());
                    continue;
                }
                RemoveAura(i);
                next = i;
            }
        }
    }
    return true;
}

void Unit::RemoveAura(uint32 spellId, SpellEffectIndex effindex, Aura* except)
{
    spellEffectPair spair = spellEffectPair(spellId, effindex);
    for(AuraMap::iterator iter = m_Auras.lower_bound(spair); iter != m_Auras.upper_bound(spair);)
    {
        if(iter->second!=except)
        {
            RemoveAura(iter);
            iter = m_Auras.lower_bound(spair);
        }
        else
            ++iter;
    }
}

void Unit::RemoveAurasByCasterSpell(uint32 spellId, uint64 casterGUID)
{
    for (AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end(); )
    {
        Aura *aur = iter->second;
        if (aur->GetId() == spellId && aur->GetCasterGUID() == casterGUID)
            RemoveAura(iter);
        else
            ++iter;
    }
}

void Unit::RemoveAurasByCasterSpell(uint32 spellId, SpellEffectIndex effindex, uint64 casterGUID)
{
    spellEffectPair spair = spellEffectPair(spellId, effindex);
    for(AuraMap::iterator iter = m_Auras.lower_bound(spair); iter != m_Auras.upper_bound(spair);)
    {
        Aura *aur = iter->second;
        if (aur->GetId() == spellId && aur->GetCasterGUID() == casterGUID)
        {
            RemoveAura(iter);
            iter = m_Auras.lower_bound(spair);
        }
        else
            ++iter;
    }
}

void Unit::RemoveSingleAuraDueToSpellByDispel(uint32 spellId, uint64 casterGUID, Unit *dispeler)
{
    SpellEntry const* spellEntry = sSpellStore.LookupEntry(spellId);

    // Custom dispel cases
    // Unstable Affliction
    if(spellEntry->SpellFamilyName == SPELLFAMILY_WARLOCK && (spellEntry->SpellFamilyFlags & UI64LIT(0x010000000000)))
    {
        if (Aura* dotAura = GetAura(SPELL_AURA_PERIODIC_DAMAGE,SPELLFAMILY_WARLOCK,UI64LIT(0x010000000000),0x00000000,casterGUID))
        {
            int32 damage = dotAura->GetModifier()->m_amount*9;

            // Remove spell auras from stack
            RemoveSingleSpellAurasByCasterSpell(spellId, casterGUID, AURA_REMOVE_BY_DISPEL);

            // backfire damage and silence
            dispeler->CastCustomSpell(dispeler, 31117, &damage, NULL, NULL, true, NULL, NULL,casterGUID);
            return;
        }
    }
    // Flame Shock
    else if (spellEntry->SpellFamilyName == SPELLFAMILY_SHAMAN && (spellEntry->SpellFamilyFlags & UI64LIT(0x10000000)))
    {
        Unit* caster = NULL;
        uint32 triggeredSpell = 0;

        if (Aura* dotAura = GetAura(SPELL_AURA_PERIODIC_DAMAGE, SPELLFAMILY_SHAMAN, UI64LIT(0x10000000), 0x00000000, casterGUID))
            caster = dotAura->GetCaster();

        if (caster && !caster->isDead())
        {
            Unit::AuraList const& auras = caster->GetAurasByType(SPELL_AURA_DUMMY);
            for (Unit::AuraList::const_iterator i = auras.begin(); i != auras.end(); ++i)
            {
                switch((*i)->GetId())
                {
                    case 51480: triggeredSpell=64694; break;// Lava Flows, Rank 1
                    case 51481: triggeredSpell=65263; break;// Lava Flows, Rank 2
                    case 51482: triggeredSpell=65264; break;// Lava Flows, Rank 3
                    default: continue;
                }
                break;
            }
        }

        // Remove spell auras from stack
        RemoveSingleSpellAurasByCasterSpell(spellId, casterGUID, AURA_REMOVE_BY_DISPEL);

        // Haste
        if (triggeredSpell)
            caster->CastSpell(caster, triggeredSpell, true);
        return;
    }
    // Vampiric touch (first dummy aura)
    else if (spellEntry->SpellFamilyName == SPELLFAMILY_PRIEST && spellEntry->SpellFamilyFlags & UI64LIT(0x0000040000000000))
    {
        if (Aura *dot = GetAura(SPELL_AURA_PERIODIC_DAMAGE, SPELLFAMILY_PRIEST, UI64LIT(0x0000040000000000), 0x00000000, casterGUID))
        {
            if(Unit* caster = dot->GetCaster())
            {
                int32 bp0 = dot->GetModifier()->m_amount;
                bp0 = 8 * caster->SpellDamageBonus(this, spellEntry, bp0, DOT, 1);

                // Remove spell auras from stack
                RemoveSingleSpellAurasByCasterSpell(spellId, casterGUID, AURA_REMOVE_BY_DISPEL);

                CastCustomSpell(this, 64085, &bp0, NULL, NULL, true, NULL, NULL, casterGUID);
                return;
            }
        }
    }

    RemoveSingleSpellAurasByCasterSpell(spellId, casterGUID, AURA_REMOVE_BY_DISPEL);
}

void Unit::RemoveAurasDueToSpellBySteal(uint32 spellId, uint64 casterGUID, Unit *stealer)
{
    for (AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end(); )
    {
        Aura *aur = iter->second;
        if (aur->GetId() == spellId && aur->GetCasterGUID() == casterGUID)
        {
            int32 basePoints = aur->GetBasePoints();
            // construct the new aura for the attacker - will never return NULL, it's just a wrapper for
            // some different constructors
            Aura * new_aur = CreateAura(aur->GetSpellProto(), aur->GetEffIndex(), &basePoints, stealer, this);

            // set its duration and maximum duration
            // max duration 2 minutes (in msecs)
            int32 dur = aur->GetAuraDuration();
            int32 max_dur = 2*MINUTE*IN_MILISECONDS;
            int32 new_max_dur = max_dur > dur ? dur : max_dur;
            new_aur->SetAuraMaxDuration( new_max_dur );
            new_aur->SetAuraDuration( new_max_dur );

            // set periodic to do at least one tick (for case when original aura has been at last tick preparing)
            int32 periodic = aur->GetModifier()->periodictime;
            new_aur->GetModifier()->periodictime = periodic < new_max_dur ? periodic : new_max_dur;

            // Unregister _before_ adding to stealer
            aur->UnregisterSingleCastAura();

            // strange but intended behaviour: Stolen single target auras won't be treated as single targeted
            new_aur->SetIsSingleTarget(false);

            // add the new aura to stealer
            stealer->AddAura(new_aur);

            // Remove aura as dispel
            RemoveAura(iter, AURA_REMOVE_BY_DISPEL);
        }
        else
            ++iter;
    }
}

void Unit::RemoveAurasDueToSpellByCancel(uint32 spellId)
{
    for (AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end(); )
    {
        if (iter->second->GetId() == spellId)
            RemoveAura(iter, AURA_REMOVE_BY_CANCEL);
        else
            ++iter;
    }
}

void Unit::RemoveAurasWithDispelType( DispelType type )
{
    // Create dispel mask by dispel type
    uint32 dispelMask = GetDispellMask(type);
    // Dispel all existing auras vs current dispel type
    AuraMap& auras = GetAuras();
    for(AuraMap::iterator itr = auras.begin(); itr != auras.end(); )
    {
        SpellEntry const* spell = itr->second->GetSpellProto();
        if( (1<<spell->Dispel) & dispelMask )
        {
            // Dispel aura
            RemoveAurasDueToSpell(spell->Id);
            itr = auras.begin();
        }
        else
            ++itr;
    }
}

void Unit::RemoveSingleAuraFromStack(AuraMap::iterator &i, AuraRemoveMode mode)
{
    if (i->second->modStackAmount(-1))
        RemoveAura(i,mode);
}


void Unit::RemoveSingleAuraFromStack(uint32 spellId, SpellEffectIndex effindex, AuraRemoveMode mode)
{
    AuraMap::iterator iter = m_Auras.find(spellEffectPair(spellId, effindex));
    if(iter != m_Auras.end())
        RemoveSingleAuraFromStack(iter,mode);
}

void Unit::RemoveSingleSpellAurasFromStack(uint32 spellId, AuraRemoveMode mode)
{
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
        RemoveSingleAuraFromStack(spellId, SpellEffectIndex(i), mode);
}

void Unit::RemoveSingleSpellAurasByCasterSpell(uint32 spellId, uint64 casterGUID, AuraRemoveMode mode)
{
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
        RemoveSingleAuraByCasterSpell(spellId, SpellEffectIndex(i), casterGUID, mode);
}

void Unit::RemoveSingleAuraByCasterSpell(uint32 spellId, SpellEffectIndex effindex, uint64 casterGUID, AuraRemoveMode mode)
{
    spellEffectPair spair = spellEffectPair(spellId, effindex);
    for(AuraMap::iterator iter = m_Auras.lower_bound(spair); iter != m_Auras.upper_bound(spair); ++iter)
    {
        Aura *aur = iter->second;
        if (aur->GetId() == spellId && aur->GetCasterGUID() == casterGUID)
        {
            RemoveSingleAuraFromStack(iter,mode);
            break;
        }
    }
}


void Unit::RemoveAurasDueToSpell(uint32 spellId, Aura* except)
{
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
        RemoveAura(spellId,SpellEffectIndex(i),except);
}

void Unit::RemoveAurasDueToItemSpell(Item* castItem,uint32 spellId)
{
    for (int k=0; k < MAX_EFFECT_INDEX; ++k)
    {
        spellEffectPair spair = spellEffectPair(spellId, SpellEffectIndex(k));
        for (AuraMap::iterator iter = m_Auras.lower_bound(spair); iter != m_Auras.upper_bound(spair);)
        {
            if (iter->second->GetCastItemGUID() == castItem->GetGUID())
            {
                RemoveAura(iter);
                iter = m_Auras.upper_bound(spair);          // overwrite by more appropriate
            }
            else
                ++iter;
        }
    }
}

void Unit::RemoveAurasWithInterruptFlags(uint32 flags)
{
    for (AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end(); )
    {
        if (iter->second->GetSpellProto()->AuraInterruptFlags & flags)
            RemoveAura(iter);
        else
            ++iter;
    }
}

void Unit::RemoveNotOwnSingleTargetAuras(uint32 newPhase)
{
    // single target auras from other casters
    for (AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end(); )
    {
        if (iter->second->GetCasterGUID()!=GetGUID() && IsSingleTargetSpell(iter->second->GetSpellProto()))
        {
            if(!newPhase)
                RemoveAura(iter);
            else
            {
                Unit* caster = iter->second->GetCaster();
                if(!caster || !caster->InSamePhase(newPhase))
                    RemoveAura(iter);
                else
                    ++iter;
            }
        }
        else
            ++iter;
    }

    // single target auras at other targets
    AuraList& scAuras = GetSingleCastAuras();
    for (AuraList::iterator iter = scAuras.begin(); iter != scAuras.end(); )
    {
        Aura* aura = *iter;
        if (aura->GetTarget() != this && !aura->GetTarget()->InSamePhase(newPhase))
        {
            scAuras.erase(iter);                            // explicitly remove, instead waiting remove in RemoveAura
            aura->GetTarget()->RemoveAura(aura);
            iter = scAuras.begin();
        }
        else
            ++iter;
    }

}

void Unit::RemoveAura(Aura* aura, AuraRemoveMode mode /*= AURA_REMOVE_BY_DEFAULT*/)
{
    AuraMap::iterator i = m_Auras.lower_bound(spellEffectPair(aura->GetId(), aura->GetEffIndex()));
    AuraMap::iterator upperBound = m_Auras.upper_bound(spellEffectPair(aura->GetId(), aura->GetEffIndex()));
    for (; i != upperBound; ++i)
    {
        if (i->second == aura)
        {
            RemoveAura(i,mode);
            return;
        }
    }
    sLog.outDebug("Trying to remove aura id %u effect %u by pointer but aura not found on target", aura->GetId(), aura->GetEffIndex());
}

void Unit::RemoveAura(AuraMap::iterator &i, AuraRemoveMode mode)
{
    Aura* Aur = i->second;
    SpellEntry const* AurSpellInfo = Aur->GetSpellProto();

    Aur->UnregisterSingleCastAura();

    // remove from list before mods removing (prevent cyclic calls, mods added before including to aura list - use reverse order)
    if (Aur->GetModifier()->m_auraname < TOTAL_AURAS)
    {
        m_modAuras[Aur->GetModifier()->m_auraname].remove(Aur);
    }

    // Set remove mode
    Aur->SetRemoveMode(mode);

    // if unit currently update aura list then make safe update iterator shift to next
    if (m_AurasUpdateIterator == i)
        ++m_AurasUpdateIterator;

    // some ShapeshiftBoosts at remove trigger removing other auras including parent Shapeshift aura
    // remove aura from list before to prevent deleting it before
    m_Auras.erase(i);

    // now aura removed from from list and can't be deleted by indirect call but can be referenced from callers

    // Statue unsummoned at aura remove
    Totem* statue = NULL;
    if(IsChanneledSpell(AurSpellInfo))
        if(Unit* caster = Aur->GetCaster())
            if(caster->GetTypeId()==TYPEID_UNIT && ((Creature*)caster)->isTotem() && ((Totem*)caster)->GetTotemType()==TOTEM_STATUE)
                statue = ((Totem*)caster);

    sLog.outDebug("Aura %u now is remove mode %d",Aur->GetModifier()->m_auraname, mode);
    if (mode != AURA_REMOVE_BY_DELETE)                      // not unapply if target will deleted
        Aur->ApplyModifier(false,true);

    if (Aur->_RemoveAura())
    {
        // last aura in stack removed
        if (mode != AURA_REMOVE_BY_DELETE && IsSpellLastAuraEffect(Aur->GetSpellProto(),Aur->GetEffIndex()))
            Aur->HandleSpellSpecificBoosts(false);
    }

    // If aura in use (removed from code that plan access to it data after return)
    // store it in aura list with delayed deletion
    if (Aur->IsInUse())
        m_deletedAuras.push_back(Aur);
    else
        delete Aur;

    if(statue)
        statue->UnSummon();

    // only way correctly remove all auras from list
    if( m_Auras.empty() )
        i = m_Auras.end();
    else
        i = m_Auras.begin();

}

void Unit::RemoveAllAuras(AuraRemoveMode mode /*= AURA_REMOVE_BY_DEFAULT*/)
{
    while (!m_Auras.empty())
    {
        AuraMap::iterator iter = m_Auras.begin();
        RemoveAura(iter,mode);
    }
}

void Unit::RemoveArenaAuras(bool onleave)
{
    // in join, remove positive buffs, on end, remove negative
    // used to remove positive visible auras in arenas
    for(AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end();)
    {
        if (!(iter->second->GetSpellProto()->AttributesEx4 & SPELL_ATTR_EX4_UNK21) &&
                                                            // don't remove stances, shadowform, pally/hunter auras
            !iter->second->IsPassive() &&                   // don't remove passive auras
            (!(iter->second->GetSpellProto()->Attributes & SPELL_ATTR_UNAFFECTED_BY_INVULNERABILITY) ||
            !(iter->second->GetSpellProto()->Attributes & SPELL_ATTR_UNK8)) &&
                                                            // not unaffected by invulnerability auras or not having that unknown flag (that seemed the most probable)
            (iter->second->IsPositive() != onleave))        // remove positive buffs on enter, negative buffs on leave
            RemoveAura(iter);
        else
            ++iter;
    }
}

void Unit::RemoveAllAurasOnDeath()
{
    // used just after dieing to remove all visible auras
    // and disable the mods for the passive ones
    for(AuraMap::iterator iter = m_Auras.begin(); iter != m_Auras.end();)
    {
        if (!iter->second->IsPassive() && !iter->second->IsDeathPersistent())
            RemoveAura(iter, AURA_REMOVE_BY_DEATH);
        else
            ++iter;
    }
}

void Unit::DelayAura(uint32 spellId, SpellEffectIndex effindex, int32 delaytime)
{
    AuraMap::const_iterator iter = m_Auras.find(spellEffectPair(spellId, effindex));
    if (iter != m_Auras.end())
    {
        if (iter->second->GetAuraDuration() < delaytime)
            iter->second->SetAuraDuration(0);
        else
            iter->second->SetAuraDuration(iter->second->GetAuraDuration() - delaytime);
        iter->second->SendAuraUpdate(false);
        sLog.outDebug("Aura %u partially interrupted on unit %u, new duration: %u ms",iter->second->GetModifier()->m_auraname, GetGUIDLow(), iter->second->GetAuraDuration());
    }
}

void Unit::_RemoveAllAuraMods()
{
    for (AuraMap::const_iterator i = m_Auras.begin(); i != m_Auras.end(); ++i)
    {
        (*i).second->ApplyModifier(false);
    }
}

void Unit::_ApplyAllAuraMods()
{
    for (AuraMap::const_iterator i = m_Auras.begin(); i != m_Auras.end(); ++i)
    {
        (*i).second->ApplyModifier(true);
    }
}

Aura* Unit::GetAura(uint32 spellId, SpellEffectIndex effindex)
{
    AuraMap::const_iterator iter = m_Auras.find(spellEffectPair(spellId, effindex));
    if (iter != m_Auras.end())
        return iter->second;
    return NULL;
}

Aura* Unit::GetAura(AuraType type, uint32 family, uint64 familyFlag, uint32 familyFlag2, uint64 casterGUID)
{
    AuraList const& auras = GetAurasByType(type);
    for(AuraList::const_iterator i = auras.begin();i != auras.end(); ++i)
    {
        SpellEntry const *spell = (*i)->GetSpellProto();
        if (spell->SpellFamilyName == family && (spell->SpellFamilyFlags & familyFlag || spell->SpellFamilyFlags2 & familyFlag2))
        {
            if (casterGUID && (*i)->GetCasterGUID()!=casterGUID)
                continue;
            return (*i);
        }
    }
    return NULL;
}

bool Unit::HasAura(uint32 spellId) const
{
    for (int i = 0; i < MAX_EFFECT_INDEX ; ++i)
    {
        AuraMap::const_iterator iter = m_Auras.find(spellEffectPair(spellId, SpellEffectIndex(i)));
        if (iter != m_Auras.end())
            return true;
    }
    return false;
}

void Unit::AddDynObject(DynamicObject* dynObj)
{
    m_dynObjGUIDs.push_back(dynObj->GetGUID());
}

void Unit::RemoveDynObject(uint32 spellid)
{
    if(m_dynObjGUIDs.empty())
        return;
    for (DynObjectGUIDs::iterator i = m_dynObjGUIDs.begin(); i != m_dynObjGUIDs.end();)
    {
        DynamicObject* dynObj = GetMap()->GetDynamicObject(*i);
        if(!dynObj)
        {
            i = m_dynObjGUIDs.erase(i);
        }
        else if(spellid == 0 || dynObj->GetSpellId() == spellid)
        {
            dynObj->Delete();
            i = m_dynObjGUIDs.erase(i);
        }
        else
            ++i;
    }
}

void Unit::RemoveAllDynObjects()
{
    while(!m_dynObjGUIDs.empty())
    {
        DynamicObject* dynObj = GetMap()->GetDynamicObject(*m_dynObjGUIDs.begin());
        if(dynObj)
            dynObj->Delete();
        m_dynObjGUIDs.erase(m_dynObjGUIDs.begin());
    }
}

DynamicObject * Unit::GetDynObject(uint32 spellId, SpellEffectIndex effIndex)
{
    for (DynObjectGUIDs::iterator i = m_dynObjGUIDs.begin(); i != m_dynObjGUIDs.end();)
    {
        DynamicObject* dynObj = GetMap()->GetDynamicObject(*i);
        if(!dynObj)
        {
            i = m_dynObjGUIDs.erase(i);
            continue;
        }

        if (dynObj->GetSpellId() == spellId && dynObj->GetEffIndex() == effIndex)
            return dynObj;
        ++i;
    }
    return NULL;
}

DynamicObject * Unit::GetDynObject(uint32 spellId)
{
    for (DynObjectGUIDs::iterator i = m_dynObjGUIDs.begin(); i != m_dynObjGUIDs.end();)
    {
        DynamicObject* dynObj = GetMap()->GetDynamicObject(*i);
        if(!dynObj)
        {
            i = m_dynObjGUIDs.erase(i);
            continue;
        }

        if (dynObj->GetSpellId() == spellId)
            return dynObj;
        ++i;
    }
    return NULL;
}

GameObject* Unit::GetGameObject(uint32 spellId) const
{
    for (GameObjectList::const_iterator i = m_gameObj.begin(); i != m_gameObj.end(); ++i)
        if ((*i)->GetSpellId() == spellId)
            return *i;

    return NULL;
}

void Unit::AddGameObject(GameObject* gameObj)
{
    assert(gameObj && gameObj->GetOwnerGUID()==0);
    m_gameObj.push_back(gameObj);
    gameObj->SetOwnerGUID(GetGUID());

    if ( GetTypeId()==TYPEID_PLAYER && gameObj->GetSpellId() )
    {
        SpellEntry const* createBySpell = sSpellStore.LookupEntry(gameObj->GetSpellId());
        // Need disable spell use for owner
        if (createBySpell && createBySpell->Attributes & SPELL_ATTR_DISABLED_WHILE_ACTIVE)
            // note: item based cooldowns and cooldown spell mods with charges ignored (unknown existed cases)
            ((Player*)this)->AddSpellAndCategoryCooldowns(createBySpell,0,NULL,true);
    }
}

void Unit::RemoveGameObject(GameObject* gameObj, bool del)
{
    assert(gameObj && gameObj->GetOwnerGUID()==GetGUID());

    gameObj->SetOwnerGUID(0);

    // GO created by some spell
    if (uint32 spellid = gameObj->GetSpellId())
    {
        RemoveAurasDueToSpell(spellid);

        if (GetTypeId()==TYPEID_PLAYER)
        {
            SpellEntry const* createBySpell = sSpellStore.LookupEntry(spellid );
            // Need activate spell use for owner
            if (createBySpell && createBySpell->Attributes & SPELL_ATTR_DISABLED_WHILE_ACTIVE)
                // note: item based cooldowns and cooldown spell mods with charges ignored (unknown existed cases)
                ((Player*)this)->SendCooldownEvent(createBySpell);
        }
    }

    m_gameObj.remove(gameObj);

    if(del)
    {
        gameObj->SetRespawnTime(0);
        gameObj->Delete();
    }
}

void Unit::RemoveGameObject(uint32 spellid, bool del)
{
    if(m_gameObj.empty())
        return;
    GameObjectList::iterator i, next;
    for (i = m_gameObj.begin(); i != m_gameObj.end(); i = next)
    {
        next = i;
        if(spellid == 0 || (*i)->GetSpellId() == spellid)
        {
            (*i)->SetOwnerGUID(0);
            if(del)
            {
                (*i)->SetRespawnTime(0);
                (*i)->Delete();
            }

            next = m_gameObj.erase(i);
        }
        else
            ++next;
    }
}

void Unit::RemoveAllGameObjects()
{
    // remove references to unit
    for(GameObjectList::iterator i = m_gameObj.begin(); i != m_gameObj.end();)
    {
        (*i)->SetOwnerGUID(0);
        (*i)->SetRespawnTime(0);
        (*i)->Delete();
        i = m_gameObj.erase(i);
    }
}

void Unit::SendSpellNonMeleeDamageLog(SpellNonMeleeDamage *log)
{
    WorldPacket data(SMSG_SPELLNONMELEEDAMAGELOG, (16+4+4+4+1+4+4+1+1+4+4+1)); // we guess size
    data.append(log->target->GetPackGUID());
    data.append(log->attacker->GetPackGUID());
    data << uint32(log->SpellID);
    data << uint32(log->damage);                            // damage amount
    data << uint32(log->overkill);                          // overkill
    data << uint8 (log->schoolMask);                        // damage school
    data << uint32(log->absorb);                            // AbsorbedDamage
    data << uint32(log->resist);                            // resist
    data << uint8 (log->physicalLog);                       // if 1, then client show spell name (example: %s's ranged shot hit %s for %u school or %s suffers %u school damage from %s's spell_name
    data << uint8 (log->unused);                            // unused
    data << uint32(log->blocked);                           // blocked
    data << uint32(log->HitInfo);
    data << uint8 (0);                                      // flag to use extend data
    SendMessageToSet( &data, true );
}

void Unit::SendSpellNonMeleeDamageLog(Unit *target, uint32 SpellID, uint32 Damage, SpellSchoolMask damageSchoolMask, uint32 AbsorbedDamage, uint32 Resist, bool PhysicalDamage, uint32 Blocked, bool CriticalHit)
{
    SpellNonMeleeDamage log(this, target, SpellID, damageSchoolMask);
    log.damage = Damage - AbsorbedDamage - Resist - Blocked;
    log.absorb = AbsorbedDamage;
    log.resist = Resist;
    log.physicalLog = PhysicalDamage;
    log.blocked = Blocked;
    log.HitInfo = SPELL_HIT_TYPE_UNK1 | SPELL_HIT_TYPE_UNK3 | SPELL_HIT_TYPE_UNK6;
    if(CriticalHit)
        log.HitInfo |= SPELL_HIT_TYPE_CRIT;
    SendSpellNonMeleeDamageLog(&log);
}

void Unit::SendPeriodicAuraLog(SpellPeriodicAuraLogInfo *pInfo)
{
    Aura *aura = pInfo->aura;
    Modifier *mod = aura->GetModifier();

    WorldPacket data(SMSG_PERIODICAURALOG, 30);
    data.append(aura->GetTarget()->GetPackGUID());
    data.appendPackGUID(aura->GetCasterGUID());
    data << uint32(aura->GetId());                          // spellId
    data << uint32(1);                                      // count
    data << uint32(mod->m_auraname);                        // auraId
    switch(mod->m_auraname)
    {
        case SPELL_AURA_PERIODIC_DAMAGE:
        case SPELL_AURA_PERIODIC_DAMAGE_PERCENT:
            data << uint32(pInfo->damage);                  // damage
            data << uint32(pInfo->overDamage);              // overkill?
            data << uint32(GetSpellSchoolMask(aura->GetSpellProto()));
            data << uint32(pInfo->absorb);                  // absorb
            data << uint32(pInfo->resist);                  // resist
            data << uint8(pInfo->critical ? 1 : 0);         // new 3.1.2 critical flag
            break;
        case SPELL_AURA_PERIODIC_HEAL:
        case SPELL_AURA_OBS_MOD_HEALTH:
            data << uint32(pInfo->damage);                  // damage
            data << uint32(pInfo->overDamage);              // overheal?
            data << uint8(pInfo->critical ? 1 : 0);         // new 3.1.2 critical flag
            break;
        case SPELL_AURA_OBS_MOD_MANA:
        case SPELL_AURA_PERIODIC_ENERGIZE:
            data << uint32(mod->m_miscvalue);               // power type
            data << uint32(pInfo->damage);                  // damage
            break;
        case SPELL_AURA_PERIODIC_MANA_LEECH:
            data << uint32(mod->m_miscvalue);               // power type
            data << uint32(pInfo->damage);                  // amount
            data << float(pInfo->multiplier);               // gain multiplier
            break;
        default:
            sLog.outError("Unit::SendPeriodicAuraLog: unknown aura %u", uint32(mod->m_auraname));
            return;
    }

    aura->GetTarget()->SendMessageToSet(&data, true);
}

void Unit::ProcDamageAndSpell(Unit *pVictim, uint32 procAttacker, uint32 procVictim, uint32 procExtra, uint32 amount, WeaponAttackType attType, SpellEntry const *procSpell)
{
     // Not much to do if no flags are set.
    if (procAttacker)
        ProcDamageAndSpellFor(false,pVictim,procAttacker, procExtra,attType, procSpell, amount);
    // Now go on with a victim's events'n'auras
    // Not much to do if no flags are set or there is no victim
    if(pVictim && pVictim->isAlive() && procVictim)
        pVictim->ProcDamageAndSpellFor(true,this,procVictim, procExtra, attType, procSpell, amount);
}

void Unit::SendSpellMiss(Unit *target, uint32 spellID, SpellMissInfo missInfo)
{
    WorldPacket data(SMSG_SPELLLOGMISS, (4+8+1+4+8+1));
    data << uint32(spellID);
    data << uint64(GetGUID());
    data << uint8(0);                                       // can be 0 or 1
    data << uint32(1);                                      // target count
    // for(i = 0; i < target count; ++i)
    data << uint64(target->GetGUID());                      // target GUID
    data << uint8(missInfo);
    // end loop
    SendMessageToSet(&data, true);
}

void Unit::SendAttackStateUpdate(CalcDamageInfo *damageInfo)
{
    sLog.outDebug("WORLD: Sending SMSG_ATTACKERSTATEUPDATE");

    uint32 count = 1;
    WorldPacket data(SMSG_ATTACKERSTATEUPDATE, 16 + 45);    // we guess size
    data << uint32(damageInfo->HitInfo);
    data.append(damageInfo->attacker->GetPackGUID());
    data.append(damageInfo->target->GetPackGUID());
    data << uint32(damageInfo->damage);                     // Full damage
    data << uint32(0);                                      // overkill value
    data << uint8(count);                                   // Sub damage count

    for(uint32 i = 0; i < count; ++i)
    {
        data << uint32(damageInfo->damageSchoolMask);       // School of sub damage
        data << float(damageInfo->damage);                  // sub damage
        data << uint32(damageInfo->damage);                 // Sub Damage
    }

    if(damageInfo->HitInfo & (HITINFO_ABSORB | HITINFO_ABSORB2))
    {
        for(uint32 i = 0; i < count; ++i)
            data << uint32(damageInfo->absorb);             // Absorb
    }

    if(damageInfo->HitInfo & (HITINFO_RESIST | HITINFO_RESIST2))
    {
        for(uint32 i = 0; i < count; ++i)
            data << uint32(damageInfo->resist);             // Resist
    }

    data << uint8(damageInfo->TargetState);
    data << uint32(0);
    data << uint32(0);

    if(damageInfo->HitInfo & HITINFO_BLOCK)
        data << uint32(damageInfo->blocked_amount);

    if(damageInfo->HitInfo & HITINFO_UNK3)
        data << uint32(0);

    if(damageInfo->HitInfo & HITINFO_UNK1)
    {
        data << uint32(0);
        data << float(0);
        data << float(0);
        data << float(0);
        data << float(0);
        data << float(0);
        data << float(0);
        data << float(0);
        data << float(0);
        for(uint8 i = 0; i < 5; ++i)
        {
            data << float(0);
            data << float(0);
        }
        data << uint32(0);
    }

    SendMessageToSet( &data, true );
}

void Unit::SendAttackStateUpdate(uint32 HitInfo, Unit *target, uint8 /*SwingType*/, SpellSchoolMask damageSchoolMask, uint32 Damage, uint32 AbsorbDamage, uint32 Resist, VictimState TargetState, uint32 BlockedAmount)
{
    CalcDamageInfo dmgInfo;
    dmgInfo.HitInfo = HitInfo;
    dmgInfo.attacker = this;
    dmgInfo.target = target;
    dmgInfo.damage = Damage - AbsorbDamage - Resist - BlockedAmount;
    dmgInfo.damageSchoolMask = damageSchoolMask;
    dmgInfo.absorb = AbsorbDamage;
    dmgInfo.resist = Resist;
    dmgInfo.TargetState = TargetState;
    dmgInfo.blocked_amount = BlockedAmount;
    SendAttackStateUpdate(&dmgInfo);
}

bool Unit::HandleHasteAuraProc(Unit *pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const * /*procSpell*/, uint32 /*procFlag*/, uint32 /*procEx*/, uint32 cooldown)
{
    SpellEntry const *hasteSpell = triggeredByAura->GetSpellProto();

    Item* castItem = triggeredByAura->GetCastItemGUID() && GetTypeId()==TYPEID_PLAYER
        ? ((Player*)this)->GetItemByGuid(triggeredByAura->GetCastItemGUID()) : NULL;

    uint32 triggered_spell_id = 0;
    Unit* target = pVictim;
    int32 basepoints0 = 0;

    switch(hasteSpell->SpellFamilyName)
    {
        case SPELLFAMILY_ROGUE:
        {
            switch(hasteSpell->Id)
            {
                // Blade Flurry
                case 13877:
                case 33735:
                {
                    target = SelectRandomUnfriendlyTarget(pVictim);
                    if(!target)
                        return false;
                    basepoints0 = damage;
                    triggered_spell_id = 22482;
                    break;
                }
            }
            break;
        }
    }

    // processed charge only counting case
    if(!triggered_spell_id)
        return true;

    SpellEntry const* triggerEntry = sSpellStore.LookupEntry(triggered_spell_id);

    if(!triggerEntry)
    {
        sLog.outError("Unit::HandleHasteAuraProc: Spell %u have not existed triggered spell %u",hasteSpell->Id,triggered_spell_id);
        return false;
    }

    // default case
    if(!target || target!=this && !target->isAlive())
        return false;

    if( cooldown && GetTypeId()==TYPEID_PLAYER && ((Player*)this)->HasSpellCooldown(triggered_spell_id))
        return false;

    if(basepoints0)
        CastCustomSpell(target,triggered_spell_id,&basepoints0,NULL,NULL,true,castItem,triggeredByAura);
    else
        CastSpell(target,triggered_spell_id,true,castItem,triggeredByAura);

    if( cooldown && GetTypeId()==TYPEID_PLAYER )
        ((Player*)this)->AddSpellCooldown(triggered_spell_id,0,time(NULL) + cooldown);

    return true;
}

bool Unit::HandleSpellCritChanceAuraProc(Unit *pVictim, uint32 /*damage*/, Aura* triggeredByAura, SpellEntry const * /*procSpell*/, uint32 /*procFlag*/, uint32 /*procEx*/, uint32 cooldown)
{
    SpellEntry const *triggeredByAuraSpell = triggeredByAura->GetSpellProto();

    Item* castItem = triggeredByAura->GetCastItemGUID() && GetTypeId()==TYPEID_PLAYER
        ? ((Player*)this)->GetItemByGuid(triggeredByAura->GetCastItemGUID()) : NULL;

    uint32 triggered_spell_id = 0;
    Unit* target = pVictim;
    int32 basepoints0 = 0;

    switch(triggeredByAuraSpell->SpellFamilyName)
    {
        case SPELLFAMILY_MAGE:
        {
            switch(triggeredByAuraSpell->Id)
            {
                // Focus Magic
                case 54646:
                {
                    Unit* caster = triggeredByAura->GetCaster();
                    if(!caster)
                        return false;

                    triggered_spell_id = 54648;
                    target = caster;
                    break;
                }
            }
        }
    }

    // processed charge only counting case
    if(!triggered_spell_id)
        return true;

    SpellEntry const* triggerEntry = sSpellStore.LookupEntry(triggered_spell_id);

    if(!triggerEntry)
    {
        sLog.outError("Unit::HandleHasteAuraProc: Spell %u have not existed triggered spell %u",triggeredByAuraSpell->Id,triggered_spell_id);
        return false;
    }

    // default case
    if(!target || target!=this && !target->isAlive())
        return false;

    if( cooldown && GetTypeId()==TYPEID_PLAYER && ((Player*)this)->HasSpellCooldown(triggered_spell_id))
        return false;

    if(basepoints0)
        CastCustomSpell(target,triggered_spell_id,&basepoints0,NULL,NULL,true,castItem,triggeredByAura);
    else
        CastSpell(target,triggered_spell_id,true,castItem,triggeredByAura);

    if( cooldown && GetTypeId()==TYPEID_PLAYER )
        ((Player*)this)->AddSpellCooldown(triggered_spell_id,0,time(NULL) + cooldown);

    return true;
}

bool Unit::HandleDummyAuraProc(Unit *pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const * procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown)
{
    SpellEntry const *dummySpell = triggeredByAura->GetSpellProto ();
    SpellEffectIndex effIndex = triggeredByAura->GetEffIndex();
    int32  triggerAmount = triggeredByAura->GetModifier()->m_amount;

    Item* castItem = triggeredByAura->GetCastItemGUID() && GetTypeId()==TYPEID_PLAYER
        ? ((Player*)this)->GetItemByGuid(triggeredByAura->GetCastItemGUID()) : NULL;

    uint32 triggered_spell_id = 0;
    Unit* target = pVictim;
    int32 basepoints0 = 0;

    switch(dummySpell->SpellFamilyName)
    {
        case SPELLFAMILY_GENERIC:
        {
            switch (dummySpell->Id)
            {
                // Eye for an Eye
                case 9799:
                case 25988:
                {
                    // return damage % to attacker but < 50% own total health
                    basepoints0 = triggerAmount*int32(damage)/100;
                    if(basepoints0 > (int32)GetMaxHealth()/2)
                        basepoints0 = (int32)GetMaxHealth()/2;

                    triggered_spell_id = 25997;
                    break;
                }
                // Sweeping Strikes (NPC spells may be)
                case 18765:
                case 35429:
                {
                    // prevent chain of triggered spell from same triggered spell
                    if(procSpell && procSpell->Id == 26654)
                        return false;

                    target = SelectRandomUnfriendlyTarget(pVictim);
                    if(!target)
                        return false;

                    triggered_spell_id = 26654;
                    break;
                }
                // Twisted Reflection (boss spell)
                case 21063:
                    triggered_spell_id = 21064;
                    break;
                // Unstable Power
                case 24658:
                {
                    if (!procSpell || procSpell->Id == 24659)
                        return false;
                    // Need remove one 24659 aura
                    RemoveSingleSpellAurasFromStack(24659);
                    return true;
                }
                // Restless Strength
                case 24661:
                {
                    // Need remove one 24662 aura
                    RemoveSingleSpellAurasFromStack(24662);
                    return true;
                }
                // Adaptive Warding (Frostfire Regalia set)
                case 28764:
                {
                    if(!procSpell)
                        return false;

                    // find Mage Armor
                    bool found = false;
                    AuraList const& mRegenInterupt = GetAurasByType(SPELL_AURA_MOD_MANA_REGEN_INTERRUPT);
                    for(AuraList::const_iterator iter = mRegenInterupt.begin(); iter != mRegenInterupt.end(); ++iter)
                    {
                        if(SpellEntry const* iterSpellProto = (*iter)->GetSpellProto())
                        {
                            if(iterSpellProto->SpellFamilyName==SPELLFAMILY_MAGE && (iterSpellProto->SpellFamilyFlags & UI64LIT(0x10000000)))
                            {
                                found=true;
                                break;
                            }
                        }
                    }
                    if(!found)
                        return false;

                    switch(GetFirstSchoolInMask(GetSpellSchoolMask(procSpell)))
                    {
                        case SPELL_SCHOOL_NORMAL:
                        case SPELL_SCHOOL_HOLY:
                            return false;                   // ignored
                        case SPELL_SCHOOL_FIRE:   triggered_spell_id = 28765; break;
                        case SPELL_SCHOOL_NATURE: triggered_spell_id = 28768; break;
                        case SPELL_SCHOOL_FROST:  triggered_spell_id = 28766; break;
                        case SPELL_SCHOOL_SHADOW: triggered_spell_id = 28769; break;
                        case SPELL_SCHOOL_ARCANE: triggered_spell_id = 28770; break;
                        default:
                            return false;
                    }

                    target = this;
                    break;
                }
                // Obsidian Armor (Justice Bearer`s Pauldrons shoulder)
                case 27539:
                {
                    if(!procSpell)
                        return false;

                    switch(GetFirstSchoolInMask(GetSpellSchoolMask(procSpell)))
                    {
                        case SPELL_SCHOOL_NORMAL:
                            return false;                   // ignore
                        case SPELL_SCHOOL_HOLY:   triggered_spell_id = 27536; break;
                        case SPELL_SCHOOL_FIRE:   triggered_spell_id = 27533; break;
                        case SPELL_SCHOOL_NATURE: triggered_spell_id = 27538; break;
                        case SPELL_SCHOOL_FROST:  triggered_spell_id = 27534; break;
                        case SPELL_SCHOOL_SHADOW: triggered_spell_id = 27535; break;
                        case SPELL_SCHOOL_ARCANE: triggered_spell_id = 27540; break;
                        default:
                            return false;
                    }

                    target = this;
                    break;
                }
                // Mana Leech (Passive) (Priest Pet Aura)
                case 28305:
                {
                    // Cast on owner
                    target = GetOwner();
                    if(!target)
                        return false;

                    triggered_spell_id = 34650;
                    break;
                }
                // Divine purpose
                case 31871:
                case 31872:
                {
                    // Roll chane
                    if (!roll_chance_i(triggerAmount))
                        return false;

                    // Remove any stun effect on target
                    AuraMap& Auras = pVictim->GetAuras();
                    for(AuraMap::const_iterator iter = Auras.begin(); iter != Auras.end();)
                    {
                        SpellEntry const *spell = iter->second->GetSpellProto();
                        if( spell->Mechanic == MECHANIC_STUN ||
                            spell->EffectMechanic[iter->second->GetEffIndex()] == MECHANIC_STUN)
                        {
                            pVictim->RemoveAurasDueToSpell(spell->Id);
                            iter = Auras.begin();
                        }
                        else
                            ++iter;
                    }
                    return true;
                }
                // Mark of Malice
                case 33493:
                {
                    // Cast finish spell at last charge
                    if (triggeredByAura->GetAuraCharges() > 1)
                        return false;

                    target = this;
                    triggered_spell_id = 33494;
                    break;
                }
                // Vampiric Aura (boss spell)
                case 38196:
                {
                    basepoints0 = 3 * damage;               // 300%
                    if (basepoints0 < 0)
                        return false;

                    triggered_spell_id = 31285;
                    target = this;
                    break;
                }
                // Aura of Madness (Darkmoon Card: Madness trinket)
                //=====================================================
                // 39511 Sociopath: +35 strength (Paladin, Rogue, Druid, Warrior)
                // 40997 Delusional: +70 attack power (Rogue, Hunter, Paladin, Warrior, Druid)
                // 40998 Kleptomania: +35 agility (Warrior, Rogue, Paladin, Hunter, Druid)
                // 40999 Megalomania: +41 damage/healing (Druid, Shaman, Priest, Warlock, Mage, Paladin)
                // 41002 Paranoia: +35 spell/melee/ranged crit strike rating (All classes)
                // 41005 Manic: +35 haste (spell, melee and ranged) (All classes)
                // 41009 Narcissism: +35 intellect (Druid, Shaman, Priest, Warlock, Mage, Paladin, Hunter)
                // 41011 Martyr Complex: +35 stamina (All classes)
                // 41406 Dementia: Every 5 seconds either gives you +5% damage/healing. (Druid, Shaman, Priest, Warlock, Mage, Paladin)
                // 41409 Dementia: Every 5 seconds either gives you -5% damage/healing. (Druid, Shaman, Priest, Warlock, Mage, Paladin)
                case 39446:
                {
                    if(GetTypeId() != TYPEID_PLAYER)
                        return false;

                    // Select class defined buff
                    switch (getClass())
                    {
                        case CLASS_PALADIN:                 // 39511,40997,40998,40999,41002,41005,41009,41011,41409
                        case CLASS_DRUID:                   // 39511,40997,40998,40999,41002,41005,41009,41011,41409
                        {
                            uint32 RandomSpell[]={39511,40997,40998,40999,41002,41005,41009,41011,41409};
                            triggered_spell_id = RandomSpell[ irand(0, sizeof(RandomSpell)/sizeof(uint32) - 1) ];
                            break;
                        }
                        case CLASS_ROGUE:                   // 39511,40997,40998,41002,41005,41011
                        case CLASS_WARRIOR:                 // 39511,40997,40998,41002,41005,41011
                        {
                            uint32 RandomSpell[]={39511,40997,40998,41002,41005,41011};
                            triggered_spell_id = RandomSpell[ irand(0, sizeof(RandomSpell)/sizeof(uint32) - 1) ];
                            break;
                        }
                        case CLASS_PRIEST:                  // 40999,41002,41005,41009,41011,41406,41409
                        case CLASS_SHAMAN:                  // 40999,41002,41005,41009,41011,41406,41409
                        case CLASS_MAGE:                    // 40999,41002,41005,41009,41011,41406,41409
                        case CLASS_WARLOCK:                 // 40999,41002,41005,41009,41011,41406,41409
                        {
                            uint32 RandomSpell[]={40999,41002,41005,41009,41011,41406,41409};
                            triggered_spell_id = RandomSpell[ irand(0, sizeof(RandomSpell)/sizeof(uint32) - 1) ];
                            break;
                        }
                        case CLASS_HUNTER:                  // 40997,40999,41002,41005,41009,41011,41406,41409
                        {
                            uint32 RandomSpell[]={40997,40999,41002,41005,41009,41011,41406,41409};
                            triggered_spell_id = RandomSpell[ irand(0, sizeof(RandomSpell)/sizeof(uint32) - 1) ];
                            break;
                        }
                        default:
                            return false;
                    }

                    target = this;
                    if (roll_chance_i(10))
                        ((Player*)this)->Say("This is Madness!", LANG_UNIVERSAL);
                    break;
                }
                // Sunwell Exalted Caster Neck (Shattered Sun Pendant of Acumen neck)
                // cast 45479 Light's Wrath if Exalted by Aldor
                // cast 45429 Arcane Bolt if Exalted by Scryers
                case 45481:
                {
                    if(GetTypeId() != TYPEID_PLAYER)
                        return false;

                    // Get Aldor reputation rank
                    if (((Player *)this)->GetReputationRank(932) == REP_EXALTED)
                    {
                        target = this;
                        triggered_spell_id = 45479;
                        break;
                    }
                    // Get Scryers reputation rank
                    if (((Player *)this)->GetReputationRank(934) == REP_EXALTED)
                    {
                        // triggered at positive/self casts also, current attack target used then
                        if(IsFriendlyTo(target))
                        {
                            target = getVictim();
                            if(!target)
                            {
                                uint64 selected_guid = ((Player *)this)->GetSelection();
                                target = ObjectAccessor::GetUnit(*this,selected_guid);
                                if(!target)
                                    return false;
                            }
                            if(IsFriendlyTo(target))
                                return false;
                        }

                        triggered_spell_id = 45429;
                        break;
                    }
                    return false;
                }
                // Sunwell Exalted Melee Neck (Shattered Sun Pendant of Might neck)
                // cast 45480 Light's Strength if Exalted by Aldor
                // cast 45428 Arcane Strike if Exalted by Scryers
                case 45482:
                {
                    if(GetTypeId() != TYPEID_PLAYER)
                        return false;

                    // Get Aldor reputation rank
                    if (((Player *)this)->GetReputationRank(932) == REP_EXALTED)
                    {
                        target = this;
                        triggered_spell_id = 45480;
                        break;
                    }
                    // Get Scryers reputation rank
                    if (((Player *)this)->GetReputationRank(934) == REP_EXALTED)
                    {
                        triggered_spell_id = 45428;
                        break;
                    }
                    return false;
                }
                // Sunwell Exalted Tank Neck (Shattered Sun Pendant of Resolve neck)
                // cast 45431 Arcane Insight if Exalted by Aldor
                // cast 45432 Light's Ward if Exalted by Scryers
                case 45483:
                {
                    if(GetTypeId() != TYPEID_PLAYER)
                        return false;

                    // Get Aldor reputation rank
                    if (((Player *)this)->GetReputationRank(932) == REP_EXALTED)
                    {
                        target = this;
                        triggered_spell_id = 45432;
                        break;
                    }
                    // Get Scryers reputation rank
                    if (((Player *)this)->GetReputationRank(934) == REP_EXALTED)
                    {
                        target = this;
                        triggered_spell_id = 45431;
                        break;
                    }
                    return false;
                }
                // Sunwell Exalted Healer Neck (Shattered Sun Pendant of Restoration neck)
                // cast 45478 Light's Salvation if Exalted by Aldor
                // cast 45430 Arcane Surge if Exalted by Scryers
                case 45484:
                {
                    if(GetTypeId() != TYPEID_PLAYER)
                        return false;

                    // Get Aldor reputation rank
                    if (((Player *)this)->GetReputationRank(932) == REP_EXALTED)
                    {
                        target = this;
                        triggered_spell_id = 45478;
                        break;
                    }
                    // Get Scryers reputation rank
                    if (((Player *)this)->GetReputationRank(934) == REP_EXALTED)
                    {
                        triggered_spell_id = 45430;
                        break;
                    }
                    return false;
                }
                /*
                // Sunwell Exalted Caster Neck (??? neck)
                // cast ??? Light's Wrath if Exalted by Aldor
                // cast ??? Arcane Bolt if Exalted by Scryers*/
                case 46569:
                    return false;                           // old unused version
                // Living Seed
                case 48504:
                {
                    triggered_spell_id = 48503;
                    basepoints0 = triggerAmount;
                    target = this;
                    break;
                }
                // Vampiric Touch (generic, used by some boss)
                case 52723:
                case 60501:
                {
                    triggered_spell_id = 52724;
                    basepoints0 = damage / 2;
                    target = this;
                    break;
                }
                // Shadowfiend Death (Gain mana if pet dies with Glyph of Shadowfiend)
                case 57989:
                {
                    Unit *owner = GetOwner();
                    if (!owner || owner->GetTypeId() != TYPEID_PLAYER)
                        return false;

                    // Glyph of Shadowfiend (need cast as self cast for owner, no hidden cooldown)
                    owner->CastSpell(owner,58227,true,castItem,triggeredByAura);
                    return true;
                }
                // Glyph of Life Tap
                case 63320:
                    triggered_spell_id = 63321;
                    break;
                // Item - Shadowmourne Legendary
                case 71903:
                {
                    if (!roll_chance_i(triggerAmount))
                        return false;

                    Aura *aur = GetAura(71905, EFFECT_INDEX_0);
                    if (aur && uint32(aur->GetStackAmount() + 1) >= aur->GetSpellProto()->StackAmount)
                    {
                        RemoveAurasDueToSpell(71905);
                        CastSpell(this, 71904, true);       // Chaos Bane
                        return true;
                    }
                    else
                        triggered_spell_id = 71905;

                    break;
                }
            }
            break;
        }
        case SPELLFAMILY_MAGE:
        {
            // Magic Absorption
            if (dummySpell->SpellIconID == 459)             // only this spell have SpellIconID == 459 and dummy aura
            {
                if (getPowerType() != POWER_MANA)
                    return false;

                // mana reward
                basepoints0 = (triggerAmount * GetMaxPower(POWER_MANA) / 100);
                target = this;
                triggered_spell_id = 29442;
                break;
            }
            // Master of Elements
            if (dummySpell->SpellIconID == 1920)
            {
                if(!procSpell)
                    return false;

                // mana cost save
                int32 cost = procSpell->manaCost + procSpell->ManaCostPercentage * GetCreateMana() / 100;
                basepoints0 = cost * triggerAmount/100;
                if( basepoints0 <=0 )
                    return false;

                target = this;
                triggered_spell_id = 29077;
                break;
            }

            // Arcane Potency
            if (dummySpell->SpellIconID == 2120)
            {
                if(!procSpell)
                    return false;

                target = this;
                switch (dummySpell->Id)
                {
                    case 31571: triggered_spell_id = 57529; break;
                    case 31572: triggered_spell_id = 57531; break;
                    default:
                        sLog.outError("Unit::HandleDummyAuraProc: non handled spell id: %u",dummySpell->Id);
                        return false;
                }
                break;
            }

            // Hot Streak
            if (dummySpell->SpellIconID == 2999)
            {
                if (effIndex != EFFECT_INDEX_0)
                    return true;
                Aura *counter = GetAura(triggeredByAura->GetId(), EFFECT_INDEX_1);
                if (!counter)
                    return true;

                // Count spell criticals in a row in second aura
                Modifier *mod = counter->GetModifier();
                if (procEx & PROC_EX_CRITICAL_HIT)
                {
                    mod->m_amount *=2;
                    if (mod->m_amount < 100) // not enough
                        return true;
                    // Crititcal counted -> roll chance
                    if (roll_chance_i(triggerAmount))
                       CastSpell(this, 48108, true, castItem, triggeredByAura);
                }
                mod->m_amount = 25;
                return true;
            }
            // Burnout
            if (dummySpell->SpellIconID == 2998)
            {
                if(!procSpell)
                    return false;

                int32 cost = procSpell->manaCost + procSpell->ManaCostPercentage * GetCreateMana() / 100;
                basepoints0 = cost * triggerAmount/100;
                if( basepoints0 <=0 )
                    return false;
                triggered_spell_id = 44450;
                target = this;
                break;
            }
            // Incanter's Regalia set (add trigger chance to Mana Shield)
            if (dummySpell->SpellFamilyFlags & UI64LIT(0x0000000000008000))
            {
                if(GetTypeId() != TYPEID_PLAYER)
                    return false;

                target = this;
                triggered_spell_id = 37436;
                break;
            }
            switch(dummySpell->Id)
            {
                // Ignite
                case 11119:
                case 11120:
                case 12846:
                case 12847:
                case 12848:
                {
                    switch (dummySpell->Id)
                    {
                        case 11119: basepoints0 = int32(0.04f*damage); break;
                        case 11120: basepoints0 = int32(0.08f*damage); break;
                        case 12846: basepoints0 = int32(0.12f*damage); break;
                        case 12847: basepoints0 = int32(0.16f*damage); break;
                        case 12848: basepoints0 = int32(0.20f*damage); break;
                        default:
                            sLog.outError("Unit::HandleDummyAuraProc: non handled spell id: %u (IG)",dummySpell->Id);
                            return false;
                    }

                    triggered_spell_id = 12654;
                    break;
                }
                // Combustion
                case 11129:
                {
                    //last charge and crit
                    if (triggeredByAura->GetAuraCharges() <= 1 && (procEx & PROC_EX_CRITICAL_HIT) )
                        return true;                        // charge counting (will removed)

                    CastSpell(this, 28682, true, castItem, triggeredByAura);
                    return (procEx & PROC_EX_CRITICAL_HIT); // charge update only at crit hits, no hidden cooldowns
                }
                // Glyph of Ice Block
                case 56372:
                {
                    if (GetTypeId() != TYPEID_PLAYER)
                        return false;

                    // not 100% safe with client version switches but for 3.1.3 no spells with cooldown that can have mage player except Frost Nova.
                    ((Player*)this)->RemoveSpellCategoryCooldown(35, true);
                    return true;
                }
                // Glyph of Polymorph
                case 56375:
                {
                    if (!pVictim || !pVictim->isAlive())
                        return false;

                    pVictim->RemoveSpellsCausingAura(SPELL_AURA_PERIODIC_DAMAGE);
                    pVictim->RemoveSpellsCausingAura(SPELL_AURA_PERIODIC_DAMAGE_PERCENT);
                    return true;
                }
            }
            break;
        }
        case SPELLFAMILY_WARRIOR:
        {
            // Retaliation
            if (dummySpell->SpellFamilyFlags == UI64LIT(0x0000000800000000))
            {
                // check attack comes not from behind
                if (!HasInArc(M_PI_F, pVictim))
                    return false;

                triggered_spell_id = 22858;
                break;
            }
            // Second Wind
            if (dummySpell->SpellIconID == 1697)
            {
                // only for spells and hit/crit (trigger start always) and not start from self casted spells (5530 Mace Stun Effect for example)
                if (procSpell == 0 || !(procEx & (PROC_EX_NORMAL_HIT|PROC_EX_CRITICAL_HIT)) || this == pVictim)
                    return false;
                // Need stun or root mechanic
                if (!(GetAllSpellMechanicMask(procSpell) & IMMUNE_TO_ROOT_AND_STUN_MASK))
                    return false;

                switch (dummySpell->Id)
                {
                    case 29838: triggered_spell_id=29842; break;
                    case 29834: triggered_spell_id=29841; break;
                    case 42770: triggered_spell_id=42771; break;
                    default:
                        sLog.outError("Unit::HandleDummyAuraProc: non handled spell id: %u (SW)",dummySpell->Id);
                    return false;
                }

                target = this;
                break;
            }
            // Damage Shield
            if (dummySpell->SpellIconID == 3214)
            {
                triggered_spell_id = 59653;
                basepoints0 = GetShieldBlockValue() * triggerAmount / 100;
                break;
            }

            // Sweeping Strikes
            if (dummySpell->Id == 12328)
            {
                // prevent chain of triggered spell from same triggered spell
                if(procSpell && procSpell->Id == 26654)
                    return false;

                target = SelectRandomUnfriendlyTarget(pVictim);
                if(!target)
                    return false;

                triggered_spell_id = 26654;
                break;
            }
            break;
        }
        case SPELLFAMILY_WARLOCK:
        {
            // Seed of Corruption
            if (dummySpell->SpellFamilyFlags & UI64LIT(0x0000001000000000))
            {
                Modifier* mod = triggeredByAura->GetModifier();
                // if damage is more than need or target die from damage deal finish spell
                if( mod->m_amount <= (int32)damage || GetHealth() <= damage )
                {
                    // remember guid before aura delete
                    uint64 casterGuid = triggeredByAura->GetCasterGUID();

                    // Remove aura (before cast for prevent infinite loop handlers)
                    RemoveAurasDueToSpell(triggeredByAura->GetId());

                    // Cast finish spell (triggeredByAura already not exist!)
                    CastSpell(this, 27285, true, castItem, NULL, casterGuid);
                    return true;                            // no hidden cooldown
                }

                // Damage counting
                mod->m_amount-=damage;
                return true;
            }
            // Seed of Corruption (Mobs cast) - no die req
            if (dummySpell->SpellFamilyFlags == UI64LIT(0x0) && dummySpell->SpellIconID == 1932)
            {
                Modifier* mod = triggeredByAura->GetModifier();
                // if damage is more than need deal finish spell
                if( mod->m_amount <= (int32)damage )
                {
                    // remember guid before aura delete
                    uint64 casterGuid = triggeredByAura->GetCasterGUID();

                    // Remove aura (before cast for prevent infinite loop handlers)
                    RemoveAurasDueToSpell(triggeredByAura->GetId());

                    // Cast finish spell (triggeredByAura already not exist!)
                    CastSpell(this, 32865, true, castItem, NULL, casterGuid);
                    return true;                            // no hidden cooldown
                }
                // Damage counting
                mod->m_amount-=damage;
                return true;
            }
            // Fel Synergy
            if (dummySpell->SpellIconID == 3222)
            {
                target = GetPet();
                if (!target)
                    return false;
                basepoints0 = damage * triggerAmount / 100;
                triggered_spell_id = 54181;
                break;
            }
            switch(dummySpell->Id)
            {
                // Nightfall & Glyph of Corruption
                case 18094:
                case 18095:
                case 56218:
                {
                    target = this;
                    triggered_spell_id = 17941;
                    break;
                }
                //Soul Leech
                case 30293:
                case 30295:
                case 30296:
                {
                    // health
                    basepoints0 = int32(damage*triggerAmount/100);
                    target = this;
                    triggered_spell_id = 30294;
                    break;
                }
                // Shadowflame (Voidheart Raiment set bonus)
                case 37377:
                {
                    triggered_spell_id = 37379;
                    break;
                }
                // Pet Healing (Corruptor Raiment or Rift Stalker Armor)
                case 37381:
                {
                    target = GetPet();
                    if(!target)
                        return false;

                    // heal amount
                    basepoints0 = damage * triggerAmount/100;
                    triggered_spell_id = 37382;
                    break;
                }
                // Shadowflame Hellfire (Voidheart Raiment set bonus)
                case 39437:
                {
                    triggered_spell_id = 37378;
                    break;
                }
                // Siphon Life
                case 63108:
                {
                    basepoints0 = int32(damage * triggerAmount / 100);
                    triggered_spell_id = 63106;
                    break;
                }
            }
            break;
        }
        case SPELLFAMILY_PRIEST:
        {
            // Vampiric Touch
            if (dummySpell->SpellFamilyFlags & UI64LIT(0x0000040000000000))
            {
                if(!pVictim || !pVictim->isAlive())
                    return false;

                // pVictim is caster of aura
                if(triggeredByAura->GetCasterGUID() != pVictim->GetGUID())
                    return false;

                // Energize 0.25% of max. mana
                pVictim->CastSpell(pVictim,57669,true,castItem,triggeredByAura);
                return true;                                // no hidden cooldown
            }

            switch(dummySpell->SpellIconID)
            {
                // Improved Shadowform
                case 217:
                {
                    if(!roll_chance_i(triggerAmount))
                        return false;

                    RemoveSpellsCausingAura(SPELL_AURA_MOD_ROOT);
                    RemoveSpellsCausingAura(SPELL_AURA_MOD_DECREASE_SPEED);
                    break;
                }
                // Divine Aegis
                case 2820:
                {
                    basepoints0 = damage * triggerAmount/100;
                    triggered_spell_id = 47753;
                    break;
                }
                // Empowered Renew
                case 3021:
                {
                    if (!procSpell)
                        return false;

                    Aura* healingAura = pVictim->GetAura(procSpell->Id, EFFECT_INDEX_0);
                    if (!healingAura)
                        return false;

                    int32 healingfromticks = SpellHealingBonus(pVictim, procSpell, (healingAura->GetModifier()->m_amount* GetSpellAuraMaxTicks(procSpell)), DOT);
                    basepoints0 = healingfromticks * triggerAmount / 100;
                    triggered_spell_id = 63544;
                    break;
                }
                // Improved Devouring Plague
                case 3790:
                {
                    if (!procSpell)
                        return false;

                    Aura* leachAura = pVictim->GetAura(procSpell->Id, EFFECT_INDEX_0);
                    if (!leachAura)
                        return false;

                    int32 damagefromticks = SpellDamageBonus(pVictim, procSpell, (leachAura->GetModifier()->m_amount* GetSpellAuraMaxTicks(procSpell)), DOT);
                    basepoints0 = damagefromticks * triggerAmount / 100;
                    triggered_spell_id = 63675;
                    break;
                }
            }

            switch(dummySpell->Id)
            {
                // Vampiric Embrace
                case 15286:
                {
                    // Return if self damage
                    if (this == pVictim)
                        return false;

                    // Heal amount - Self/Team
                    int32 team = triggerAmount*damage/500;
                    int32 self = triggerAmount*damage/100 - team;
                    CastCustomSpell(this,15290,&team,&self,NULL,true,castItem,triggeredByAura);
                    return true;                                // no hidden cooldown
                }
                // Priest Tier 6 Trinket (Ashtongue Talisman of Acumen)
                case 40438:
                {
                    // Shadow Word: Pain
                    if (procSpell->SpellFamilyFlags & UI64LIT(0x0000000000008000))
                        triggered_spell_id = 40441;
                    // Renew
                    else if (procSpell->SpellFamilyFlags & UI64LIT(0x0000000000000010))
                        triggered_spell_id = 40440;
                    else
                        return false;

                    target = this;
                    break;
                }
                // Oracle Healing Bonus ("Garments of the Oracle" set)
                case 26169:
                {
                    // heal amount
                    basepoints0 = int32(damage * 10/100);
                    target = this;
                    triggered_spell_id = 26170;
                    break;
                }
                // Frozen Shadoweave (Shadow's Embrace set) warning! its not only priest set
                case 39372:
                {
                    if(!procSpell || (GetSpellSchoolMask(procSpell) & (SPELL_SCHOOL_MASK_FROST | SPELL_SCHOOL_MASK_SHADOW))==0 )
                        return false;

                    // heal amount
                    basepoints0 = damage * triggerAmount/100;
                    target = this;
                    triggered_spell_id = 39373;
                    break;
                }
                // Greater Heal (Vestments of Faith (Priest Tier 3) - 4 pieces bonus)
                case 28809:
                {
                    triggered_spell_id = 28810;
                    break;
                }
                // Glyph of Dispel Magic
                case 55677:
                {
                    if(!target->IsFriendlyTo(this))
                        return false;

                    basepoints0 = int32(target->GetMaxHealth() * triggerAmount / 100);
                    triggered_spell_id = 56131;
                    break;
                }
            }
            break;
        }
        case SPELLFAMILY_DRUID:
        {
            switch(dummySpell->Id)
            {
                // Leader of the Pack
                case 24932:
                {
                    // dummy m_amount store health percent (!=0 if Improved Leader of the Pack applied)
                    int32 heal_percent = triggeredByAura->GetModifier()->m_amount;
                    if (!heal_percent)
                        return false;

                    // check explicitly only to prevent mana cast when halth cast cooldown
                    if (cooldown && ((Player*)this)->HasSpellCooldown(34299))
                        return false;

                    // health
                    triggered_spell_id = 34299;
                    basepoints0 = GetMaxHealth() * heal_percent / 100;
                    target = this;

                    // mana to caster
                    if (triggeredByAura->GetCasterGUID() == GetGUID())
                    {
                        if (SpellEntry const* manaCastEntry = sSpellStore.LookupEntry(60889))
                        {
                            int32 mana_percent = manaCastEntry->CalculateSimpleValue(EFFECT_INDEX_0) * heal_percent;
                            CastCustomSpell(this, manaCastEntry, &mana_percent, NULL, NULL, true, castItem, triggeredByAura);
                        }
                    }
                    break;
                }
                // Healing Touch (Dreamwalker Raiment set)
                case 28719:
                {
                    // mana back
                    basepoints0 = int32(procSpell->manaCost * 30 / 100);
                    target = this;
                    triggered_spell_id = 28742;
                    break;
                }
                // Healing Touch Refund (Idol of Longevity trinket)
                case 28847:
                {
                    target = this;
                    triggered_spell_id = 28848;
                    break;
                }
                // Mana Restore (Malorne Raiment set / Malorne Regalia set)
                case 37288:
                case 37295:
                {
                    target = this;
                    triggered_spell_id = 37238;
                    break;
                }
                // Druid Tier 6 Trinket
                case 40442:
                {
                    float  chance;

                    // Starfire
                    if (procSpell->SpellFamilyFlags & UI64LIT(0x0000000000000004))
                    {
                        triggered_spell_id = 40445;
                        chance = 25.0f;
                    }
                    // Rejuvenation
                    else if (procSpell->SpellFamilyFlags & UI64LIT(0x0000000000000010))
                    {
                        triggered_spell_id = 40446;
                        chance = 25.0f;
                    }
                    // Mangle (Bear) and Mangle (Cat)
                    else if (procSpell->SpellFamilyFlags & UI64LIT(0x0000044000000000))
                    {
                        triggered_spell_id = 40452;
                        chance = 40.0f;
                    }
                    else
                        return false;

                    if (!roll_chance_f(chance))
                        return false;

                    target = this;
                    break;
                }
                // Maim Interrupt
                case 44835:
                {
                    // Deadly Interrupt Effect
                    triggered_spell_id = 32747;
                    break;
                }
                // Glyph of Rejuvenation
                case 54754:
                {
                    // less 50% health
                    if (pVictim->GetMaxHealth() < 2 * pVictim->GetHealth())
                        return false;
                    basepoints0 = triggerAmount * damage / 100;
                    triggered_spell_id = 54755;
                    break;
                }
                // Item - Druid T10 Restoration 4P Bonus (Rejuvenation)
                case 70664:
                {
                    if (!procSpell || GetTypeId() != TYPEID_PLAYER)
                        return false;

                    float radius;
                    if (procSpell->EffectRadiusIndex[EFFECT_INDEX_0])
                        radius = GetSpellRadius(sSpellRadiusStore.LookupEntry(procSpell->EffectRadiusIndex[EFFECT_INDEX_0]));
                    else
                        radius = GetSpellMaxRange(sSpellRangeStore.LookupEntry(procSpell->rangeIndex));

                    ((Player*)this)->ApplySpellMod(procSpell->Id, SPELLMOD_RADIUS, radius,NULL);

                    Unit *second = pVictim->SelectRandomFriendlyTarget(pVictim, radius);

                    if (!second)
                        return false;

                    pVictim->CastSpell(second, procSpell, true, NULL, triggeredByAura, GetGUID());
                    return true;
                }
            }
            // Eclipse
            if (dummySpell->SpellIconID == 2856)
            {
                if (!procSpell)
                    return false;
                // Only 0 aura can proc
                if (effIndex != EFFECT_INDEX_0)
                    return true;
                // Wrath crit
                if (procSpell->SpellFamilyFlags & UI64LIT(0x0000000000000001))
                {
                    if (HasAura(48517))
                        return false;
                    if (!roll_chance_i(60))
                        return false;
                    triggered_spell_id = 48518;
                    target = this;
                    break;
                }
                // Starfire crit
                if (procSpell->SpellFamilyFlags & UI64LIT(0x0000000000000004))
                {
                    if (HasAura(48518))
                        return false;
                    triggered_spell_id = 48517;
                    target = this;
                    break;
                }
                return false;
            }
            // Living Seed
            else if (dummySpell->SpellIconID == 2860)
            {
                triggered_spell_id = 48504;
                basepoints0 = triggerAmount * damage / 100;
                break;
            }
            break;
        }
        case SPELLFAMILY_ROGUE:
        {
            switch(dummySpell->Id)
            {
                // Deadly Throw Interrupt
                case 32748:
                {
                    // Prevent cast Deadly Throw Interrupt on self from last effect (apply dummy) of Deadly Throw
                    if (this == pVictim)
                        return false;

                    triggered_spell_id = 32747;
                    break;
                }
            }
            // Cut to the Chase
            if (dummySpell->SpellIconID == 2909)
            {
                // "refresh your Slice and Dice duration to its 5 combo point maximum"
                // lookup Slice and Dice
                AuraList const& sd = GetAurasByType(SPELL_AURA_MOD_HASTE);
                for(AuraList::const_iterator itr = sd.begin(); itr != sd.end(); ++itr)
                {
                    SpellEntry const *spellProto = (*itr)->GetSpellProto();
                    if (spellProto->SpellFamilyName == SPELLFAMILY_ROGUE &&
                        (spellProto->SpellFamilyFlags & UI64LIT(0x0000000000040000)))
                    {
                        (*itr)->SetAuraMaxDuration(GetSpellMaxDuration(spellProto));
                        (*itr)->RefreshAura();
                        return true;
                    }
                }
                return false;
            }
            // Deadly Brew
            if (dummySpell->SpellIconID == 2963)
            {
                triggered_spell_id = 44289;
                break;
            }
            // Quick Recovery
            if (dummySpell->SpellIconID == 2116)
            {
                if(!procSpell)
                    return false;

                // energy cost save
                basepoints0 = procSpell->manaCost * triggerAmount/100;
                if (basepoints0 <= 0)
                    return false;

                target = this;
                triggered_spell_id = 31663;
                break;
            }
            break;
        }
        case SPELLFAMILY_HUNTER:
        {
            // Aspect of the Viper
            if (dummySpell->SpellFamilyFlags & UI64LIT(0x4000000000000))
            {
                uint32 maxmana = GetMaxPower(POWER_MANA);
                basepoints0 = int32(maxmana* GetAttackTime(RANGED_ATTACK)/1000.0f/100.0f);

                target = this;
                triggered_spell_id = 34075;
                break;
            }
            // Thrill of the Hunt
            if (dummySpell->SpellIconID == 2236)
            {
                if(!procSpell)
                    return false;

                // mana cost save
                int32 mana = procSpell->manaCost + procSpell->ManaCostPercentage * GetCreateMana() / 100;
                basepoints0 = mana * 40/100;
                if(basepoints0 <= 0)
                    return false;

                target = this;
                triggered_spell_id = 34720;
                break;
            }
            // Hunting Party
            if ( dummySpell->SpellIconID == 3406 )
            {
                triggered_spell_id = 57669;
                target = this;
                break;
            }
            // Lock and Load
            if ( dummySpell->SpellIconID == 3579 )
            {
                // Proc only from periodic (from trap activation proc another aura of this spell)
                if (!(procFlag & PROC_FLAG_ON_DO_PERIODIC) || !roll_chance_i(triggerAmount))
                    return false;
                triggered_spell_id = 56453;
                target = this;
                break;
            }
            // Rapid Recuperation
            if ( dummySpell->SpellIconID == 3560 )
            {
                // This effect only from Rapid Killing (mana regen)
                if (!(procSpell->SpellFamilyFlags & UI64LIT(0x0100000000000000)))
                    return false;

                target = this;

                switch(dummySpell->Id)
                {
                    case 53228:                             // Rank 1
                        triggered_spell_id = 56654;
                        break;
                    case 53232:                             // Rank 2
                        triggered_spell_id = 58882;
                        break;
                }
                break;
            }
            // Glyph of Mend Pet
            if(dummySpell->Id == 57870)
            {
                pVictim->CastSpell(pVictim, 57894, true, NULL, NULL, GetGUID());
                return true;
            }
            break;
        }
        case SPELLFAMILY_PALADIN:
        {
            // Seal of Righteousness - melee proc dummy (addition ${$MWS*(0.022*$AP+0.044*$SPH)} damage)
            if ((dummySpell->SpellFamilyFlags & UI64LIT(0x000000008000000)) && effIndex == EFFECT_INDEX_0)
            {
                triggered_spell_id = 25742;
                float ap = GetTotalAttackPowerValue(BASE_ATTACK);
                int32 holy = SpellBaseDamageBonus(SPELL_SCHOOL_MASK_HOLY) +
                             SpellBaseDamageBonusForVictim(SPELL_SCHOOL_MASK_HOLY, pVictim);
                basepoints0 = GetAttackTime(BASE_ATTACK) * int32(ap*0.022f + 0.044f * holy) / 1000;
                break;
            }
            // Righteous Vengeance
            if (dummySpell->SpellIconID == 3025)
            {
                // 4 damage tick
                basepoints0 = triggerAmount*damage/400;
                triggered_spell_id = 61840;
                break;
            }
            // Sheath of Light
            if (dummySpell->SpellIconID == 3030)
            {
                // 4 healing tick
                basepoints0 = triggerAmount*damage/400;
                triggered_spell_id = 54203;
                break;
            }
            switch(dummySpell->Id)
            {
                // Judgement of Light
                case 20185:
                {
                    basepoints0 = int32( pVictim->GetMaxHealth() * triggeredByAura->GetModifier()->m_amount / 100 );
                    pVictim->CastCustomSpell(pVictim, 20267, &basepoints0, NULL, NULL, true, NULL, triggeredByAura);
                    return true;
                }
                // Judgement of Wisdom
                case 20186:
                {
                    if (pVictim->getPowerType() == POWER_MANA)
                    {
                        // 2% of maximum base mana
                        basepoints0 = int32(pVictim->GetCreateMana() * 2 / 100);
                        pVictim->CastCustomSpell(pVictim, 20268, &basepoints0, NULL, NULL, true, NULL, triggeredByAura);
                    }
                    return true;
                }
                // Heart of the Crusader (Rank 1)
                case 20335:
                    triggered_spell_id = 21183;
                    break;
                // Heart of the Crusader (Rank 2)
                case 20336:
                    triggered_spell_id = 54498;
                    break;
                // Heart of the Crusader (Rank 3)
                case 20337:
                    triggered_spell_id = 54499;
                    break;
                case 20911:                                 // Blessing of Sanctuary
                case 25899:                                 // Greater Blessing of Sanctuary
                {
                    target = this;
                    switch (target->getPowerType())
                    {
                        case POWER_MANA:
                            triggered_spell_id = 57319;
                            break;
                        default:
                            return false;
                    }
                    break;
                }
                // Holy Power (Redemption Armor set)
                case 28789:
                {
                    if(!pVictim)
                        return false;

                    // Set class defined buff
                    switch (pVictim->getClass())
                    {
                        case CLASS_PALADIN:
                        case CLASS_PRIEST:
                        case CLASS_SHAMAN:
                        case CLASS_DRUID:
                            triggered_spell_id = 28795;     // Increases the friendly target's mana regeneration by $s1 per 5 sec. for $d.
                            break;
                        case CLASS_MAGE:
                        case CLASS_WARLOCK:
                            triggered_spell_id = 28793;     // Increases the friendly target's spell damage and healing by up to $s1 for $d.
                            break;
                        case CLASS_HUNTER:
                        case CLASS_ROGUE:
                            triggered_spell_id = 28791;     // Increases the friendly target's attack power by $s1 for $d.
                            break;
                        case CLASS_WARRIOR:
                            triggered_spell_id = 28790;     // Increases the friendly target's armor
                            break;
                        default:
                            return false;
                    }
                    break;
                }
                // Spiritual Attunement
                case 31785:
                case 33776:
                {
                    // if healed by another unit (pVictim)
                    if(this == pVictim)
                        return false;

                    // heal amount
                    basepoints0 = triggerAmount*damage/100;
                    target = this;
                    triggered_spell_id = 31786;
                    break;
                }
                // Seal of Vengeance (damage calc on apply aura)
                case 31801:
                {
                    if (effIndex != EFFECT_INDEX_0)         // effect 1,2 used by seal unleashing code
                        return false;

                    // At melee attack or Hammer of the Righteous spell damage considered as melee attack
                    if ((procFlag & PROC_FLAG_SUCCESSFUL_MELEE_HIT) || (procSpell && procSpell->Id == 53595) )
                        triggered_spell_id = 31803;         // Holy Vengeance

                    // Add 5-stack effect from Holy Vengeance
                    int8 stacks = 0;
                    AuraList const& auras = target->GetAurasByType(SPELL_AURA_PERIODIC_DAMAGE);
                    for(AuraList::const_iterator itr = auras.begin(); itr!=auras.end(); ++itr)
                    {
                        if( ((*itr)->GetId() == 31803) && (*itr)->GetCasterGUID()==GetGUID())
                        {
                            stacks = (*itr)->GetStackAmount();
                            break;
                        }
                    }
                    if(stacks >= 5)
                        CastSpell(target,42463,true,NULL,triggeredByAura);
                    break;
                }
                // Judgements of the Wise
                case 31876:
                case 31877:
                case 31878:
                    // triggered only at casted Judgement spells, not at additional Judgement effects
                    if(!procSpell || procSpell->Category != 1210)
                        return false;

                    target = this;
                    triggered_spell_id = 31930;

                    // Replenishment
                    CastSpell(this, 57669, true, NULL, triggeredByAura);
                    break;
                // Paladin Tier 6 Trinket (Ashtongue Talisman of Zeal)
                case 40470:
                {
                    if (!procSpell)
                        return false;

                    float  chance;

                    // Flash of light/Holy light
                    if (procSpell->SpellFamilyFlags & UI64LIT(0x00000000C0000000))
                    {
                        triggered_spell_id = 40471;
                        chance = 15.0f;
                    }
                    // Judgement (any)
                    else if (GetSpellSpecific(procSpell->Id)==SPELL_JUDGEMENT)
                    {
                        triggered_spell_id = 40472;
                        chance = 50.0f;
                    }
                    else
                        return false;

                    if (!roll_chance_f(chance))
                        return false;

                    break;
                }
                // Light's Beacon (heal target area aura)
                case 53651:
                {
                    // not do bonus heal for explicit beacon focus healing
                    if (GetGUID() == triggeredByAura->GetCasterGUID())
                        return false;

                    // beacon
                    Unit* beacon = triggeredByAura->GetCaster();
                    if (!beacon)
                        return false;

                    // find caster main aura at beacon
                    Aura* dummy = NULL;
                    Unit::AuraList const& baa = beacon->GetAurasByType(SPELL_AURA_PERIODIC_TRIGGER_SPELL);
                    for(Unit::AuraList::const_iterator i = baa.begin(); i != baa.end(); ++i)
                    {
                        if ((*i)->GetId() == 53563 && (*i)->GetCasterGUID() == pVictim->GetGUID())
                        {
                            dummy = (*i);
                            break;
                        }
                    }

                    // original heal must be form beacon caster
                    if (!dummy)
                        return false;

                    triggered_spell_id = 53652;             // Beacon of Light
                    basepoints0 = triggeredByAura->GetModifier()->m_amount*damage/100;

                    // cast with original caster set but beacon to beacon for apply caster mods and avoid LoS check
                    beacon->CastCustomSpell(beacon,triggered_spell_id,&basepoints0,NULL,NULL,true,castItem,triggeredByAura,pVictim->GetGUID());
                    return true;
                }
                // Seal of Corruption (damage calc on apply aura)
                case 53736:
                {
                    if (effIndex != EFFECT_INDEX_0)         // effect 1,2 used by seal unleashing code
                        return false;

                    // At melee attack or Hammer of the Righteous spell damage considered as melee attack
                    if ((procFlag & PROC_FLAG_SUCCESSFUL_MELEE_HIT) || (procSpell && procSpell->Id == 53595))
                        triggered_spell_id = 53742;         // Blood Corruption

                    // Add 5-stack effect from Blood Corruption
                    int8 stacks = 0;
                    AuraList const& auras = target->GetAurasByType(SPELL_AURA_PERIODIC_DAMAGE);
                    for(AuraList::const_iterator itr = auras.begin(); itr!=auras.end(); ++itr)
                    {
                        if( ((*itr)->GetId() == 53742) && (*itr)->GetCasterGUID()==GetGUID())
                        {
                            stacks = (*itr)->GetStackAmount();
                            break;
                        }
                    }
                    if(stacks >= 5)
                        CastSpell(target,53739,true,NULL,triggeredByAura);
                    break;
                }
                // Glyph of Flash of Light
                case 54936:
                    {
                        triggered_spell_id = 54957;
                        basepoints0 = triggerAmount*damage/100;
                        break;
                    }
                    // Glyph of Holy Light
                case 54937:
                    {
                        triggered_spell_id = 54968;
                        basepoints0 = triggerAmount*damage/100;
                        break;
                    }
                // Glyph of Divinity
                case 54939:
                {
                    // Lookup base amount mana restore
                    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
                    {
                        if (procSpell->Effect[i] == SPELL_EFFECT_ENERGIZE)
                        {
                            int32 mana = procSpell->EffectBasePoints[i];
                            CastCustomSpell(this, 54986, NULL, &mana, NULL, true, castItem, triggeredByAura);
                            break;
                        }
                    }
                    return true;
                }
                // Sacred Shield (buff)
                case 58597:
                {
                    triggered_spell_id = 66922;
                    SpellEntry const* triggeredEntry = sSpellStore.LookupEntry(triggered_spell_id);
                    if (!triggeredEntry)
                        return false;

                    basepoints0 = int32(damage / (GetSpellDuration(triggeredEntry) / triggeredEntry->EffectAmplitude[EFFECT_INDEX_0]));
                    target = this;
                    break;
                }
                // Sacred Shield (talent rank)
                case 53601:
                {
                    triggered_spell_id = 58597;
                    target = this;
                    break;
                }
            }
            break;
        }
        case SPELLFAMILY_SHAMAN:
        {
            switch(dummySpell->Id)
            {
                // Totemic Power (The Earthshatterer set)
                case 28823:
                {
                    if( !pVictim )
                        return false;

                    // Set class defined buff
                    switch (pVictim->getClass())
                    {
                        case CLASS_PALADIN:
                        case CLASS_PRIEST:
                        case CLASS_SHAMAN:
                        case CLASS_DRUID:
                            triggered_spell_id = 28824;     // Increases the friendly target's mana regeneration by $s1 per 5 sec. for $d.
                            break;
                        case CLASS_MAGE:
                        case CLASS_WARLOCK:
                            triggered_spell_id = 28825;     // Increases the friendly target's spell damage and healing by up to $s1 for $d.
                            break;
                        case CLASS_HUNTER:
                        case CLASS_ROGUE:
                            triggered_spell_id = 28826;     // Increases the friendly target's attack power by $s1 for $d.
                            break;
                        case CLASS_WARRIOR:
                            triggered_spell_id = 28827;     // Increases the friendly target's armor
                            break;
                        default:
                            return false;
                    }
                    break;
                }
                // Lesser Healing Wave (Totem of Flowing Water Relic)
                case 28849:
                {
                    target = this;
                    triggered_spell_id = 28850;
                    break;
                }
                // Windfury Weapon (Passive) 1-5 Ranks
                case 33757:
                {
                    if(GetTypeId()!=TYPEID_PLAYER)
                        return false;

                    if(!castItem || !castItem->IsEquipped())
                        return false;

                    // custom cooldown processing case
                    if( cooldown && ((Player*)this)->HasSpellCooldown(dummySpell->Id))
                        return false;

                    // Now amount of extra power stored in 1 effect of Enchant spell
                    // Get it by item enchant id
                    uint32 spellId;
                    switch (castItem->GetEnchantmentId(EnchantmentSlot(TEMP_ENCHANTMENT_SLOT)))
                    {
                        case 283: spellId =  8232; break;   // 1 Rank
                        case 284: spellId =  8235; break;   // 2 Rank
                        case 525: spellId = 10486; break;   // 3 Rank
                        case 1669:spellId = 16362; break;   // 4 Rank
                        case 2636:spellId = 25505; break;   // 5 Rank
                        case 3785:spellId = 58801; break;   // 6 Rank
                        case 3786:spellId = 58803; break;   // 7 Rank
                        case 3787:spellId = 58804; break;   // 8 Rank
                        default:
                        {
                            sLog.outError("Unit::HandleDummyAuraProc: non handled item enchantment (rank?) %u for spell id: %u (Windfury)",
                                castItem->GetEnchantmentId(EnchantmentSlot(TEMP_ENCHANTMENT_SLOT)),dummySpell->Id);
                            return false;
                        }
                    }

                    SpellEntry const* windfurySpellEntry = sSpellStore.LookupEntry(spellId);
                    if(!windfurySpellEntry)
                    {
                        sLog.outError("Unit::HandleDummyAuraProc: non existed spell id: %u (Windfury)",spellId);
                        return false;
                    }

                    int32 extra_attack_power = CalculateSpellDamage(windfurySpellEntry, EFFECT_INDEX_1, windfurySpellEntry->EffectBasePoints[EFFECT_INDEX_1], pVictim);

                    // Off-Hand case
                    if ( castItem->GetSlot() == EQUIPMENT_SLOT_OFFHAND )
                    {
                        // Value gained from additional AP
                        basepoints0 = int32(extra_attack_power/14.0f * GetAttackTime(OFF_ATTACK)/1000/2);
                        triggered_spell_id = 33750;
                    }
                    // Main-Hand case
                    else
                    {
                        // Value gained from additional AP
                        basepoints0 = int32(extra_attack_power/14.0f * GetAttackTime(BASE_ATTACK)/1000);
                        triggered_spell_id = 25504;
                    }

                    // apply cooldown before cast to prevent processing itself
                    if( cooldown )
                        ((Player*)this)->AddSpellCooldown(dummySpell->Id,0,time(NULL) + cooldown);

                    // Attack Twice
                    for ( uint32 i = 0; i<2; ++i )
                        CastCustomSpell(pVictim,triggered_spell_id,&basepoints0,NULL,NULL,true,castItem,triggeredByAura);

                    return true;
                }
                // Shaman Tier 6 Trinket
                case 40463:
                {
                    if( !procSpell )
                        return false;

                    float  chance;
                    if (procSpell->SpellFamilyFlags & UI64LIT(0x0000000000000001))
                    {
                        triggered_spell_id = 40465;         // Lightning Bolt
                        chance = 15.0f;
                    }
                    else if (procSpell->SpellFamilyFlags & UI64LIT(0x0000000000000080))
                    {
                        triggered_spell_id = 40465;         // Lesser Healing Wave
                        chance = 10.0f;
                    }
                    else if (procSpell->SpellFamilyFlags & UI64LIT(0x0000001000000000))
                    {
                        triggered_spell_id = 40466;         // Stormstrike
                        chance = 50.0f;
                    }
                    else
                        return false;

                    if (!roll_chance_f(chance))
                        return false;

                    target = this;
                    break;
                }
                // Glyph of Healing Wave
                case 55440:
                {
                    // Not proc from self heals
                    if (this==pVictim)
                        return false;
                    basepoints0 = triggerAmount * damage / 100;
                    target = this;
                    triggered_spell_id = 55533;
                    break;
                }
                // Spirit Hunt
                case 58877:
                {
                    // Cast on owner
                    target = GetOwner();
                    if(!target)
                        return false;
                    basepoints0 = triggerAmount * damage / 100;
                    triggered_spell_id = 58879;
                    break;
                }
                // Shaman T8 Elemental 4P Bonus
                case 64928:
                {
                    basepoints0 = int32( triggerAmount * damage / 100 );
                    triggered_spell_id = 64930;            // Electrified
                    break;
                }
                // Shaman T9 Elemental 4P Bonus
                case 67228:
                {
                    basepoints0 = int32( triggerAmount * damage / 100 );
                    triggered_spell_id = 71824;
                    break;
                }
            }
            // Storm, Earth and Fire
            if (dummySpell->SpellIconID == 3063)
            {
                // Earthbind Totem summon only
                if(procSpell->Id != 2484)
                    return false;

                if (!roll_chance_i(triggerAmount))
                    return false;

                triggered_spell_id = 64695;
                break;
            }
            // Ancestral Awakening
            if (dummySpell->SpellIconID == 3065)
            {
                triggered_spell_id = 52759;
                basepoints0 = triggerAmount * damage / 100;
                target = this;
                break;
            }
            // Earth Shield
            if (dummySpell->SpellFamilyFlags & UI64LIT(0x0000040000000000))
            {
                target = this;
                basepoints0 = triggerAmount;

                // Glyph of Earth Shield
                if (Aura* aur = GetDummyAura(63279))
                {
                    int32 aur_mod = aur->GetModifier()->m_amount;
                    basepoints0 = int32(basepoints0 * (aur_mod + 100.0f) / 100.0f);
                }

                triggered_spell_id = 379;
                break;
            }
            // Improved Water Shield
            if (dummySpell->SpellIconID == 2287)
            {
                // Lesser Healing Wave need aditional 60% roll
                if ((procSpell->SpellFamilyFlags & UI64LIT(0x0000000000000080)) && !roll_chance_i(60))
                    return false;
                // lookup water shield
                AuraList const& vs = GetAurasByType(SPELL_AURA_PROC_TRIGGER_SPELL);
                for(AuraList::const_iterator itr = vs.begin(); itr != vs.end(); ++itr)
                {
                    if ((*itr)->GetSpellProto()->SpellFamilyName == SPELLFAMILY_SHAMAN &&
                        ((*itr)->GetSpellProto()->SpellFamilyFlags & UI64LIT(0x0000002000000000)))
                    {
                        uint32 spell = (*itr)->GetSpellProto()->EffectTriggerSpell[(*itr)->GetEffIndex()];
                        CastSpell(this, spell, true, castItem, triggeredByAura);
                        if ((*itr)->DropAuraCharge())
                            RemoveSingleSpellAurasFromStack((*itr)->GetId());
                        return true;
                    }
                }
                return false;
                break;
            }
            // Lightning Overload
            if (dummySpell->SpellIconID == 2018)            // only this spell have SpellFamily Shaman SpellIconID == 2018 and dummy aura
            {
                if(!procSpell || GetTypeId() != TYPEID_PLAYER || !pVictim )
                    return false;

                // custom cooldown processing case
                if( cooldown && GetTypeId()==TYPEID_PLAYER && ((Player*)this)->HasSpellCooldown(dummySpell->Id))
                    return false;

                uint32 spellId = 0;
                // Every Lightning Bolt and Chain Lightning spell have duplicate vs half damage and zero cost
                switch (procSpell->Id)
                {
                    // Lightning Bolt
                    case   403: spellId = 45284; break;     // Rank  1
                    case   529: spellId = 45286; break;     // Rank  2
                    case   548: spellId = 45287; break;     // Rank  3
                    case   915: spellId = 45288; break;     // Rank  4
                    case   943: spellId = 45289; break;     // Rank  5
                    case  6041: spellId = 45290; break;     // Rank  6
                    case 10391: spellId = 45291; break;     // Rank  7
                    case 10392: spellId = 45292; break;     // Rank  8
                    case 15207: spellId = 45293; break;     // Rank  9
                    case 15208: spellId = 45294; break;     // Rank 10
                    case 25448: spellId = 45295; break;     // Rank 11
                    case 25449: spellId = 45296; break;     // Rank 12
                    case 49237: spellId = 49239; break;     // Rank 13
                    case 49238: spellId = 49240; break;     // Rank 14
                    // Chain Lightning
                    case   421: spellId = 45297; break;     // Rank  1
                    case   930: spellId = 45298; break;     // Rank  2
                    case  2860: spellId = 45299; break;     // Rank  3
                    case 10605: spellId = 45300; break;     // Rank  4
                    case 25439: spellId = 45301; break;     // Rank  5
                    case 25442: spellId = 45302; break;     // Rank  6
                    case 49270: spellId = 49268; break;     // Rank  7
                    case 49271: spellId = 49269; break;     // Rank  8
                    default:
                        sLog.outError("Unit::HandleDummyAuraProc: non handled spell id: %u (LO)", procSpell->Id);
                        return false;
                }
                // No thread generated mod
                // TODO: exist special flag in spell attributes for this, need found and use!
                SpellModifier *mod = new SpellModifier(SPELLMOD_THREAT,SPELLMOD_PCT,-100,triggeredByAura);

                ((Player*)this)->AddSpellMod(mod, true);

                // Remove cooldown (Chain Lightning - have Category Recovery time)
                if (procSpell->SpellFamilyFlags & UI64LIT(0x0000000000000002))
                    ((Player*)this)->RemoveSpellCooldown(spellId);

                CastSpell(pVictim, spellId, true, castItem, triggeredByAura);

                ((Player*)this)->AddSpellMod(mod, false);

                if( cooldown && GetTypeId()==TYPEID_PLAYER )
                    ((Player*)this)->AddSpellCooldown(dummySpell->Id,0,time(NULL) + cooldown);

                return true;
            }
            // Static Shock
            if(dummySpell->SpellIconID == 3059)
            {
                // lookup Lightning Shield
                AuraList const& vs = GetAurasByType(SPELL_AURA_PROC_TRIGGER_SPELL);
                for(AuraList::const_iterator itr = vs.begin(); itr != vs.end(); ++itr)
                {
                    if ((*itr)->GetSpellProto()->SpellFamilyName == SPELLFAMILY_SHAMAN &&
                        ((*itr)->GetSpellProto()->SpellFamilyFlags & UI64LIT(0x0000000000000400)))
                    {
                        uint32 spell = 0;
                        switch ((*itr)->GetId())
                        {
                            case   324: spell = 26364; break;
                            case   325: spell = 26365; break;
                            case   905: spell = 26366; break;
                            case   945: spell = 26367; break;
                            case  8134: spell = 26369; break;
                            case 10431: spell = 26370; break;
                            case 10432: spell = 26363; break;
                            case 25469: spell = 26371; break;
                            case 25472: spell = 26372; break;
                            case 49280: spell = 49278; break;
                            case 49281: spell = 49279; break;
                            default:
                                return false;
                        }
                        CastSpell(target, spell, true, castItem, triggeredByAura);
                        if ((*itr)->DropAuraCharge())
                            RemoveSingleSpellAurasFromStack((*itr)->GetId());
                        return true;
                    }
                }
                return false;
            }
            // Frozen Power
            if (dummySpell->SpellIconID == 3780)
            {
                Unit *caster = triggeredByAura->GetCaster();

                if (!procSpell || !caster)
                    return false;

                float distance = caster->GetDistance(pVictim);
                int32 chance = triggerAmount;

                if (distance < 15.0f || !roll_chance_i(chance))
                    return false;

                // make triggered cast apply after current damage spell processing for prevent remove by it
                if(Spell* spell = GetCurrentSpell(CURRENT_GENERIC_SPELL))
                    spell->AddTriggeredSpell(63685);
                return true;
            }
            break;
        }
        case SPELLFAMILY_DEATHKNIGHT:
        {
            // Blood Aura
            if (dummySpell->SpellIconID == 2636)
            {
                if (GetTypeId() != TYPEID_PLAYER || !((Player*)this)->isHonorOrXPTarget(pVictim))
                    return false;
                basepoints0 = triggerAmount * damage / 100;
                triggered_spell_id = 53168;
                break;
            }
            // Butchery
            if (dummySpell->SpellIconID == 2664)
            {
                basepoints0 = triggerAmount;
                triggered_spell_id = 50163;
                target = this;
                break;
            }
            // Dancing Rune Weapon
            if (dummySpell->Id == 49028)
            {
                // 1 dummy aura for dismiss rune blade
                if (effIndex != EFFECT_INDEX_2)
                    return false;
                // TODO: wite script for this "fights on its own, doing the same attacks"
                // NOTE: Trigger here on every attack and spell cast
                return false;
            }
            // Mark of Blood
            if (dummySpell->Id == 49005)
            {
                // TODO: need more info (cooldowns/PPM)
                triggered_spell_id = 61607;
                break;
            }
            // Vendetta
            if (dummySpell->SpellFamilyFlags & UI64LIT(0x0000000000010000))
            {
                basepoints0 = triggerAmount * GetMaxHealth() / 100;
                triggered_spell_id = 50181;
                target = this;
                break;
            }
            // Necrosis
            if (dummySpell->SpellIconID == 2709)
            {
                basepoints0 = triggerAmount * damage / 100;
                triggered_spell_id = 51460;
                break;
            }
            // Threat of Thassarian
            if (dummySpell->SpellIconID == 2023)
            {
                // Must Dual Wield
                if (!procSpell || !haveOffhandWeapon())
                    return false;
                // Chance as basepoints for dummy aura
                if (!roll_chance_i(triggerAmount))
                    return false;

                switch (procSpell->Id)
                {
                    // Obliterate
                    case 49020:                             // Rank 1
                        triggered_spell_id = 66198; break;
                    case 51423:                             // Rank 2
                        triggered_spell_id = 66972; break;
                    case 51424:                             // Rank 3
                        triggered_spell_id = 66973; break;
                    case 51425:                             // Rank 4
                        triggered_spell_id = 66974; break;
                    // Frost Strike
                    case 49143:                             // Rank 1
                        triggered_spell_id = 66196; break;
                    case 51416:                             // Rank 2
                        triggered_spell_id = 66958; break;
                    case 51417:                             // Rank 3
                        triggered_spell_id = 66959; break;
                    case 51418:                             // Rank 4
                        triggered_spell_id = 66960; break;
                    case 51419:                             // Rank 5
                        triggered_spell_id = 66961; break;
                    case 55268:                             // Rank 6
                        triggered_spell_id = 66962; break;
                    // Plague Strike
                    case 45462:                             // Rank 1
                        triggered_spell_id = 66216; break;
                    case 49917:                             // Rank 2
                        triggered_spell_id = 66988; break;
                    case 49918:                             // Rank 3
                        triggered_spell_id = 66989; break;
                    case 49919:                             // Rank 4
                        triggered_spell_id = 66990; break;
                    case 49920:                             // Rank 5
                        triggered_spell_id = 66991; break;
                    case 49921:                             // Rank 6
                        triggered_spell_id = 66992; break;
                    // Death Strike
                    case 49998:                             // Rank 1
                        triggered_spell_id = 66188; break;
                    case 49999:                             // Rank 2
                        triggered_spell_id = 66950; break;
                    case 45463:                             // Rank 3
                        triggered_spell_id = 66951; break;
                    case 49923:                             // Rank 4
                        triggered_spell_id = 66952; break;
                    case 49924:                             // Rank 5
                        triggered_spell_id = 66953; break;
                    // Rune Strike
                    case 56815:
                        triggered_spell_id = 66217; break;
                    // Blood Strike
                    case 45902:                             // Rank 1
                        triggered_spell_id = 66215; break;
                    case 49926:                             // Rank 2
                        triggered_spell_id = 66975; break;
                    case 49927:                             // Rank 3
                        triggered_spell_id = 66976; break;
                    case 49928:                             // Rank 4
                        triggered_spell_id = 66977; break;
                    case 49929:                             // Rank 5
                        triggered_spell_id = 66978; break;
                    case 49930:                             // Rank 6
                        triggered_spell_id = 66979; break;
                    default:
                        return false;
                }
                break;
            }
            // Runic Power Back on Snare/Root
            if (dummySpell->Id == 61257)
            {
                // only for spells and hit/crit (trigger start always) and not start from self casted spells
                if (procSpell == 0 || !(procEx & (PROC_EX_NORMAL_HIT|PROC_EX_CRITICAL_HIT)) || this == pVictim)
                    return false;
                // Need snare or root mechanic
                if (!(GetAllSpellMechanicMask(procSpell) & IMMUNE_TO_ROOT_AND_SNARE_MASK))
                    return false;
                triggered_spell_id = 61258;
                target = this;
                break;
            }
            // Wandering Plague
            if (dummySpell->SpellIconID == 1614)
            {
                if (!roll_chance_f(GetUnitCriticalChance(BASE_ATTACK, pVictim)))
                    return false;
                basepoints0 = triggerAmount * damage / 100;
                triggered_spell_id = 50526;
                break;
            }
            // Blood-Caked Blade
            if (dummySpell->SpellIconID == 138)
            {
                triggered_spell_id = dummySpell->EffectTriggerSpell[effIndex];
                break;
            }
            break;
        }
        default:
            break;
    }

    // processed charge only counting case
    if(!triggered_spell_id)
        return true;

    SpellEntry const* triggerEntry = sSpellStore.LookupEntry(triggered_spell_id);

    if(!triggerEntry)
    {
        sLog.outError("Unit::HandleDummyAuraProc: Spell %u have not existed triggered spell %u",dummySpell->Id,triggered_spell_id);
        return false;
    }

    // default case
    if(!target || target!=this && !target->isAlive())
        return false;

    if( cooldown && GetTypeId()==TYPEID_PLAYER && ((Player*)this)->HasSpellCooldown(triggered_spell_id))
        return false;

    if(basepoints0)
        CastCustomSpell(target,triggered_spell_id,&basepoints0,NULL,NULL,true,castItem,triggeredByAura);
    else
        CastSpell(target,triggered_spell_id,true,castItem,triggeredByAura);

    if( cooldown && GetTypeId()==TYPEID_PLAYER )
        ((Player*)this)->AddSpellCooldown(triggered_spell_id,0,time(NULL) + cooldown);

    return true;
}

bool Unit::HandleProcTriggerSpell(Unit *pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const *procSpell, uint32 procFlags, uint32 procEx, uint32 cooldown)
{
    // Get triggered aura spell info
    SpellEntry const* auraSpellInfo = triggeredByAura->GetSpellProto();

    // Basepoints of trigger aura
    int32 triggerAmount = triggeredByAura->GetModifier()->m_amount;

    // Set trigger spell id, target, custom basepoints
    uint32 trigger_spell_id = auraSpellInfo->EffectTriggerSpell[triggeredByAura->GetEffIndex()];
    Unit*  target = NULL;
    int32  basepoints[3] = {0, 0, 0};

    if(triggeredByAura->GetModifier()->m_auraname == SPELL_AURA_PROC_TRIGGER_SPELL_WITH_VALUE)
        basepoints[0] = triggerAmount;

    Item* castItem = triggeredByAura->GetCastItemGUID() && GetTypeId()==TYPEID_PLAYER
        ? ((Player*)this)->GetItemByGuid(triggeredByAura->GetCastItemGUID()) : NULL;

    // Try handle unknown trigger spells
    // Custom requirements (not listed in procEx) Warning! damage dealing after this
    // Custom triggered spells
    switch (auraSpellInfo->SpellFamilyName)
    {
        case SPELLFAMILY_GENERIC:
            switch(auraSpellInfo->Id)
            {
                //case 191:                               // Elemental Response
                //    switch (procSpell->School)
                //    {
                //        case SPELL_SCHOOL_FIRE:  trigger_spell_id = 34192; break;
                //        case SPELL_SCHOOL_FROST: trigger_spell_id = 34193; break;
                //        case SPELL_SCHOOL_ARCANE:trigger_spell_id = 34194; break;
                //        case SPELL_SCHOOL_NATURE:trigger_spell_id = 34195; break;
                //        case SPELL_SCHOOL_SHADOW:trigger_spell_id = 34196; break;
                //        case SPELL_SCHOOL_HOLY:  trigger_spell_id = 34197; break;
                //        case SPELL_SCHOOL_NORMAL:trigger_spell_id = 34198; break;
                //    }
                //    break;
                //case 5301:  break;                        // Defensive State (DND)
                //case 7137:  break:                        // Shadow Charge (Rank 1)
                //case 7377:  break:                        // Take Immune Periodic Damage <Not Working>
                //case 13358: break;                        // Defensive State (DND)
                //case 16092: break;                        // Defensive State (DND)
                //case 18943: break;                        // Double Attack
                //case 19194: break;                        // Double Attack
                //case 19817: break;                        // Double Attack
                //case 19818: break;                        // Double Attack
                //case 22835: break;                        // Drunken Rage
                //    trigger_spell_id = 14822; break;
                case 23780:                                 // Aegis of Preservation (Aegis of Preservation trinket)
                    trigger_spell_id = 23781;
                    break;
                //case 24949: break;                        // Defensive State 2 (DND)
                case 27522:                                 // Mana Drain Trigger
                case 40336:                                 // Mana Drain Trigger
                    // On successful melee or ranged attack gain $29471s1 mana and if possible drain $27526s1 mana from the target.
                    if (isAlive())
                        CastSpell(this, 29471, true, castItem, triggeredByAura);
                    if (pVictim && pVictim->isAlive())
                        CastSpell(pVictim, 27526, true, castItem, triggeredByAura);
                    return true;
                case 31255:                                 // Deadly Swiftness (Rank 1)
                    // whenever you deal damage to a target who is below 20% health.
                    if (pVictim->GetHealth() > pVictim->GetMaxHealth() / 5)
                        return false;

                    target = this;
                    trigger_spell_id = 22588;
                    break;
                //case 33207: break;                        // Gossip NPC Periodic - Fidget
                case 33896:                                 // Desperate Defense (Stonescythe Whelp, Stonescythe Alpha, Stonescythe Ambusher)
                    trigger_spell_id = 33898;
                    break;
                //case 34082: break;                        // Advantaged State (DND)
                //case 34783: break:                        // Spell Reflection
                //case 35205: break:                        // Vanish
                //case 35321: break;                        // Gushing Wound
                //case 36096: break:                        // Spell Reflection
                //case 36207: break:                        // Steal Weapon
                //case 36576: break:                        // Shaleskin (Shaleskin Flayer, Shaleskin Ripper) 30023 trigger
                //case 37030: break;                        // Chaotic Temperament
                //case 38363: break;                        // Gushing Wound
                //case 39215: break;                        // Gushing Wound
                //case 40250: break;                        // Improved Duration
                //case 40329: break;                        // Demo Shout Sensor
                //case 40364: break;                        // Entangling Roots Sensor
                //case 41054: break;                        // Copy Weapon
                //    trigger_spell_id = 41055; break;
                //case 41248: break;                        // Consuming Strikes
                //    trigger_spell_id = 41249; break;
                //case 42730: break:                        // Woe Strike
                //case 43453: break:                        // Rune Ward
                //case 43504: break;                        // Alterac Valley OnKill Proc Aura
                //case 44326: break:                        // Pure Energy Passive
                //case 44526: break;                        // Hate Monster (Spar) (30 sec)
                //case 44527: break;                        // Hate Monster (Spar Buddy) (30 sec)
                //case 44819: break;                        // Hate Monster (Spar Buddy) (>30% Health)
                //case 44820: break;                        // Hate Monster (Spar) (<30%)
                case 45057:                                 // Evasive Maneuvers (Commendation of Kael`thas trinket)
                    // reduce you below $s1% health
                    if (GetHealth() - damage > GetMaxHealth() * triggerAmount / 100)
                        return false;
                    break;
                //case 45903: break:                        // Offensive State
                //case 46146: break:                        // [PH] Ahune  Spanky Hands
                //case 46939: break;                        // Black Bow of the Betrayer
                //    trigger_spell_id = 29471; - gain mana
                //                       27526; - drain mana if possible
                case 43820:                                 // Charm of the Witch Doctor (Amani Charm of the Witch Doctor trinket)
                    // Pct value stored in dummy
                    basepoints[0] = pVictim->GetCreateHealth() * auraSpellInfo->EffectBasePoints[EFFECT_INDEX_1] / 100;
                    target = pVictim;
                    break;
                //case 45205: break;                        // Copy Offhand Weapon
                //case 45343: break;                        // Dark Flame Aura
                //case 47300: break;                        // Dark Flame Aura
                //case 48876: break;                        // Beast's Mark
                //    trigger_spell_id = 48877; break;
                //case 49059: break;                        // Horde, Hate Monster (Spar Buddy) (>30% Health)
                //case 50051: break;                        // Ethereal Pet Aura
                //case 50689: break;                        // Blood Presence (Rank 1)
                //case 50844: break;                        // Blood Mirror
                //case 52856: break;                        // Charge
                //case 54072: break;                        // Knockback Ball Passive
                //case 54476: break;                        // Blood Presence
                //case 54775: break;                        // Abandon Vehicle on Poly
                case 57345:                                 // Darkmoon Card: Greatness
                {
                    float stat = 0.0f;
                    // strength
                    if (GetStat(STAT_STRENGTH) > stat) { trigger_spell_id = 60229;stat = GetStat(STAT_STRENGTH); }
                    // agility
                    if (GetStat(STAT_AGILITY)  > stat) { trigger_spell_id = 60233;stat = GetStat(STAT_AGILITY);  }
                    // intellect
                    if (GetStat(STAT_INTELLECT)> stat) { trigger_spell_id = 60234;stat = GetStat(STAT_INTELLECT);}
                    // spirit
                    if (GetStat(STAT_SPIRIT)   > stat) { trigger_spell_id = 60235;                               }
                    break;
                }
                //case 55580: break:                        // Mana Link
                //case 57587: break:                        // Steal Ranged ()
                //case 57594: break;                        // Copy Ranged Weapon
                //case 59237: break;                        // Beast's Mark
                //    trigger_spell_id = 59233; break;
                //case 59288: break;                        // Infra-Green Shield
                //case 59532: break;                        // Abandon Passengers on Poly
                //case 59735: break:                        // Woe Strike
                case 67702:                                 // Death's Choice, Item - Coliseum 25 Normal Melee Trinket
                {
                    float stat = 0.0f;
                    // strength
                    if (GetStat(STAT_STRENGTH) > stat) { trigger_spell_id = 67708;stat = GetStat(STAT_STRENGTH); }
                    // agility
                    if (GetStat(STAT_AGILITY)  > stat) { trigger_spell_id = 67703;                               }
                    break;
                }
                case 67771:                                 // Death's Choice (heroic), Item - Coliseum 25 Heroic Melee Trinket
                {
                    float stat = 0.0f;
                    // strength
                    if (GetStat(STAT_STRENGTH) > stat) { trigger_spell_id = 67773;stat = GetStat(STAT_STRENGTH); }
                    // agility
                    if (GetStat(STAT_AGILITY)  > stat) { trigger_spell_id = 67772;                               }
                    break;
                }
            }
            break;
        case SPELLFAMILY_MAGE:
            if (auraSpellInfo->SpellIconID == 2127)     // Blazing Speed
            {
                switch (auraSpellInfo->Id)
                {
                    case 31641:  // Rank 1
                    case 31642:  // Rank 2
                        trigger_spell_id = 31643;
                        break;
                    default:
                        sLog.outError("Unit::HandleProcTriggerSpell: Spell %u miss posibly Blazing Speed",auraSpellInfo->Id);
                        return false;
                }
            }
            // Persistent Shield (Scarab Brooch trinket)
            else if(auraSpellInfo->Id == 26467)
            {
                // This spell originally trigger 13567 - Dummy Trigger (vs dummy efect)
                basepoints[0] = damage * 15 / 100;
                target = pVictim;
                trigger_spell_id = 26470;
            }
            break;
        case SPELLFAMILY_WARRIOR:
            // Deep Wounds (replace triggered spells to directly apply DoT), dot spell have finilyflags
            if (auraSpellInfo->SpellFamilyFlags == UI64LIT(0x0) && auraSpellInfo->SpellIconID == 243)
            {
                float weaponDamage;
                // DW should benefit of attack power, damage percent mods etc.
                // TODO: check if using offhand damage is correct and if it should be divided by 2
                if (haveOffhandWeapon() && getAttackTimer(BASE_ATTACK) > getAttackTimer(OFF_ATTACK))
                    weaponDamage = (GetFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE) + GetFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE))/2;
                else
                    weaponDamage = (GetFloatValue(UNIT_FIELD_MINDAMAGE) + GetFloatValue(UNIT_FIELD_MAXDAMAGE))/2;

                switch (auraSpellInfo->Id)
                {
                    case 12834: basepoints[0] = int32(weaponDamage * 16 / 100); break;
                    case 12849: basepoints[0] = int32(weaponDamage * 32 / 100); break;
                    case 12867: basepoints[0] = int32(weaponDamage * 48 / 100); break;
                    // Impossible case
                    default:
                        sLog.outError("Unit::HandleProcTriggerSpell: DW unknown spell rank %u",auraSpellInfo->Id);
                        return false;
                }

                // 1 tick/sec * 6 sec = 6 ticks
                basepoints[0] /= 6;

                trigger_spell_id = 12721;
                break;
            }
            if (auraSpellInfo->Id == 50421)             // Scent of Blood
                trigger_spell_id = 50422;
            break;
        case SPELLFAMILY_WARLOCK:
        {
            // Pyroclasm
            if (auraSpellInfo->SpellIconID == 1137)
            {
                if(!pVictim || !pVictim->isAlive() || pVictim == this || procSpell == NULL)
                    return false;
                // Calculate spell tick count for spells
                uint32 tick = 1; // Default tick = 1

                // Hellfire have 15 tick
                if (procSpell->SpellFamilyFlags & UI64LIT(0x0000000000000040))
                    tick = 15;
                // Rain of Fire have 4 tick
                else if (procSpell->SpellFamilyFlags & UI64LIT(0x0000000000000020))
                    tick = 4;
                else
                    return false;

                // Calculate chance = baseChance / tick
                float chance = 0;
                switch (auraSpellInfo->Id)
                {
                    case 18096: chance = 13.0f / tick; break;
                    case 18073: chance = 26.0f / tick; break;
                }
                // Roll chance
                if (!roll_chance_f(chance))
                    return false;

                trigger_spell_id = 18093;
            }
            // Drain Soul
            else if (auraSpellInfo->SpellFamilyFlags & UI64LIT(0x0000000000004000))
            {
                Unit::AuraList const& mAddFlatModifier = GetAurasByType(SPELL_AURA_ADD_FLAT_MODIFIER);
                for(Unit::AuraList::const_iterator i = mAddFlatModifier.begin(); i != mAddFlatModifier.end(); ++i)
                {
                    if ((*i)->GetModifier()->m_miscvalue == SPELLMOD_CHANCE_OF_SUCCESS && (*i)->GetSpellProto()->SpellIconID == 113)
                    {
                        // Drain Soul
                        CastCustomSpell(this, 18371, &basepoints[0], NULL, NULL, true, castItem, triggeredByAura);
                        break;
                    }
                }
                // Not remove charge (aura removed on death in any cases)
                // Need for correct work Drain Soul SPELL_AURA_CHANNEL_DEATH_ITEM aura
                return false;
            }
            // Nether Protection
            else if (auraSpellInfo->SpellIconID == 1985)
            {
                if (!procSpell)
                    return false;
                switch(GetFirstSchoolInMask(GetSpellSchoolMask(procSpell)))
                {
                    case SPELL_SCHOOL_NORMAL:
                        return false;                   // ignore
                    case SPELL_SCHOOL_HOLY:   trigger_spell_id = 54370; break;
                    case SPELL_SCHOOL_FIRE:   trigger_spell_id = 54371; break;
                    case SPELL_SCHOOL_NATURE: trigger_spell_id = 54375; break;
                    case SPELL_SCHOOL_FROST:  trigger_spell_id = 54372; break;
                    case SPELL_SCHOOL_SHADOW: trigger_spell_id = 54374; break;
                    case SPELL_SCHOOL_ARCANE: trigger_spell_id = 54373; break;
                    default:
                        return false;
                }
            }
            // Cheat Death
            else if (auraSpellInfo->Id == 28845)
            {
                // When your health drops below 20% ....
                if (GetHealth() - damage > GetMaxHealth() / 5 || GetHealth() < GetMaxHealth() / 5)
                    return false;
            }
            // Decimation
            else if (auraSpellInfo->Id == 63156 || auraSpellInfo->Id == 63158)
            {
                // Looking for dummy effect
                Aura *aur = GetAura(auraSpellInfo->Id, EFFECT_INDEX_1);
                if (!aur)
                    return false;

                // If target's health is not below equal certain value (35%) not proc
                if (int32(pVictim->GetHealth() * 100 / pVictim->GetMaxHealth()) > aur->GetModifier()->m_amount)
                    return false;
            }
            break;
        }
        case SPELLFAMILY_PRIEST:
        {
            // Greater Heal Refund (Avatar Raiment set)
            if (auraSpellInfo->Id==37594)
            {
                // Not give if target already have full health
                if (pVictim->GetHealth() == pVictim->GetMaxHealth())
                    return false;
                // If your Greater Heal brings the target to full health, you gain $37595s1 mana.
                if (pVictim->GetHealth() + damage < pVictim->GetMaxHealth())
                    return false;
                trigger_spell_id = 37595;
            }
            // Blessed Recovery
            else if (auraSpellInfo->SpellIconID == 1875)
            {
                switch (auraSpellInfo->Id)
                {
                    case 27811: trigger_spell_id = 27813; break;
                    case 27815: trigger_spell_id = 27817; break;
                    case 27816: trigger_spell_id = 27818; break;
                    default:
                        sLog.outError("Unit::HandleProcTriggerSpell: Spell %u not handled in BR", auraSpellInfo->Id);
                    return false;
                }
                basepoints[0] = damage * triggerAmount / 100 / 3;
                target = this;
            }
            break;
        }
        case SPELLFAMILY_DRUID:
        {
            // Druid Forms Trinket
            if (auraSpellInfo->Id==37336)
            {
                switch(m_form)
                {
                    case FORM_NONE:     trigger_spell_id = 37344;break;
                    case FORM_CAT:      trigger_spell_id = 37341;break;
                    case FORM_BEAR:
                    case FORM_DIREBEAR: trigger_spell_id = 37340;break;
                    case FORM_TREE:     trigger_spell_id = 37342;break;
                    case FORM_MOONKIN:  trigger_spell_id = 37343;break;
                    default:
                        return false;
                }
            }
            // Druid T9 Feral Relic (Lacerate, Swipe, Mangle, and Shred)
            else if (auraSpellInfo->Id==67353)
            {
                switch(m_form)
                {
                    case FORM_CAT:      trigger_spell_id = 67355; break;
                    case FORM_BEAR:
                    case FORM_DIREBEAR: trigger_spell_id = 67354; break;
                    default:
                        return false;
                }
            }
            break;
        }
        case SPELLFAMILY_HUNTER:
            // Piercing Shots
            if (auraSpellInfo->SpellIconID == 3247 && auraSpellInfo->SpellVisual[0] == 0)
            {
                basepoints[0] = damage * triggerAmount / 100 / 8;
                trigger_spell_id = 63468;
                target = pVictim;
            }
            // Rapid Recuperation
            else if (auraSpellInfo->Id == 53228 || auraSpellInfo->Id == 53232)
            {
                // This effect only from Rapid Fire (ability cast)
                if (!(procSpell->SpellFamilyFlags & UI64LIT(0x0000000000000020)))
                    return false;
            }
            break;
        case SPELLFAMILY_PALADIN:
        {
            /*
            // Blessed Life
            if (auraSpellInfo->SpellIconID == 2137)
            {
                switch (auraSpellInfo->Id)
                {
                    case 31828:                         // Rank 1
                    case 31829:                         // Rank 2
                    case 31830:                         // Rank 3
                        break;
                    default:
                        sLog.outError("Unit::HandleProcTriggerSpell: Spell %u miss posibly Blessed Life", auraSpellInfo->Id);
                        return false;
                }
            }
            */
            // Healing Discount
            if (auraSpellInfo->Id==37705)
            {
                trigger_spell_id = 37706;
                target = this;
            }
            // Soul Preserver
            if (auraSpellInfo->Id==60510)
            {
                trigger_spell_id = 60515;
                target = this;
            }
            // Illumination
            else if (auraSpellInfo->SpellIconID==241)
            {
                if(!procSpell)
                    return false;
                // procspell is triggered spell but we need mana cost of original casted spell
                uint32 originalSpellId = procSpell->Id;
                // Holy Shock heal
                if (procSpell->SpellFamilyFlags & UI64LIT(0x0001000000000000))
                {
                    switch(procSpell->Id)
                    {
                        case 25914: originalSpellId = 20473; break;
                        case 25913: originalSpellId = 20929; break;
                        case 25903: originalSpellId = 20930; break;
                        case 27175: originalSpellId = 27174; break;
                        case 33074: originalSpellId = 33072; break;
                        case 48820: originalSpellId = 48824; break;
                        case 48821: originalSpellId = 48825; break;
                        default:
                            sLog.outError("Unit::HandleProcTriggerSpell: Spell %u not handled in HShock",procSpell->Id);
                           return false;
                    }
                }
                SpellEntry const *originalSpell = sSpellStore.LookupEntry(originalSpellId);
                if(!originalSpell)
                {
                    sLog.outError("Unit::HandleProcTriggerSpell: Spell %u unknown but selected as original in Illu",originalSpellId);
                    return false;
                }
                // percent stored in effect 1 (class scripts) base points
                int32 cost = originalSpell->manaCost + originalSpell->ManaCostPercentage * GetCreateMana() / 100;
                basepoints[0] = cost*auraSpellInfo->CalculateSimpleValue(EFFECT_INDEX_1)/100;
                trigger_spell_id = 20272;
                target = this;
            }
            // Lightning Capacitor
            else if (auraSpellInfo->Id==37657)
            {
                if(!pVictim || !pVictim->isAlive())
                    return false;
                // stacking
                CastSpell(this, 37658, true, NULL, triggeredByAura);

                Aura * dummy = GetDummyAura(37658);
                // release at 3 aura in stack (cont contain in basepoint of trigger aura)
                if(!dummy || dummy->GetStackAmount() < triggerAmount)
                    return false;

                RemoveAurasDueToSpell(37658);
                trigger_spell_id = 37661;
                target = pVictim;
            }
            // Bonus Healing (Crystal Spire of Karabor mace)
            else if (auraSpellInfo->Id == 40971)
            {
                // If your target is below $s1% health
                if (pVictim->GetHealth() > pVictim->GetMaxHealth() * triggerAmount / 100)
                    return false;
            }
            // Thunder Capacitor
            else if (auraSpellInfo->Id == 54841)
            {
                if(!pVictim || !pVictim->isAlive())
                    return false;
                // stacking
                CastSpell(this, 54842, true, NULL, triggeredByAura);

                // counting
                Aura * dummy = GetDummyAura(54842);
                // release at 3 aura in stack (cont contain in basepoint of trigger aura)
                if(!dummy || dummy->GetStackAmount() < triggerAmount)
                    return false;

                RemoveAurasDueToSpell(54842);
                trigger_spell_id = 54843;
                target = pVictim;
            }
            break;
        }
        case SPELLFAMILY_SHAMAN:
        {
            // Lightning Shield (overwrite non existing triggered spell call in spell.dbc
            if (auraSpellInfo->SpellFamilyFlags & UI64LIT(0x0000000000000400))
            {
                switch(auraSpellInfo->Id)
                {
                    case 324:                           // Rank 1
                        trigger_spell_id = 26364; break;
                    case 325:                           // Rank 2
                        trigger_spell_id = 26365; break;
                    case 905:                           // Rank 3
                        trigger_spell_id = 26366; break;
                    case 945:                           // Rank 4
                        trigger_spell_id = 26367; break;
                    case 8134:                          // Rank 5
                        trigger_spell_id = 26369; break;
                    case 10431:                         // Rank 6
                        trigger_spell_id = 26370; break;
                    case 10432:                         // Rank 7
                        trigger_spell_id = 26363; break;
                    case 25469:                         // Rank 8
                        trigger_spell_id = 26371; break;
                    case 25472:                         // Rank 9
                        trigger_spell_id = 26372; break;
                    case 49280:                         // Rank 10
                        trigger_spell_id = 49278; break;
                    case 49281:                         // Rank 11
                        trigger_spell_id = 49279; break;
                    default:
                        sLog.outError("Unit::HandleProcTriggerSpell: Spell %u not handled in LShield", auraSpellInfo->Id);
                    return false;
                }
            }
            // Lightning Shield (The Ten Storms set)
            else if (auraSpellInfo->Id == 23551)
            {
                trigger_spell_id = 23552;
                target = pVictim;
            }
            // Damage from Lightning Shield (The Ten Storms set)
            else if (auraSpellInfo->Id == 23552)
                trigger_spell_id = 27635;
            // Mana Surge (The Earthfury set)
            else if (auraSpellInfo->Id == 23572)
            {
                if(!procSpell)
                    return false;
                basepoints[0] = procSpell->manaCost * 35 / 100;
                trigger_spell_id = 23571;
                target = this;
            }
            // Nature's Guardian
            else if (auraSpellInfo->SpellIconID == 2013)
            {
                // Check health condition - should drop to less 30% (damage deal after this!)
                if (!(10*(int32(GetHealth() - damage)) < int32(3 * GetMaxHealth())))
                    return false;

                 if(pVictim && pVictim->isAlive())
                     pVictim->getThreatManager().modifyThreatPercent(this,-10);

                basepoints[0] = triggerAmount * GetMaxHealth() / 100;
                trigger_spell_id = 31616;
                target = this;
            }
            break;
        }
        case SPELLFAMILY_DEATHKNIGHT:
        {
            // Acclimation
            if (auraSpellInfo->SpellIconID == 1930)
            {
                if (!procSpell)
                    return false;
                switch(GetFirstSchoolInMask(GetSpellSchoolMask(procSpell)))
                {
                    case SPELL_SCHOOL_NORMAL:
                        return false;                   // ignore
                    case SPELL_SCHOOL_HOLY:   trigger_spell_id = 50490; break;
                    case SPELL_SCHOOL_FIRE:   trigger_spell_id = 50362; break;
                    case SPELL_SCHOOL_NATURE: trigger_spell_id = 50488; break;
                    case SPELL_SCHOOL_FROST:  trigger_spell_id = 50485; break;
                    case SPELL_SCHOOL_SHADOW: trigger_spell_id = 50489; break;
                    case SPELL_SCHOOL_ARCANE: trigger_spell_id = 50486; break;
                    default:
                        return false;
                }
            }
            // Blood Presence
            else if (auraSpellInfo->Id == 48266)
            {
                if (GetTypeId() != TYPEID_PLAYER)
                    return false;
                if (!((Player*)this)->isHonorOrXPTarget(pVictim))
                    return false;
                trigger_spell_id = 50475;
                basepoints[0] = damage * triggerAmount / 100;
            }
            // Blade Barrier
            else if (auraSpellInfo->SpellIconID == 85)
            {
                if (GetTypeId() != TYPEID_PLAYER || getClass() != CLASS_DEATH_KNIGHT ||
                    !((Player*)this)->IsBaseRuneSlotsOnCooldown(RUNE_BLOOD))
                    return false;
            }
            break;
        }
        default:
             break;
    }

    // All ok. Check current trigger spell
    SpellEntry const* triggerEntry = sSpellStore.LookupEntry(trigger_spell_id);
    if (!triggerEntry)
    {
        // Not cast unknown spell
        // sLog.outError("Unit::HandleProcTriggerSpell: Spell %u have 0 in EffectTriggered[%d], not handled custom case?",auraSpellInfo->Id,triggeredByAura->GetEffIndex());
        return false;
    }

    // not allow proc extra attack spell at extra attack
    if (m_extraAttacks && IsSpellHaveEffect(triggerEntry, SPELL_EFFECT_ADD_EXTRA_ATTACKS))
        return false;

    // Custom basepoints/target for exist spell
    // dummy basepoints or other customs
    switch(trigger_spell_id)
    {
        // Cast positive spell on enemy target
        case 7099:  // Curse of Mending
        case 39647: // Curse of Mending
        case 29494: // Temptation
        case 20233: // Improved Lay on Hands (cast on target)
        {
            target = pVictim;
            break;
        }
        // Combo points add triggers (need add combopoint only for main target, and after possible combopoints reset)
        case 15250: // Rogue Setup
        {
            if(!pVictim || pVictim != getVictim())   // applied only for main target
                return false;
            break;                                   // continue normal case
        }
        // Finish movies that add combo
        case 14189: // Seal Fate (Netherblade set)
        case 14157: // Ruthlessness
        {
            // Need add combopoint AFTER finish movie (or they dropped in finish phase)
            break;
        }
        // Bloodthirst (($m/100)% of max health)
        case 23880:
        {
            basepoints[0] = int32(GetMaxHealth() * triggerAmount / 100);
            break;
        }
        // Shamanistic Rage triggered spell
        case 30824:
        {
            basepoints[0] = int32(GetTotalAttackPowerValue(BASE_ATTACK) * triggerAmount / 100);
            break;
        }
        // Enlightenment (trigger only from mana cost spells)
        case 35095:
        {
            if(!procSpell || procSpell->powerType!=POWER_MANA || procSpell->manaCost==0 && procSpell->ManaCostPercentage==0 && procSpell->manaCostPerlevel==0)
                return false;
            break;
        }
        // Demonic Pact
        case 48090:
        {
            // As the spell is proced from pet's attack - find owner
            Unit* owner = GetOwner();
            if (!owner || owner->GetTypeId() != TYPEID_PLAYER)
                return false;

            // This spell doesn't stack, but refreshes duration. So we receive current bonuses to minus them later.
            int32 curBonus = 0;
            if (Aura* aur = owner->GetAura(48090, EFFECT_INDEX_0))
                curBonus = aur->GetModifier()->m_amount;
            int32 spellDamage  = owner->SpellBaseDamageBonus(SPELL_SCHOOL_MASK_MAGIC) - curBonus;
            if(spellDamage <= 0)
                return false;

            // percent stored in owner talent dummy
            AuraList const& dummyAuras = owner->GetAurasByType(SPELL_AURA_DUMMY);
            for (AuraList::const_iterator i = dummyAuras.begin(); i != dummyAuras.end(); ++i)
            {
                if ((*i)->GetSpellProto()->SpellIconID == 3220)
                {
                    basepoints[0] = basepoints[1] = int32(spellDamage * (*i)->GetModifier()->m_amount / 100);
                    break;
                }
            }
            break;
        }
        // Sword and Board
        case 50227:
        {
            // Remove cooldown on Shield Slam
            if (GetTypeId() == TYPEID_PLAYER)
                ((Player*)this)->RemoveSpellCategoryCooldown(1209, true);
            break;
        }
        // Maelstrom Weapon
        case 53817:
        {
            // have rank dependent proc chance, ignore too often cases
            // PPM = 2.5 * (rank of talent),
            uint32 rank = sSpellMgr.GetSpellRank(auraSpellInfo->Id);
            // 5 rank -> 100% 4 rank -> 80% and etc from full rate
            if(!roll_chance_i(20*rank))
                return false;
            break;
        }
        // Brain Freeze
        case 57761:
        {
            if(!procSpell)
                return false;
            // For trigger from Blizzard need exist Improved Blizzard
            if (procSpell->SpellFamilyName==SPELLFAMILY_MAGE && (procSpell->SpellFamilyFlags & UI64LIT(0x0000000000000080)))
            {
                bool found = false;
                AuraList const& mOverrideClassScript = GetAurasByType(SPELL_AURA_OVERRIDE_CLASS_SCRIPTS);
                for(AuraList::const_iterator i = mOverrideClassScript.begin(); i != mOverrideClassScript.end(); ++i)
                {
                    int32 script = (*i)->GetModifier()->m_miscvalue;
                    if(script==836 || script==988 || script==989)
                    {
                        found=true;
                        break;
                    }
                }
                if(!found)
                    return false;
            }
            break;
        }
        // Astral Shift
        case 52179:
        {
            if (procSpell == 0 || !(procEx & (PROC_EX_NORMAL_HIT|PROC_EX_CRITICAL_HIT)) || this == pVictim)
                return false;

            // Need stun, fear or silence mechanic
            if (!(GetAllSpellMechanicMask(procSpell) & IMMUNE_TO_SILENCE_AND_STUN_AND_FEAR_MASK))
                return false;
            break;
        }
        // Burning Determination
        case 54748:
        {
            if(!procSpell)
                return false;
            // Need Interrupt or Silenced mechanic
            if (!(GetAllSpellMechanicMask(procSpell) & IMMUNE_TO_INTERRUPT_AND_SILENCE_MASK))
                return false;
            break;
        }
        // Lock and Load
        case 56453:
        {
            // Proc only from trap activation (from periodic proc another aura of this spell)
            if (!(procFlags & PROC_FLAG_ON_TRAP_ACTIVATION) || !roll_chance_i(triggerAmount))
                return false;
            break;
        }
        // Freezing Fog (Rime triggered)
        case 59052:
        {
            // Howling Blast cooldown reset
            if (GetTypeId() == TYPEID_PLAYER)
                ((Player*)this)->RemoveSpellCategoryCooldown(1248, true);
            break;
        }
        // Druid - Savage Defense
        case 62606:
        {
            basepoints[0] = int32(GetTotalAttackPowerValue(BASE_ATTACK) * triggerAmount / 100);
            break;
        }
    }

    if( cooldown && GetTypeId()==TYPEID_PLAYER && ((Player*)this)->HasSpellCooldown(trigger_spell_id))
        return false;

    // try detect target manually if not set
    if ( target == NULL )
       target = !(procFlags & PROC_FLAG_SUCCESSFUL_POSITIVE_SPELL) && IsPositiveSpell(trigger_spell_id) ? this : pVictim;

    // default case
    if(!target || target!=this && !target->isAlive())
        return false;

    if(basepoints[0] || basepoints[1] || basepoints[2])
        CastCustomSpell(target,trigger_spell_id,
            basepoints[0] ? &basepoints[0] : NULL,
            basepoints[1] ? &basepoints[1] : NULL,
            basepoints[2] ? &basepoints[2] : NULL,
            true,castItem,triggeredByAura);
    else
        CastSpell(target,trigger_spell_id,true,castItem,triggeredByAura);

    if( cooldown && GetTypeId()==TYPEID_PLAYER )
        ((Player*)this)->AddSpellCooldown(trigger_spell_id,0,time(NULL) + cooldown);

    return true;
}

bool Unit::HandleOverrideClassScriptAuraProc(Unit *pVictim, uint32 /*damage*/, Aura *triggeredByAura, SpellEntry const *procSpell, uint32 cooldown)
{
    int32 scriptId = triggeredByAura->GetModifier()->m_miscvalue;

    if(!pVictim || !pVictim->isAlive())
        return false;

    Item* castItem = triggeredByAura->GetCastItemGUID() && GetTypeId()==TYPEID_PLAYER
        ? ((Player*)this)->GetItemByGuid(triggeredByAura->GetCastItemGUID()) : NULL;

    // Basepoints of trigger aura
    int32 triggerAmount = triggeredByAura->GetModifier()->m_amount;

    uint32 triggered_spell_id = 0;

    switch(scriptId)
    {
        case 836:                                           // Improved Blizzard (Rank 1)
        {
            if (!procSpell || procSpell->SpellVisual[0]!=9487)
                return false;
            triggered_spell_id = 12484;
            break;
        }
        case 988:                                           // Improved Blizzard (Rank 2)
        {
            if (!procSpell || procSpell->SpellVisual[0]!=9487)
                return false;
            triggered_spell_id = 12485;
            break;
        }
        case 989:                                           // Improved Blizzard (Rank 3)
        {
            if (!procSpell || procSpell->SpellVisual[0]!=9487)
                return false;
            triggered_spell_id = 12486;
            break;
        }
        case 4086:                                          // Improved Mend Pet (Rank 1)
        case 4087:                                          // Improved Mend Pet (Rank 2)
        {
            if(!roll_chance_i(triggerAmount))
                return false;

            triggered_spell_id = 24406;
            break;
        }
        case 4533:                                          // Dreamwalker Raiment 2 pieces bonus
        {
            // Chance 50%
            if (!roll_chance_i(50))
                return false;

            switch (pVictim->getPowerType())
            {
                case POWER_MANA:   triggered_spell_id = 28722; break;
                case POWER_RAGE:   triggered_spell_id = 28723; break;
                case POWER_ENERGY: triggered_spell_id = 28724; break;
                default:
                    return false;
            }
            break;
        }
        case 4537:                                          // Dreamwalker Raiment 6 pieces bonus
            triggered_spell_id = 28750;                     // Blessing of the Claw
            break;
        case 5497:                                          // Improved Mana Gems (Serpent-Coil Braid)
            triggered_spell_id = 37445;                     // Mana Surge
            break;
        case 6953:                                          // Warbringer
            RemoveAurasAtMechanicImmunity(IMMUNE_TO_ROOT_AND_SNARE_MASK,0,true);
            return true;
        case 7010:                                          // Revitalize (rank 1)
        case 7011:                                          // Revitalize (rank 2)
        case 7012:                                          // Revitalize (rank 3)
        {
            if(!roll_chance_i(triggerAmount))
                return false;

            switch( pVictim->getPowerType() )
            {
                case POWER_MANA:        triggered_spell_id = 48542; break;
                case POWER_RAGE:        triggered_spell_id = 48541; break;
                case POWER_ENERGY:      triggered_spell_id = 48540; break;
                case POWER_RUNIC_POWER: triggered_spell_id = 48543; break;
                default: return false;
            }
            break;
        }
    }

    // not processed
    if(!triggered_spell_id)
        return false;

    // standard non-dummy case
    SpellEntry const* triggerEntry = sSpellStore.LookupEntry(triggered_spell_id);

    if(!triggerEntry)
    {
        sLog.outError("Unit::HandleOverrideClassScriptAuraProc: Spell %u triggering for class script id %u",triggered_spell_id,scriptId);
        return false;
    }

    if( cooldown && GetTypeId()==TYPEID_PLAYER && ((Player*)this)->HasSpellCooldown(triggered_spell_id))
        return false;

    CastSpell(pVictim, triggered_spell_id, true, castItem, triggeredByAura);

    if( cooldown && GetTypeId()==TYPEID_PLAYER )
        ((Player*)this)->AddSpellCooldown(triggered_spell_id,0,time(NULL) + cooldown);

    return true;
}

void Unit::setPowerType(Powers new_powertype)
{
    SetByteValue(UNIT_FIELD_BYTES_0, 3, new_powertype);

    if(GetTypeId() == TYPEID_PLAYER)
    {
        if(((Player*)this)->GetGroup())
            ((Player*)this)->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_POWER_TYPE);
    }
    else if(((Creature*)this)->isPet())
    {
        Pet *pet = ((Pet*)this);
        if(pet->isControlled())
        {
            Unit *owner = GetOwner();
            if(owner && (owner->GetTypeId() == TYPEID_PLAYER) && ((Player*)owner)->GetGroup())
                ((Player*)owner)->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_POWER_TYPE);
        }
    }

    switch(new_powertype)
    {
        default:
        case POWER_MANA:
            break;
        case POWER_RAGE:
            SetMaxPower(POWER_RAGE,GetCreatePowers(POWER_RAGE));
            SetPower(   POWER_RAGE,0);
            break;
        case POWER_FOCUS:
            SetMaxPower(POWER_FOCUS,GetCreatePowers(POWER_FOCUS));
            SetPower(   POWER_FOCUS,GetCreatePowers(POWER_FOCUS));
            break;
        case POWER_ENERGY:
            SetMaxPower(POWER_ENERGY,GetCreatePowers(POWER_ENERGY));
            break;
        case POWER_HAPPINESS:
            SetMaxPower(POWER_HAPPINESS,GetCreatePowers(POWER_HAPPINESS));
            SetPower(POWER_HAPPINESS,GetCreatePowers(POWER_HAPPINESS));
            break;
    }
}

FactionTemplateEntry const* Unit::getFactionTemplateEntry() const
{
    FactionTemplateEntry const* entry = sFactionTemplateStore.LookupEntry(getFaction());
    if(!entry)
    {
        static uint64 guid = 0;                             // prevent repeating spam same faction problem

        if(GetGUID() != guid)
        {
            if(GetTypeId() == TYPEID_PLAYER)
                sLog.outError("Player %s have invalid faction (faction template id) #%u", ((Player*)this)->GetName(), getFaction());
            else
                sLog.outError("Creature (template id: %u) have invalid faction (faction template id) #%u", ((Creature*)this)->GetCreatureInfo()->Entry, getFaction());
            guid = GetGUID();
        }
    }
    return entry;
}

bool Unit::IsHostileTo(Unit const* unit) const
{
    // always non-hostile to self
    if(unit==this)
        return false;

    // always non-hostile to GM in GM mode
    if(unit->GetTypeId()==TYPEID_PLAYER && ((Player const*)unit)->isGameMaster())
        return false;

    // always hostile to enemy
    if(getVictim()==unit || unit->getVictim()==this)
        return true;

    // test pet/charm masters instead pers/charmeds
    Unit const* testerOwner = GetCharmerOrOwner();
    Unit const* targetOwner = unit->GetCharmerOrOwner();

    // always hostile to owner's enemy
    if(testerOwner && (testerOwner->getVictim()==unit || unit->getVictim()==testerOwner))
        return true;

    // always hostile to enemy owner
    if(targetOwner && (getVictim()==targetOwner || targetOwner->getVictim()==this))
        return true;

    // always hostile to owner of owner's enemy
    if(testerOwner && targetOwner && (testerOwner->getVictim()==targetOwner || targetOwner->getVictim()==testerOwner))
        return true;

    Unit const* tester = testerOwner ? testerOwner : this;
    Unit const* target = targetOwner ? targetOwner : unit;

    // always non-hostile to target with common owner, or to owner/pet
    if(tester==target)
        return false;

    // special cases (Duel, etc)
    if(tester->GetTypeId()==TYPEID_PLAYER && target->GetTypeId()==TYPEID_PLAYER)
    {
        Player const* pTester = (Player const*)tester;
        Player const* pTarget = (Player const*)target;

        // Duel
        if(pTester->duel && pTester->duel->opponent == pTarget && pTester->duel->startTime != 0)
            return true;

        // Group
        if(pTester->GetGroup() && pTester->GetGroup()==pTarget->GetGroup())
            return false;

        // Sanctuary
        if(pTarget->HasByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_SANCTUARY) && pTester->HasByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_SANCTUARY))
            return false;

        // PvP FFA state
        if(pTester->IsFFAPvP() && pTarget->IsFFAPvP())
            return true;

        //= PvP states
        // Green/Blue (can't attack)
        if(pTester->GetTeam()==pTarget->GetTeam())
            return false;

        // Red (can attack) if true, Blue/Yellow (can't attack) in another case
        return pTester->IsPvP() && pTarget->IsPvP();
    }

    // faction base cases
    FactionTemplateEntry const*tester_faction = tester->getFactionTemplateEntry();
    FactionTemplateEntry const*target_faction = target->getFactionTemplateEntry();
    if(!tester_faction || !target_faction)
        return false;

    if(target->isAttackingPlayer() && tester->IsContestedGuard())
        return true;

    // PvC forced reaction and reputation case
    if(tester->GetTypeId()==TYPEID_PLAYER)
    {
        // forced reaction
        if(target_faction->faction)
        {
            if(ReputationRank const* force =((Player*)tester)->GetReputationMgr().GetForcedRankIfAny(target_faction))
                return *force <= REP_HOSTILE;

            // if faction have reputation then hostile state for tester at 100% dependent from at_war state
            if(FactionEntry const* raw_target_faction = sFactionStore.LookupEntry(target_faction->faction))
                if(FactionState const* factionState = ((Player*)tester)->GetReputationMgr().GetState(raw_target_faction))
                    return (factionState->Flags & FACTION_FLAG_AT_WAR);
        }
    }
    // CvP forced reaction and reputation case
    else if(target->GetTypeId()==TYPEID_PLAYER)
    {
        // forced reaction
        if(tester_faction->faction)
        {
            if(ReputationRank const* force = ((Player*)target)->GetReputationMgr().GetForcedRankIfAny(tester_faction))
                return *force <= REP_HOSTILE;

            // apply reputation state
            FactionEntry const* raw_tester_faction = sFactionStore.LookupEntry(tester_faction->faction);
            if(raw_tester_faction && raw_tester_faction->reputationListID >=0 )
                return ((Player const*)target)->GetReputationMgr().GetRank(raw_tester_faction) <= REP_HOSTILE;
        }
    }

    // common faction based case (CvC,PvC,CvP)
    return tester_faction->IsHostileTo(*target_faction);
}

bool Unit::IsFriendlyTo(Unit const* unit) const
{
    // always friendly to self
    if(unit==this)
        return true;

    // always friendly to GM in GM mode
    if(unit->GetTypeId()==TYPEID_PLAYER && ((Player const*)unit)->isGameMaster())
        return true;

    // always non-friendly to enemy
    if(getVictim()==unit || unit->getVictim()==this)
        return false;

    // test pet/charm masters instead pers/charmeds
    Unit const* testerOwner = GetCharmerOrOwner();
    Unit const* targetOwner = unit->GetCharmerOrOwner();

    // always non-friendly to owner's enemy
    if(testerOwner && (testerOwner->getVictim()==unit || unit->getVictim()==testerOwner))
        return false;

    // always non-friendly to enemy owner
    if(targetOwner && (getVictim()==targetOwner || targetOwner->getVictim()==this))
        return false;

    // always non-friendly to owner of owner's enemy
    if(testerOwner && targetOwner && (testerOwner->getVictim()==targetOwner || targetOwner->getVictim()==testerOwner))
        return false;

    Unit const* tester = testerOwner ? testerOwner : this;
    Unit const* target = targetOwner ? targetOwner : unit;

    // always friendly to target with common owner, or to owner/pet
    if(tester==target)
        return true;

    // special cases (Duel)
    if(tester->GetTypeId()==TYPEID_PLAYER && target->GetTypeId()==TYPEID_PLAYER)
    {
        Player const* pTester = (Player const*)tester;
        Player const* pTarget = (Player const*)target;

        // Duel
        if(pTester->duel && pTester->duel->opponent == target && pTester->duel->startTime != 0)
            return false;

        // Group
        if(pTester->GetGroup() && pTester->GetGroup()==pTarget->GetGroup())
            return true;

        // Sanctuary
        if(pTarget->HasByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_SANCTUARY) && pTester->HasByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_SANCTUARY))
            return true;

        // PvP FFA state
        if(pTester->IsFFAPvP() && pTarget->IsFFAPvP())
            return false;

        //= PvP states
        // Green/Blue (non-attackable)
        if(pTester->GetTeam()==pTarget->GetTeam())
            return true;

        // Blue (friendly/non-attackable) if not PVP, or Yellow/Red in another case (attackable)
        return !pTarget->IsPvP();
    }

    // faction base cases
    FactionTemplateEntry const*tester_faction = tester->getFactionTemplateEntry();
    FactionTemplateEntry const*target_faction = target->getFactionTemplateEntry();
    if(!tester_faction || !target_faction)
        return false;

    if(target->isAttackingPlayer() && tester->IsContestedGuard())
        return false;

    // PvC forced reaction and reputation case
    if(tester->GetTypeId()==TYPEID_PLAYER)
    {
        // forced reaction
        if(target_faction->faction)
        {
            if(ReputationRank const* force =((Player*)tester)->GetReputationMgr().GetForcedRankIfAny(target_faction))
                return *force >= REP_FRIENDLY;

            // if faction have reputation then friendly state for tester at 100% dependent from at_war state
            if(FactionEntry const* raw_target_faction = sFactionStore.LookupEntry(target_faction->faction))
                if(FactionState const* factionState = ((Player*)tester)->GetReputationMgr().GetState(raw_target_faction))
                    return !(factionState->Flags & FACTION_FLAG_AT_WAR);
        }
    }
    // CvP forced reaction and reputation case
    else if(target->GetTypeId()==TYPEID_PLAYER)
    {
        // forced reaction
        if(tester_faction->faction)
        {
            if(ReputationRank const* force =((Player*)target)->GetReputationMgr().GetForcedRankIfAny(tester_faction))
                return *force >= REP_FRIENDLY;

            // apply reputation state
            if(FactionEntry const* raw_tester_faction = sFactionStore.LookupEntry(tester_faction->faction))
                if(raw_tester_faction->reputationListID >=0 )
                    return ((Player const*)target)->GetReputationMgr().GetRank(raw_tester_faction) >= REP_FRIENDLY;
        }
    }

    // common faction based case (CvC,PvC,CvP)
    return tester_faction->IsFriendlyTo(*target_faction);
}

bool Unit::IsHostileToPlayers() const
{
    FactionTemplateEntry const* my_faction = getFactionTemplateEntry();
    if(!my_faction || !my_faction->faction)
        return false;

    FactionEntry const* raw_faction = sFactionStore.LookupEntry(my_faction->faction);
    if(raw_faction && raw_faction->reputationListID >=0 )
        return false;

    return my_faction->IsHostileToPlayers();
}

bool Unit::IsNeutralToAll() const
{
    FactionTemplateEntry const* my_faction = getFactionTemplateEntry();
    if(!my_faction || !my_faction->faction)
        return true;

    FactionEntry const* raw_faction = sFactionStore.LookupEntry(my_faction->faction);
    if(raw_faction && raw_faction->reputationListID >=0 )
        return false;

    return my_faction->IsNeutralToAll();
}

bool Unit::Attack(Unit *victim, bool meleeAttack)
{
    if(!victim || victim == this)
        return false;

    // dead units can neither attack nor be attacked
    if(!isAlive() || !victim->IsInWorld() || !victim->isAlive())
        return false;

    // player cannot attack in mount state
    if(GetTypeId()==TYPEID_PLAYER && IsMounted())
        return false;

    // nobody can attack GM in GM-mode
    if(victim->GetTypeId()==TYPEID_PLAYER)
    {
        if(((Player*)victim)->isGameMaster())
            return false;
    }
    else
    {
        if(((Creature*)victim)->IsInEvadeMode())
            return false;
    }

    // remove SPELL_AURA_MOD_UNATTACKABLE at attack (in case non-interruptible spells stun aura applied also that not let attack)
    if(HasAuraType(SPELL_AURA_MOD_UNATTACKABLE))
        RemoveSpellsCausingAura(SPELL_AURA_MOD_UNATTACKABLE);

    // in fighting already
    if (m_attacking)
    {
        if (m_attacking == victim)
        {
            // switch to melee attack from ranged/magic
            if( meleeAttack && !hasUnitState(UNIT_STAT_MELEE_ATTACKING) )
            {
                addUnitState(UNIT_STAT_MELEE_ATTACKING);
                SendMeleeAttackStart(victim);
                return true;
            }
            return false;
        }

        // remove old target data
        AttackStop(true);
    }
    // new battle
    else
    {
        // set position before any AI calls/assistance
        if(GetTypeId()==TYPEID_UNIT)
            ((Creature*)this)->SetCombatStartPosition(GetPositionX(), GetPositionY(), GetPositionZ());
    }

    // Set our target
    SetTargetGUID(victim->GetGUID());

    if(meleeAttack)
        addUnitState(UNIT_STAT_MELEE_ATTACKING);

    m_attacking = victim;
    m_attacking->_addAttacker(this);

    if (GetTypeId() == TYPEID_UNIT)
    {
        ((Creature*)this)->SendAIReaction(AI_REACTION_AGGRO);
        ((Creature*)this)->CallAssistance();
    }

    // delay offhand weapon attack to next attack time
    if(haveOffhandWeapon())
        resetAttackTimer(OFF_ATTACK);

    if(meleeAttack)
        SendMeleeAttackStart(victim);

    return true;
}

bool Unit::AttackStop(bool targetSwitch /*=false*/)
{
    if (!m_attacking)
        return false;

    Unit* victim = m_attacking;

    m_attacking->_removeAttacker(this);
    m_attacking = NULL;

    // Clear our target
    SetTargetGUID(0);

    clearUnitState(UNIT_STAT_MELEE_ATTACKING);

    InterruptSpell(CURRENT_MELEE_SPELL);

    // reset only at real combat stop
    if(!targetSwitch && GetTypeId()==TYPEID_UNIT )
    {
        ((Creature*)this)->SetNoCallAssistance(false);

        if (((Creature*)this)->HasSearchedAssistance())
        {
            ((Creature*)this)->SetNoSearchAssistance(false);
            UpdateSpeed(MOVE_RUN, false);
        }
    }

    SendMeleeAttackStop(victim);

    return true;
}

void Unit::CombatStop(bool includingCast)
{
    if (includingCast && IsNonMeleeSpellCasted(false))
        InterruptNonMeleeSpells(false);

    AttackStop();
    RemoveAllAttackers();
    if( GetTypeId()==TYPEID_PLAYER )
        ((Player*)this)->SendAttackSwingCancelAttack();     // melee and ranged forced attack cancel
    ClearInCombat();
}

struct CombatStopWithPetsHelper
{
    explicit CombatStopWithPetsHelper(bool _includingCast) : includingCast(_includingCast) {}
    void operator()(Unit* unit) const { unit->CombatStop(includingCast); }
    bool includingCast;
};

void Unit::CombatStopWithPets(bool includingCast)
{
    CombatStop(includingCast);
    CallForAllControlledUnits(CombatStopWithPetsHelper(includingCast),false,true,true);
}

struct IsAttackingPlayerHelper
{
    explicit IsAttackingPlayerHelper() {}
    bool operator()(Unit* unit) const { return unit->isAttackingPlayer(); }
};

bool Unit::isAttackingPlayer() const
{
    if(hasUnitState(UNIT_STAT_ATTACK_PLAYER))
        return true;

    return CheckAllControlledUnits(IsAttackingPlayerHelper(),true,true,true);
}

void Unit::RemoveAllAttackers()
{
    while (!m_attackers.empty())
    {
        AttackerSet::iterator iter = m_attackers.begin();
        if(!(*iter)->AttackStop())
        {
            sLog.outError("WORLD: Unit has an attacker that isn't attacking it!");
            m_attackers.erase(iter);
        }
    }
}

bool Unit::HasAuraStateForCaster(AuraState flag, uint64 caster) const
{
    if(!HasAuraState(flag))
        return false;

    // single per-caster aura state
    if(flag == AURA_STATE_CONFLAGRATE)
    {
        Unit::AuraList const& dotList = GetAurasByType(SPELL_AURA_PERIODIC_DAMAGE);
        for(Unit::AuraList::const_iterator i = dotList.begin(); i != dotList.end(); ++i)
        {
            if ((*i)->GetSpellProto()->SpellFamilyName == SPELLFAMILY_WARLOCK &&
                (*i)->GetCasterGUID() == caster &&
                //  Immolate
                (((*i)->GetSpellProto()->SpellFamilyFlags & UI64LIT(0x0000000000000004)) ||
                // Shadowflame
                ((*i)->GetSpellProto()->SpellFamilyFlags2 & 0x00000002)))
            {
                return true;
            }
        }

        return false;
    }

    return true;
}

void Unit::ModifyAuraState(AuraState flag, bool apply)
{
    if (apply)
    {
        if (!HasFlag(UNIT_FIELD_AURASTATE, 1<<(flag-1)))
        {
            SetFlag(UNIT_FIELD_AURASTATE, 1<<(flag-1));
            if(GetTypeId() == TYPEID_PLAYER)
            {
                const PlayerSpellMap& sp_list = ((Player*)this)->GetSpellMap();
                for (PlayerSpellMap::const_iterator itr = sp_list.begin(); itr != sp_list.end(); ++itr)
                {
                    if(itr->second.state == PLAYERSPELL_REMOVED) continue;
                    SpellEntry const *spellInfo = sSpellStore.LookupEntry(itr->first);
                    if (!spellInfo || !IsPassiveSpell(itr->first)) continue;
                    if (spellInfo->CasterAuraState == flag)
                        CastSpell(this, itr->first, true, NULL);
                }
            }
        }
    }
    else
    {
        if (HasFlag(UNIT_FIELD_AURASTATE,1<<(flag-1)))
        {
            RemoveFlag(UNIT_FIELD_AURASTATE, 1<<(flag-1));

            if (flag != AURA_STATE_ENRAGE)                  // enrage aura state triggering continues auras
            {
                Unit::AuraMap& tAuras = GetAuras();
                for (Unit::AuraMap::iterator itr = tAuras.begin(); itr != tAuras.end();)
                {
                    SpellEntry const* spellProto = (*itr).second->GetSpellProto();
                    if (spellProto->CasterAuraState == flag)
                    {
                        // exceptions (applied at state but not removed at state change)
                        // Rampage
                        if(spellProto->SpellIconID==2006 && spellProto->SpellFamilyName==SPELLFAMILY_WARRIOR && spellProto->SpellFamilyFlags==0x100000)
                        {
                            ++itr;
                            continue;
                        }

                        RemoveAura(itr);
                    }
                    else
                        ++itr;
                }
            }
        }
    }
}

Unit *Unit::GetOwner() const
{
    if(uint64 ownerid = GetOwnerGUID())
        return ObjectAccessor::GetUnit(*this, ownerid);
    return NULL;
}

Unit *Unit::GetCharmer() const
{
    if(uint64 charmerid = GetCharmerGUID())
        return ObjectAccessor::GetUnit(*this, charmerid);
    return NULL;
}

bool Unit::IsCharmerOrOwnerPlayerOrPlayerItself() const
{
    if (GetTypeId()==TYPEID_PLAYER)
        return true;

    return IS_PLAYER_GUID(GetCharmerOrOwnerGUID());
}

Player* Unit::GetCharmerOrOwnerPlayerOrPlayerItself()
{
    uint64 guid = GetCharmerOrOwnerGUID();
    if(IS_PLAYER_GUID(guid))
        return ObjectAccessor::FindPlayer(guid);

    return GetTypeId()==TYPEID_PLAYER ? (Player*)this : NULL;
}

Pet* Unit::GetPet() const
{
    if(uint64 pet_guid = GetPetGUID())
    {
        if(Pet* pet = GetMap()->GetPet(pet_guid))
            return pet;

        sLog.outError("Unit::GetPet: Pet %u not exist.",GUID_LOPART(pet_guid));
        const_cast<Unit*>(this)->SetPet(0);
    }

    return NULL;
}

Unit* Unit::GetCharm() const
{
    if (uint64 charm_guid = GetCharmGUID())
    {
        if(Unit* pet = ObjectAccessor::GetUnit(*this, charm_guid))
            return pet;

        sLog.outError("Unit::GetCharm: Charmed creature %u not exist.",GUID_LOPART(charm_guid));
        const_cast<Unit*>(this)->SetCharm(NULL);
    }

    return NULL;
}

void Unit::Uncharm()
{
    if (Unit* charm = GetCharm())
    {
        charm->RemoveSpellsCausingAura(SPELL_AURA_MOD_CHARM);
        charm->RemoveSpellsCausingAura(SPELL_AURA_MOD_POSSESS);
    }
}

float Unit::GetCombatDistance( const Unit* target ) const
{
    float radius = target->GetFloatValue(UNIT_FIELD_COMBATREACH) + GetFloatValue(UNIT_FIELD_COMBATREACH);
    float dx = GetPositionX() - target->GetPositionX();
    float dy = GetPositionY() - target->GetPositionY();
    float dz = GetPositionZ() - target->GetPositionZ();
    float dist = sqrt((dx*dx) + (dy*dy) + (dz*dz)) - radius;
    return ( dist > 0 ? dist : 0);
}

void Unit::SetPet(Pet* pet)
{
    SetPetGUID(pet ? pet->GetGUID() : 0);

    if(pet && GetTypeId() == TYPEID_PLAYER)
        ((Player*)this)->SendPetGUIDs();

    // FIXME: hack, speed must be set only at follow
    if(pet && GetTypeId()==TYPEID_PLAYER)
        for(int i = 0; i < MAX_MOVE_TYPE; ++i)
            pet->SetSpeedRate(UnitMoveType(i), m_speed_rate[i], true);
}

void Unit::SetCharm(Unit* pet)
{
    SetCharmGUID(pet ? pet->GetGUID() : 0);
}

void Unit::AddGuardian( Pet* pet )
{
    m_guardianPets.insert(pet->GetGUID());
}

void Unit::RemoveGuardian( Pet* pet )
{
    m_guardianPets.erase(pet->GetGUID());
}

void Unit::RemoveGuardians()
{
    while(!m_guardianPets.empty())
    {
        uint64 guid = *m_guardianPets.begin();
        if(Pet* pet = GetMap()->GetPet(guid))
            pet->Remove(PET_SAVE_AS_DELETED);

        m_guardianPets.erase(guid);
    }
}

Pet* Unit::FindGuardianWithEntry(uint32 entry)
{
    // pet guid middle part is entry (and creature also)
    // and in guardian list must be guardians with same entry _always_
    for(GuardianPetList::const_iterator itr = m_guardianPets.begin(); itr != m_guardianPets.end(); ++itr)
        if(Pet* pet = GetMap()->GetPet(*itr))
            if (pet->GetEntry() == entry)
                return pet;

    return NULL;
}

Unit* Unit::_GetTotem(uint8 slot) const
{
    return GetTotem(slot);
}

Totem* Unit::GetTotem( uint8 slot ) const
{
    if(slot >= MAX_TOTEM || !IsInWorld())
        return NULL;

    Creature *totem = GetMap()->GetCreature(m_TotemSlot[slot]);
    return totem && totem->isTotem() ? (Totem*)totem : NULL;
}

void Unit::UnsummonAllTotems()
{
    for (int8 i = 0; i < MAX_TOTEM; ++i)
    {
        if(!m_TotemSlot[i])
            continue;

        Creature *OldTotem = GetMap()->GetCreature(m_TotemSlot[i]);
        if (OldTotem && OldTotem->isTotem())
            ((Totem*)OldTotem)->UnSummon();
    }
}

int32 Unit::DealHeal(Unit *pVictim, uint32 addhealth, SpellEntry const *spellProto, bool critical)
{
    int32 gain = pVictim->ModifyHealth(int32(addhealth));

    Unit* unit = this;

    if( GetTypeId()==TYPEID_UNIT && ((Creature*)this)->isTotem() && ((Totem*)this)->GetTotemType()!=TOTEM_STATUE)
        unit = GetOwner();

    if (unit->GetTypeId()==TYPEID_PLAYER)
    {
        // overheal = addhealth - gain
        unit->SendHealSpellLog(pVictim, spellProto->Id, addhealth, addhealth - gain, critical);

        if (BattleGround *bg = ((Player*)unit)->GetBattleGround())
            bg->UpdatePlayerScore((Player*)unit, SCORE_HEALING_DONE, gain);

        // use the actual gain, as the overheal shall not be counted, skip gain 0 (it ignored anyway in to criteria)
        if (gain)
            ((Player*)unit)->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HEALING_DONE, gain, 0, pVictim);

        ((Player*)unit)->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HEAL_CASTED, addhealth);
    }

    if (pVictim->GetTypeId()==TYPEID_PLAYER)
    {
        ((Player*)pVictim)->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_TOTAL_HEALING_RECEIVED, gain);
        ((Player*)pVictim)->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HEALING_RECEIVED, addhealth);
    }

    return gain;
}

Unit* Unit::SelectMagnetTarget(Unit *victim, SpellEntry const *spellInfo)
{
    if(!victim)
        return NULL;

    // Magic case
    if(spellInfo && (spellInfo->DmgClass == SPELL_DAMAGE_CLASS_NONE || spellInfo->DmgClass == SPELL_DAMAGE_CLASS_MAGIC))
    {
        Unit::AuraList const& magnetAuras = victim->GetAurasByType(SPELL_AURA_SPELL_MAGNET);
        for(Unit::AuraList::const_iterator itr = magnetAuras.begin(); itr != magnetAuras.end(); ++itr)
            if(Unit* magnet = (*itr)->GetCaster())
                if(magnet->IsWithinLOSInMap(this) && magnet->isAlive())
                    return magnet;
    }
    // Melee && ranged case
    else
    {
        AuraList const& hitTriggerAuras = victim->GetAurasByType(SPELL_AURA_ADD_CASTER_HIT_TRIGGER);
        for(AuraList::const_iterator i = hitTriggerAuras.begin(); i != hitTriggerAuras.end(); ++i)
            if(Unit* magnet = (*i)->GetCaster())
                if(magnet->isAlive() && magnet->IsWithinLOSInMap(this))
                    if(roll_chance_i((*i)->GetModifier()->m_amount))
                        return magnet;
    }

    return victim;
}

void Unit::SendHealSpellLog(Unit *pVictim, uint32 SpellID, uint32 Damage, uint32 OverHeal, bool critical)
{
    // we guess size
    WorldPacket data(SMSG_SPELLHEALLOG, (8+8+4+4+1));
    data.append(pVictim->GetPackGUID());
    data.append(GetPackGUID());
    data << uint32(SpellID);
    data << uint32(Damage);
    data << uint32(OverHeal);
    data << uint8(critical ? 1 : 0);
    data << uint8(0);                                       // unused in client?
    SendMessageToSet(&data, true);
}

void Unit::SendEnergizeSpellLog(Unit *pVictim, uint32 SpellID, uint32 Damage, Powers powertype)
{
    WorldPacket data(SMSG_SPELLENERGIZELOG, (8+8+4+4+4+1));
    data.append(pVictim->GetPackGUID());
    data.append(GetPackGUID());
    data << uint32(SpellID);
    data << uint32(powertype);
    data << uint32(Damage);
    SendMessageToSet(&data, true);
}

void Unit::EnergizeBySpell(Unit *pVictim, uint32 SpellID, uint32 Damage, Powers powertype)
{
    SendEnergizeSpellLog(pVictim, SpellID, Damage, powertype);
    // needs to be called after sending spell log
    pVictim->ModifyPower(powertype, Damage);
}

uint32 Unit::SpellDamageBonus(Unit *pVictim, SpellEntry const *spellProto, uint32 pdamage, DamageEffectType damagetype, uint32 stack)
{
    if(!spellProto || !pVictim || damagetype==DIRECT_DAMAGE )
        return pdamage;

    // For totems get damage bonus from owner (statue isn't totem in fact)
    if( GetTypeId()==TYPEID_UNIT && ((Creature*)this)->isTotem() && ((Totem*)this)->GetTotemType()!=TOTEM_STATUE)
    {
        if(Unit* owner = GetOwner())
            return owner->SpellDamageBonus(pVictim, spellProto, pdamage, damagetype);
    }

    // Taken/Done total percent damage auras
    float DoneTotalMod = 1.0f;
    float TakenTotalMod = 1.0f;
    int32 DoneTotal = 0;
    int32 TakenTotal = 0;

    // ..done
    // Creature damage
    if( GetTypeId() == TYPEID_UNIT && !((Creature*)this)->isPet() )
        DoneTotalMod *= ((Creature*)this)->GetSpellDamageMod(((Creature*)this)->GetCreatureInfo()->rank);

    if (!(spellProto->AttributesEx6 & SPELL_ATTR_EX6_NO_DMG_PERCENT_MODS))
    {
        AuraList const& mModDamagePercentDone = GetAurasByType(SPELL_AURA_MOD_DAMAGE_PERCENT_DONE);
        for(AuraList::const_iterator i = mModDamagePercentDone.begin(); i != mModDamagePercentDone.end(); ++i)
        {
            if( ((*i)->GetModifier()->m_miscvalue & GetSpellSchoolMask(spellProto)) &&
                (*i)->GetSpellProto()->EquippedItemClass == -1 &&
                                                                // -1 == any item class (not wand then)
                (*i)->GetSpellProto()->EquippedItemInventoryTypeMask == 0 )
                                                                // 0 == any inventory type (not wand then)
            {
                DoneTotalMod *= ((*i)->GetModifier()->m_amount+100.0f)/100.0f;
            }
        }
    }

    uint32 creatureTypeMask = pVictim->GetCreatureTypeMask();
    // Add flat bonus from spell damage versus
    DoneTotal += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_FLAT_SPELL_DAMAGE_VERSUS, creatureTypeMask);
    AuraList const& mDamageDoneVersus = GetAurasByType(SPELL_AURA_MOD_DAMAGE_DONE_VERSUS);
    for(AuraList::const_iterator i = mDamageDoneVersus.begin();i != mDamageDoneVersus.end(); ++i)
        if(creatureTypeMask & uint32((*i)->GetModifier()->m_miscvalue))
            DoneTotalMod *= ((*i)->GetModifier()->m_amount+100.0f)/100.0f;

    // done scripted mod (take it from owner)
    Unit *owner = GetOwner();
    if (!owner) owner = this;
    AuraList const& mOverrideClassScript= owner->GetAurasByType(SPELL_AURA_OVERRIDE_CLASS_SCRIPTS);
    for(AuraList::const_iterator i = mOverrideClassScript.begin(); i != mOverrideClassScript.end(); ++i)
    {
        if (!(*i)->isAffectedOnSpell(spellProto))
            continue;
        switch((*i)->GetModifier()->m_miscvalue)
        {
            case 4920: // Molten Fury
            case 4919:
            case 6917: // Death's Embrace
            case 6926:
            case 6928:
            {
                if(pVictim->HasAuraState(AURA_STATE_HEALTHLESS_35_PERCENT))
                    DoneTotalMod *= (100.0f+(*i)->GetModifier()->m_amount)/100.0f;
                break;
            }
            // Soul Siphon
            case 4992:
            case 4993:
            {
                // effect 1 m_amount
                int32 maxPercent = (*i)->GetModifier()->m_amount;
                // effect 0 m_amount
                int32 stepPercent = CalculateSpellDamage((*i)->GetSpellProto(), EFFECT_INDEX_0, (*i)->GetSpellProto()->EffectBasePoints[EFFECT_INDEX_0], this);
                // count affliction effects and calc additional damage in percentage
                int32 modPercent = 0;
                AuraMap const& victimAuras = pVictim->GetAuras();
                for (AuraMap::const_iterator itr = victimAuras.begin(); itr != victimAuras.end(); ++itr)
                {
                    SpellEntry const* m_spell = itr->second->GetSpellProto();
                    if (m_spell->SpellFamilyName != SPELLFAMILY_WARLOCK || !(m_spell->SpellFamilyFlags & UI64LIT(0x0004071B8044C402)))
                        continue;
                    modPercent += stepPercent * itr->second->GetStackAmount();
                    if (modPercent >= maxPercent)
                    {
                        modPercent = maxPercent;
                        break;
                    }
                }
                DoneTotalMod *= (modPercent+100.0f)/100.0f;
                break;
            }
            case 6916: // Death's Embrace
            case 6925:
            case 6927:
                if (HasAuraState(AURA_STATE_HEALTHLESS_20_PERCENT))
                    DoneTotalMod *= (100.0f+(*i)->GetModifier()->m_amount)/100.0f;
                break;
            case 5481: // Starfire Bonus
            {
                if (pVictim->GetAura(SPELL_AURA_PERIODIC_DAMAGE, SPELLFAMILY_DRUID, UI64LIT(0x0000000000200002)))
                    DoneTotalMod *= ((*i)->GetModifier()->m_amount+100.0f)/100.0f;
                break;
            }
            case 4418: // Increased Shock Damage
            case 4554: // Increased Lightning Damage
            case 4555: // Improved Moonfire
            case 5142: // Increased Lightning Damage
            case 5147: // Improved Consecration / Libram of Resurgence
            case 5148: // Idol of the Shooting Star
            case 6008: // Increased Lightning Damage / Totem of Hex
            {
                DoneTotal+=(*i)->GetModifier()->m_amount;
                break;
            }
            // Tundra Stalker
            // Merciless Combat
            case 7277:
            {
                // Merciless Combat
                if ((*i)->GetSpellProto()->SpellIconID == 2656)
                {
                    if(pVictim->HasAuraState(AURA_STATE_HEALTHLESS_35_PERCENT))
                        DoneTotalMod *= (100.0f+(*i)->GetModifier()->m_amount)/100.0f;
                }
                else // Tundra Stalker
                {
                    // Frost Fever (target debuff)
                    if (pVictim->GetAura(SPELL_AURA_MOD_HASTE, SPELLFAMILY_DEATHKNIGHT, UI64LIT(0x0000000000000000), 0x00000002))
                        DoneTotalMod *= ((*i)->GetModifier()->m_amount+100.0f)/100.0f;
                    break;
                }
                break;
            }
            case 7293: // Rage of Rivendare
            {
                if (pVictim->GetAura(SPELL_AURA_PERIODIC_DAMAGE, SPELLFAMILY_DEATHKNIGHT, UI64LIT(0x0200000000000000)))
                    DoneTotalMod *= ((*i)->GetModifier()->m_amount+100.0f)/100.0f;
                break;
            }
            // Twisted Faith
            case 7377:
            {
                if (pVictim->GetAura(SPELL_AURA_PERIODIC_DAMAGE, SPELLFAMILY_PRIEST, UI64LIT(0x0000000000008000), 0, GetGUID()))
                    DoneTotalMod *= ((*i)->GetModifier()->m_amount+100.0f)/100.0f;
                break;
            }
            // Marked for Death
            case 7598:
            case 7599:
            case 7600:
            case 7601:
            case 7602:
            {
                if (pVictim->GetAura(SPELL_AURA_MOD_STALKED, SPELLFAMILY_HUNTER, UI64LIT(0x0000000000000400)))
                    DoneTotalMod *= ((*i)->GetModifier()->m_amount+100.0f)/100.0f;
                break;
            }
        }
    }

    // Custom scripted damage
    switch(spellProto->SpellFamilyName)
    {
        case SPELLFAMILY_MAGE:
        {
            // Ice Lance
            if (spellProto->SpellIconID == 186)
            {
                if (pVictim->isFrozen())
                {
                    float multiplier = 3.0f;

                    // if target have higher level
                    if (pVictim->getLevel() > getLevel())
                        // Glyph of Ice Lance
                        if (Aura* glyph = GetDummyAura(56377))
                            multiplier = glyph->GetModifier()->m_amount;

                    DoneTotalMod *= multiplier;
                }
            }
            // Torment the weak affected (Arcane Barrage, Arcane Blast, Frostfire Bolt, Arcane Missiles, Fireball)
            if ((spellProto->SpellFamilyFlags & UI64LIT(0x0000900020200021)) &&
                (pVictim->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) || pVictim->HasAuraType(SPELL_AURA_MELEE_SLOW)))
            {
                //Search for Torment the weak dummy aura
                Unit::AuraList const& ttw = GetAurasByType(SPELL_AURA_DUMMY);
                for(Unit::AuraList::const_iterator i = ttw.begin(); i != ttw.end(); ++i)
                {
                    if ((*i)->GetSpellProto()->SpellIconID == 3263)
                    {
                        DoneTotalMod *= ((*i)->GetModifier()->m_amount+100.0f) / 100.0f;
                        break;
                    }
                }
            }
            break;
        }
        case SPELLFAMILY_WARLOCK:
        {
            // Drain Soul
            if (spellProto->SpellFamilyFlags & UI64LIT(0x0000000000004000))
            {
                if (pVictim->GetHealth() * 100 / pVictim->GetMaxHealth() <= 25)
                  DoneTotalMod *= 4;
            }
            break;
        }
        case SPELLFAMILY_DEATHKNIGHT:
        {
            // Icy Touch, Howling Blast and Frost Strike
            if (spellProto->SpellFamilyFlags & UI64LIT(0x0000000600000002))
            {
                // search disease
                bool found = false;
                Unit::AuraMap const& auras = pVictim->GetAuras();
                for(Unit::AuraMap::const_iterator itr = auras.begin(); itr!=auras.end(); ++itr)
                {
                    if(itr->second->GetSpellProto()->Dispel == DISPEL_DISEASE)
                    {
                        found = true;
                        break;
                    }
                }
                if(!found)
                    break;

                // search for Glacier Rot dummy aura
                Unit::AuraList const& dummyAuras = GetAurasByType(SPELL_AURA_DUMMY);
                for(Unit::AuraList::const_iterator i = dummyAuras.begin(); i != dummyAuras.end(); ++i)
                {
                    if ((*i)->GetSpellProto()->EffectMiscValue[(*i)->GetEffIndex()] == 7244)
                    {
                        DoneTotalMod *= ((*i)->GetModifier()->m_amount+100.0f) / 100.0f;
                        break;
                    }
                }
            }
            break;
        }
        default:
            break;
    }


    // ..taken
    AuraList const& mModDamagePercentTaken = pVictim->GetAurasByType(SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN);
    for(AuraList::const_iterator i = mModDamagePercentTaken.begin(); i != mModDamagePercentTaken.end(); ++i)
    {
        if ((*i)->GetModifier()->m_miscvalue & GetSpellSchoolMask(spellProto))
            TakenTotalMod *= ((*i)->GetModifier()->m_amount + 100.0f) / 100.0f;
    }

    // .. taken pct: dummy auras
    if (pVictim->GetTypeId() == TYPEID_PLAYER)
    {
        //Cheat Death
        if (Aura *dummy = pVictim->GetDummyAura(45182))
        {
            float mod = -((Player*)pVictim)->GetRatingBonusValue(CR_CRIT_TAKEN_SPELL)*2*4;
            if (mod < float(dummy->GetModifier()->m_amount))
                mod = float(dummy->GetModifier()->m_amount);
            TakenTotalMod *= (mod+100.0f)/100.0f;
        }
    }

    // From caster spells
    AuraList const& mOwnerTaken = pVictim->GetAurasByType(SPELL_AURA_MOD_DAMAGE_FROM_CASTER);
    for(AuraList::const_iterator i = mOwnerTaken.begin(); i != mOwnerTaken.end(); ++i)
    {
        if ((*i)->GetCasterGUID() == GetGUID() && (*i)->isAffectedOnSpell(spellProto))
            TakenTotalMod *= ((*i)->GetModifier()->m_amount + 100.0f) / 100.0f;
    }

    // Mod damage from spell mechanic
    TakenTotalMod *= pVictim->GetTotalAuraMultiplierByMiscValueForMask(SPELL_AURA_MOD_MECHANIC_DAMAGE_TAKEN_PERCENT,GetAllSpellMechanicMask(spellProto));

    // Mod damage taken from AoE spells
    if(IsAreaOfEffectSpell(spellProto))
    {
        AuraList const& avoidAuras = pVictim->GetAurasByType(SPELL_AURA_MOD_AOE_DAMAGE_AVOIDANCE);
        for(AuraList::const_iterator itr = avoidAuras.begin(); itr != avoidAuras.end(); ++itr)
            TakenTotalMod *= ((*itr)->GetModifier()->m_amount + 100.0f) / 100.0f;
    }

    // Taken/Done fixed damage bonus auras
    int32 DoneAdvertisedBenefit  = SpellBaseDamageBonus(GetSpellSchoolMask(spellProto));
    int32 TakenAdvertisedBenefit = SpellBaseDamageBonusForVictim(GetSpellSchoolMask(spellProto), pVictim);

    // Pets just add their bonus damage to their spell damage
    // note that their spell damage is just gain of their own auras
    if (GetTypeId() == TYPEID_UNIT && ((Creature*)this)->isPet())
        DoneAdvertisedBenefit += ((Pet*)this)->GetBonusDamage();

    float LvlPenalty = CalculateLevelPenalty(spellProto);
    // Spellmod SpellDamage
    float SpellModSpellDamage = 100.0f;
    if(Player* modOwner = GetSpellModOwner())
        modOwner->ApplySpellMod(spellProto->Id,SPELLMOD_SPELL_BONUS_DAMAGE,SpellModSpellDamage);
    SpellModSpellDamage /= 100.0f;

    // Check for table values
    if (SpellBonusEntry const* bonus = sSpellMgr.GetSpellBonusData(spellProto->Id))
    {
        float coeff;
        if (damagetype == DOT)
            coeff = bonus->dot_damage * LvlPenalty * stack;
        else
            coeff = bonus->direct_damage * LvlPenalty * stack;

        if (bonus->ap_bonus)
            DoneTotal += int32(bonus->ap_bonus * GetTotalAttackPowerValue(BASE_ATTACK) * stack);

        DoneTotal  += int32(DoneAdvertisedBenefit * coeff * SpellModSpellDamage);
        TakenTotal += int32(TakenAdvertisedBenefit * coeff);
    }
    // Default calculation
    else if (DoneAdvertisedBenefit || TakenAdvertisedBenefit)
    {
        // Damage over Time spells bonus calculation
        float DotFactor = 1.0f;
        if (damagetype == DOT)
        {
            if (!IsChanneledSpell(spellProto))
                DotFactor = GetSpellDuration(spellProto) / 15000.0f;

            if (uint16 DotTicks = GetSpellAuraMaxTicks(spellProto))
            {
                DoneAdvertisedBenefit = DoneAdvertisedBenefit * int32(stack) / DotTicks;
                TakenAdvertisedBenefit = TakenAdvertisedBenefit * int32(stack) / DotTicks;
            }
        }
        // Distribute Damage over multiple effects, reduce by AoE
        uint32 CastingTime = !IsChanneledSpell(spellProto) ? GetSpellCastTime(spellProto) : GetSpellDuration(spellProto);
        CastingTime = GetCastingTimeForBonus( spellProto, damagetype, CastingTime );
        // 50% for damage and healing spells for leech spells from damage bonus and 0% from healing
        for(int j = 0; j < MAX_EFFECT_INDEX; ++j)
        {
            if (spellProto->Effect[j] == SPELL_EFFECT_HEALTH_LEECH ||
                (spellProto->Effect[j] == SPELL_EFFECT_APPLY_AURA &&
                spellProto->EffectApplyAuraName[j] == SPELL_AURA_PERIODIC_LEECH))
            {
                CastingTime /= 2;
                break;
            }
        }
        DoneTotal += int32(DoneAdvertisedBenefit * (CastingTime / 3500.0f) * DotFactor * LvlPenalty * SpellModSpellDamage);
        TakenTotal+= int32(TakenAdvertisedBenefit * (CastingTime / 3500.0f) * DotFactor * LvlPenalty);
    }

    float tmpDamage = (pdamage + DoneTotal) * DoneTotalMod;
    // apply spellmod to Done damage (flat and pct)
    if(Player* modOwner = GetSpellModOwner())
        modOwner->ApplySpellMod(spellProto->Id, damagetype == DOT ? SPELLMOD_DOT : SPELLMOD_DAMAGE, tmpDamage);

    tmpDamage = (tmpDamage + TakenTotal) * TakenTotalMod;

    return tmpDamage > 0 ? uint32(tmpDamage) : 0;
}

int32 Unit::SpellBaseDamageBonus(SpellSchoolMask schoolMask)
{
    int32 DoneAdvertisedBenefit = 0;

    // ..done
    AuraList const& mDamageDone = GetAurasByType(SPELL_AURA_MOD_DAMAGE_DONE);
    for(AuraList::const_iterator i = mDamageDone.begin();i != mDamageDone.end(); ++i)
    {
        if (((*i)->GetModifier()->m_miscvalue & schoolMask) != 0 &&
            (*i)->GetSpellProto()->EquippedItemClass == -1 &&                   // -1 == any item class (not wand then)
            (*i)->GetSpellProto()->EquippedItemInventoryTypeMask == 0)          //  0 == any inventory type (not wand then)
                DoneAdvertisedBenefit += (*i)->GetModifier()->m_amount;
    }

    if (GetTypeId() == TYPEID_PLAYER)
    {
        // Base value
        DoneAdvertisedBenefit +=((Player*)this)->GetBaseSpellPowerBonus();

        // Damage bonus from stats
        AuraList const& mDamageDoneOfStatPercent = GetAurasByType(SPELL_AURA_MOD_SPELL_DAMAGE_OF_STAT_PERCENT);
        for(AuraList::const_iterator i = mDamageDoneOfStatPercent.begin();i != mDamageDoneOfStatPercent.end(); ++i)
        {
            if((*i)->GetModifier()->m_miscvalue & schoolMask)
            {
                // stat used stored in miscValueB for this aura
                Stats usedStat = Stats((*i)->GetMiscBValue());
                DoneAdvertisedBenefit += int32(GetStat(usedStat) * (*i)->GetModifier()->m_amount / 100.0f);
            }
        }
        // ... and attack power
        AuraList const& mDamageDonebyAP = GetAurasByType(SPELL_AURA_MOD_SPELL_DAMAGE_OF_ATTACK_POWER);
        for(AuraList::const_iterator i =mDamageDonebyAP.begin();i != mDamageDonebyAP.end(); ++i)
        {
            if ((*i)->GetModifier()->m_miscvalue & schoolMask)
                DoneAdvertisedBenefit += int32(GetTotalAttackPowerValue(BASE_ATTACK) * (*i)->GetModifier()->m_amount / 100.0f);
        }

    }
    return DoneAdvertisedBenefit;
}

int32 Unit::SpellBaseDamageBonusForVictim(SpellSchoolMask schoolMask, Unit *pVictim)
{
    uint32 creatureTypeMask = pVictim->GetCreatureTypeMask();

    int32 TakenAdvertisedBenefit = 0;
    // ..done (for creature type by mask) in taken
    AuraList const& mDamageDoneCreature = GetAurasByType(SPELL_AURA_MOD_DAMAGE_DONE_CREATURE);
    for(AuraList::const_iterator i = mDamageDoneCreature.begin();i != mDamageDoneCreature.end(); ++i)
    {
        if(creatureTypeMask & uint32((*i)->GetModifier()->m_miscvalue))
            TakenAdvertisedBenefit += (*i)->GetModifier()->m_amount;
    }

    // ..taken
    AuraList const& mDamageTaken = pVictim->GetAurasByType(SPELL_AURA_MOD_DAMAGE_TAKEN);
    for(AuraList::const_iterator i = mDamageTaken.begin();i != mDamageTaken.end(); ++i)
    {
        if(((*i)->GetModifier()->m_miscvalue & schoolMask) != 0)
            TakenAdvertisedBenefit += (*i)->GetModifier()->m_amount;
    }

    return TakenAdvertisedBenefit;
}

bool Unit::isSpellCrit(Unit *pVictim, SpellEntry const *spellProto, SpellSchoolMask schoolMask, WeaponAttackType attackType)
{
    // not critting spell
    if((spellProto->AttributesEx2 & SPELL_ATTR_EX2_CANT_CRIT))
        return false;

    float crit_chance = 0.0f;
    switch(spellProto->DmgClass)
    {
        case SPELL_DAMAGE_CLASS_NONE:
            return false;
        case SPELL_DAMAGE_CLASS_MAGIC:
        {
            if (schoolMask & SPELL_SCHOOL_MASK_NORMAL)
                crit_chance = 0.0f;
            // For other schools
            else if (GetTypeId() == TYPEID_PLAYER)
                crit_chance = GetFloatValue( PLAYER_SPELL_CRIT_PERCENTAGE1 + GetFirstSchoolInMask(schoolMask));
            else
            {
                crit_chance = float(m_baseSpellCritChance);
                crit_chance += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL, schoolMask);
            }
            // taken
            if (pVictim)
            {
                if (!IsPositiveSpell(spellProto->Id))
                {
                    // Modify critical chance by victim SPELL_AURA_MOD_ATTACKER_SPELL_CRIT_CHANCE
                    crit_chance += pVictim->GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_ATTACKER_SPELL_CRIT_CHANCE, schoolMask);
                    // Modify critical chance by victim SPELL_AURA_MOD_ATTACKER_SPELL_AND_WEAPON_CRIT_CHANCE
                    crit_chance += pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_SPELL_AND_WEAPON_CRIT_CHANCE);
                    // Modify by player victim resilience
                    crit_chance -= pVictim->GetSpellCritChanceReduction();
                }

                // scripted (increase crit chance ... against ... target by x%)
                // scripted (Increases the critical effect chance of your .... by x% on targets ...)
                AuraList const& mOverrideClassScript = GetAurasByType(SPELL_AURA_OVERRIDE_CLASS_SCRIPTS);
                for(AuraList::const_iterator i = mOverrideClassScript.begin(); i != mOverrideClassScript.end(); ++i)
                {
                    if (!((*i)->isAffectedOnSpell(spellProto)))
                        continue;
                    switch((*i)->GetModifier()->m_miscvalue)
                    {
                        case  849: if (pVictim->isFrozen()) crit_chance+= 17.0f; break; //Shatter Rank 1
                        case  910: if (pVictim->isFrozen()) crit_chance+= 34.0f; break; //Shatter Rank 2
                        case  911: if (pVictim->isFrozen()) crit_chance+= 50.0f; break; //Shatter Rank 3
                        case 7917:                          // Glyph of Shadowburn
                            if (pVictim->HasAuraState(AURA_STATE_HEALTHLESS_35_PERCENT))
                                crit_chance+=(*i)->GetModifier()->m_amount;
                            break;
                        case 7997:                          // Renewed Hope
                        case 7998:
                            if (pVictim->HasAura(6788))
                                crit_chance+=(*i)->GetModifier()->m_amount;
                            break;
                        default:
                            break;
                    }
                }
                // Custom crit by class
                switch(spellProto->SpellFamilyName)
                {
                    case SPELLFAMILY_PRIEST:
                        // Flash Heal
                        if (spellProto->SpellFamilyFlags & UI64LIT(0x0000000000000800))
                        {
                            if (pVictim->GetHealth() > pVictim->GetMaxHealth()/2)
                                break;
                            AuraList const& mDummyAuras = GetAurasByType(SPELL_AURA_DUMMY);
                            for(AuraList::const_iterator i = mDummyAuras.begin(); i!= mDummyAuras.end(); ++i)
                            {
                                // Improved Flash Heal
                                if ((*i)->GetSpellProto()->SpellFamilyName == SPELLFAMILY_PRIEST &&
                                    (*i)->GetSpellProto()->SpellIconID == 2542)
                                {
                                    crit_chance+=(*i)->GetModifier()->m_amount;
                                    break;
                                }
                            }
                        }
                        break;
                    case SPELLFAMILY_PALADIN:
                        // Sacred Shield
                        if (spellProto->SpellFamilyFlags & UI64LIT(0x0000000040000000))
                        {
                            Aura *aura = pVictim->GetDummyAura(58597);
                            if (aura && aura->GetCasterGUID() == GetGUID())
                                crit_chance+=aura->GetModifier()->m_amount;
                        }
                        // Exorcism
                        else if (spellProto->Category == 19)
                        {
                            if (pVictim->GetCreatureTypeMask() & CREATURE_TYPEMASK_DEMON_OR_UNDEAD)
                                return true;
                        }
                        break;
                    case SPELLFAMILY_SHAMAN:
                        // Lava Burst
                        if (spellProto->SpellFamilyFlags & UI64LIT(0x0000100000000000))
                        {
                            // Flame Shock
                            if (pVictim->GetAura(SPELL_AURA_PERIODIC_DAMAGE, SPELLFAMILY_SHAMAN, UI64LIT(0x0000000010000000), 0, GetGUID()))
                                return true;
                        }
                        break;
                }
            }
            break;
        }
        case SPELL_DAMAGE_CLASS_MELEE:
        case SPELL_DAMAGE_CLASS_RANGED:
        {
            if (pVictim)
                crit_chance = GetUnitCriticalChance(attackType, pVictim);

            crit_chance+= GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL, schoolMask);
            break;
        }
        default:
            return false;
    }
    // percent done
    // only players use intelligence for critical chance computations
    if(Player* modOwner = GetSpellModOwner())
        modOwner->ApplySpellMod(spellProto->Id, SPELLMOD_CRITICAL_CHANCE, crit_chance);

    crit_chance = crit_chance > 0.0f ? crit_chance : 0.0f;
    if (roll_chance_f(crit_chance))
        return true;
    return false;
}

uint32 Unit::SpellCriticalDamageBonus(SpellEntry const *spellProto, uint32 damage, Unit *pVictim)
{
    // Calculate critical bonus
    int32 crit_bonus;
    switch(spellProto->DmgClass)
    {
        case SPELL_DAMAGE_CLASS_MELEE:                      // for melee based spells is 100%
        case SPELL_DAMAGE_CLASS_RANGED:
            crit_bonus = damage;
            break;
        default:
            crit_bonus = damage / 2;                        // for spells is 50%
            break;
    }

    // adds additional damage to crit_bonus (from talents)
    if(Player* modOwner = GetSpellModOwner())
        modOwner->ApplySpellMod(spellProto->Id, SPELLMOD_CRIT_DAMAGE_BONUS, crit_bonus);

    if(!pVictim)
        return damage += crit_bonus;

    int32 critPctDamageMod = 0;
    if(spellProto->DmgClass >= SPELL_DAMAGE_CLASS_MELEE)
    {
        if(GetWeaponAttackType(spellProto) == RANGED_ATTACK)
            critPctDamageMod += pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_RANGED_CRIT_DAMAGE);
        else
            critPctDamageMod += pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_ATTACKER_MELEE_CRIT_DAMAGE);
    }
    else
        critPctDamageMod += pVictim->GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_ATTACKER_SPELL_CRIT_DAMAGE,GetSpellSchoolMask(spellProto));

    critPctDamageMod += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_CRIT_DAMAGE_BONUS, GetSpellSchoolMask(spellProto));

    uint32 creatureTypeMask = pVictim->GetCreatureTypeMask();
    critPctDamageMod += GetTotalAuraMultiplierByMiscMask(SPELL_AURA_MOD_CRIT_PERCENT_VERSUS, creatureTypeMask);

    if(critPctDamageMod!=0)
        crit_bonus = int32(crit_bonus * float((100.0f + critPctDamageMod)/100.0f));

    if(crit_bonus > 0)
        damage += crit_bonus;

    return damage;
}

uint32 Unit::SpellCriticalHealingBonus(SpellEntry const *spellProto, uint32 damage, Unit *pVictim)
{
    // Calculate critical bonus
    int32 crit_bonus;
    switch(spellProto->DmgClass)
    {
        case SPELL_DAMAGE_CLASS_MELEE:                      // for melee based spells is 100%
        case SPELL_DAMAGE_CLASS_RANGED:
            // TODO: write here full calculation for melee/ranged spells
            crit_bonus = damage;
            break;
        default:
            crit_bonus = damage / 2;                        // for spells is 50%
            break;
    }

    if(pVictim)
    {
        uint32 creatureTypeMask = pVictim->GetCreatureTypeMask();
        crit_bonus = int32(crit_bonus * GetTotalAuraMultiplierByMiscMask(SPELL_AURA_MOD_CRIT_PERCENT_VERSUS, creatureTypeMask));
    }

    if(crit_bonus > 0)
        damage += crit_bonus;

    damage = int32(damage * GetTotalAuraMultiplier(SPELL_AURA_MOD_CRITICAL_HEALING_AMOUNT));

    return damage;
}

int32 Unit::SpellHealingBonus(Unit *pVictim, SpellEntry const *spellProto, int32 healamount, DamageEffectType damagetype, uint32 stack)
{
    // For totems get healing bonus from owner (statue isn't totem in fact)
    if( GetTypeId()==TYPEID_UNIT && ((Creature*)this)->isTotem() && ((Totem*)this)->GetTotemType()!=TOTEM_STATUE)
        if(Unit* owner = GetOwner())
            return owner->SpellHealingBonus(pVictim, spellProto, healamount, damagetype, stack);

    float  TakenTotalMod = 1.0f;

    // Healing taken percent
    float minval = float(pVictim->GetMaxNegativeAuraModifier(SPELL_AURA_MOD_HEALING_PCT));
    if(minval)
        TakenTotalMod *= (100.0f + minval) / 100.0f;

    float maxval = float(pVictim->GetMaxPositiveAuraModifier(SPELL_AURA_MOD_HEALING_PCT));
    if(maxval)
        TakenTotalMod *= (100.0f + maxval) / 100.0f;

    // No heal amount for this class spells
    if (spellProto->DmgClass == SPELL_DAMAGE_CLASS_NONE)
    {
        healamount = int32(healamount * TakenTotalMod);
        return healamount < 0 ? 0 : healamount;
    }

    // Healing Done
    // Taken/Done total percent damage auras
    float  DoneTotalMod = 1.0f;
    int32  DoneTotal = 0;
    int32  TakenTotal = 0;

    // Healing done percent
    AuraList const& mHealingDonePct = GetAurasByType(SPELL_AURA_MOD_HEALING_DONE_PERCENT);
    for(AuraList::const_iterator i = mHealingDonePct.begin();i != mHealingDonePct.end(); ++i)
        DoneTotalMod *= (100.0f + (*i)->GetModifier()->m_amount) / 100.0f;

    // done scripted mod (take it from owner)
    Unit *owner = GetOwner();
    if (!owner) owner = this;
    AuraList const& mOverrideClassScript= owner->GetAurasByType(SPELL_AURA_OVERRIDE_CLASS_SCRIPTS);
    for(AuraList::const_iterator i = mOverrideClassScript.begin(); i != mOverrideClassScript.end(); ++i)
    {
        if (!(*i)->isAffectedOnSpell(spellProto))
            continue;
        switch((*i)->GetModifier()->m_miscvalue)
        {
            case 4415: // Increased Rejuvenation Healing
            case 4953:
            case 3736: // Hateful Totem of the Third Wind / Increased Lesser Healing Wave / LK Arena (4/5/6) Totem of the Third Wind / Savage Totem of the Third Wind
                DoneTotal+=(*i)->GetModifier()->m_amount;
                break;
            case 7997: // Renewed Hope
            case 7998:
                if (pVictim->HasAura(6788))
                    DoneTotalMod *=((*i)->GetModifier()->m_amount + 100.0f)/100.0f;
                break;
            case   21: // Test of Faith
            case 6935:
            case 6918:
                if (pVictim->GetHealth() < pVictim->GetMaxHealth()/2)
                    DoneTotalMod *=((*i)->GetModifier()->m_amount + 100.0f)/100.0f;
                break;
            case 7798: // Glyph of Regrowth
            {
                if (pVictim->GetAura(SPELL_AURA_PERIODIC_HEAL, SPELLFAMILY_DRUID, UI64LIT(0x0000000000000040)))
                    DoneTotalMod *= ((*i)->GetModifier()->m_amount+100.0f)/100.0f;
                break;
            }
            case 8477: // Nourish Heal Boost
            {
                int32 stepPercent = (*i)->GetModifier()->m_amount;

                int ownHotCount = 0;                        // counted HoT types amount, not stacks

                Unit::AuraList const& RejorRegr = pVictim->GetAurasByType(SPELL_AURA_PERIODIC_HEAL);
                for(Unit::AuraList::const_iterator i = RejorRegr.begin(); i != RejorRegr.end(); ++i)
                    if ((*i)->GetSpellProto()->SpellFamilyName == SPELLFAMILY_DRUID &&
                        (*i)->GetCasterGUID() == GetGUID())
                        ++ownHotCount;

                if (ownHotCount)
                    TakenTotalMod *= (stepPercent * ownHotCount + 100.0f) / 100.0f;
                break;
            }
            case 7871: // Glyph of Lesser Healing Wave
            {
                if (pVictim->GetAura(SPELL_AURA_DUMMY, SPELLFAMILY_SHAMAN, UI64LIT(0x0000040000000000), 0, GetGUID()))
                    DoneTotalMod *= ((*i)->GetModifier()->m_amount+100.0f)/100.0f;
                break;
            }
            default:
                break;
        }
    }

    // Taken/Done fixed damage bonus auras
    int32 DoneAdvertisedBenefit  = SpellBaseHealingBonus(GetSpellSchoolMask(spellProto));
    int32 TakenAdvertisedBenefit = SpellBaseHealingBonusForVictim(GetSpellSchoolMask(spellProto), pVictim);

    float LvlPenalty = CalculateLevelPenalty(spellProto);
    // Spellmod SpellDamage
    float SpellModSpellDamage = 100.0f;
    if(Player* modOwner = GetSpellModOwner())
        modOwner->ApplySpellMod(spellProto->Id, SPELLMOD_SPELL_BONUS_DAMAGE, SpellModSpellDamage);
    SpellModSpellDamage /= 100.0f;

    // Check for table values
    SpellBonusEntry const* bonus = sSpellMgr.GetSpellBonusData(spellProto->Id);
    if (bonus)
    {
        float coeff;
        if (damagetype == DOT)
            coeff = bonus->dot_damage * LvlPenalty * stack;
        else
            coeff = bonus->direct_damage * LvlPenalty * stack;

        if (bonus->ap_bonus)
            DoneTotal += int32(bonus->ap_bonus * GetTotalAttackPowerValue(BASE_ATTACK) * stack);

        DoneTotal  += int32(DoneAdvertisedBenefit * coeff * SpellModSpellDamage);
        TakenTotal += int32(TakenAdvertisedBenefit * coeff);
    }
    // Default calculation
    else if (DoneAdvertisedBenefit || TakenAdvertisedBenefit)
    {
        // Damage over Time spells bonus calculation
        float DotFactor = 1.0f;
        if(damagetype == DOT)
        {
            if(!IsChanneledSpell(spellProto))
                DotFactor = GetSpellDuration(spellProto) / 15000.0f;
            uint16 DotTicks = GetSpellAuraMaxTicks(spellProto);
            if(DotTicks)
            {
                DoneAdvertisedBenefit = DoneAdvertisedBenefit * int32(stack) / DotTicks;
                TakenAdvertisedBenefit = TakenAdvertisedBenefit * int32(stack) / DotTicks;
            }
        }
        // Distribute Damage over multiple effects, reduce by AoE
        uint32 CastingTime = !IsChanneledSpell(spellProto) ? GetSpellCastTime(spellProto) : GetSpellDuration(spellProto);
        CastingTime = GetCastingTimeForBonus( spellProto, damagetype, CastingTime );
        // 50% for damage and healing spells for leech spells from damage bonus and 0% from healing
        for(int j = 0; j < MAX_EFFECT_INDEX; ++j)
        {
            if( spellProto->Effect[j] == SPELL_EFFECT_HEALTH_LEECH ||
                spellProto->Effect[j] == SPELL_EFFECT_APPLY_AURA && spellProto->EffectApplyAuraName[j] == SPELL_AURA_PERIODIC_LEECH )
            {
                CastingTime /= 2;
                break;
            }
        }
        DoneTotal  += int32(DoneAdvertisedBenefit * (CastingTime / 3500.0f) * DotFactor * LvlPenalty * SpellModSpellDamage * 1.88f);
        TakenTotal += int32(TakenAdvertisedBenefit * (CastingTime / 3500.0f) * DotFactor * LvlPenalty * 1.88f);
    }

    // use float as more appropriate for negative values and percent applying
    float heal = (healamount + DoneTotal)*DoneTotalMod;
    // apply spellmod to Done amount
    if(Player* modOwner = GetSpellModOwner())
        modOwner->ApplySpellMod(spellProto->Id, damagetype == DOT ? SPELLMOD_DOT : SPELLMOD_DAMAGE, heal);

    // Taken mods
    // Healing Wave cast
    if (spellProto->SpellFamilyName == SPELLFAMILY_SHAMAN && (spellProto->SpellFamilyFlags & UI64LIT(0x0000000000000040)))
    {
        // Search for Healing Way on Victim
        Unit::AuraList const& auraDummy = pVictim->GetAurasByType(SPELL_AURA_DUMMY);
        for(Unit::AuraList::const_iterator itr = auraDummy.begin(); itr!=auraDummy.end(); ++itr)
            if((*itr)->GetId() == 29203)
                TakenTotalMod *= ((*itr)->GetModifier()->m_amount+100.0f) / 100.0f;
    }
    // Nourish 20% of heal increase if target is affected by Druids HOTs
    else if (spellProto->SpellFamilyName == SPELLFAMILY_DRUID && (spellProto->SpellFamilyFlags & UI64LIT(0x0200000000000000)))
    {
        int ownHotCount = 0;                        // counted HoT types amount, not stacks
        Unit::AuraList const& RejorRegr = pVictim->GetAurasByType(SPELL_AURA_PERIODIC_HEAL);
        for(Unit::AuraList::const_iterator i = RejorRegr.begin(); i != RejorRegr.end(); ++i)
            if ((*i)->GetSpellProto()->SpellFamilyName == SPELLFAMILY_DRUID &&
                (*i)->GetCasterGUID() == GetGUID())
                ++ownHotCount;

        if (ownHotCount)
        {
            TakenTotalMod *= 1.2f;                          // base bonus at HoTs

            if (Aura* glyph = GetAura(62971, EFFECT_INDEX_0))// Glyph of Nourish
                TakenTotalMod *= (glyph->GetModifier()->m_amount * ownHotCount + 100.0f) / 100.0f;
        }
    }


    AuraList const& mHealingGet= pVictim->GetAurasByType(SPELL_AURA_MOD_HEALING_RECEIVED);
    for(AuraList::const_iterator i = mHealingGet.begin(); i != mHealingGet.end(); ++i)
        if ((*i)->isAffectedOnSpell(spellProto))
            TakenTotalMod *= ((*i)->GetModifier()->m_amount + 100.0f) / 100.0f;

    heal = (heal + TakenTotal) * TakenTotalMod;

    return heal < 0 ? 0 : uint32(heal);
}

int32 Unit::SpellBaseHealingBonus(SpellSchoolMask schoolMask)
{
    int32 AdvertisedBenefit = 0;

    AuraList const& mHealingDone = GetAurasByType(SPELL_AURA_MOD_HEALING_DONE);
    for(AuraList::const_iterator i = mHealingDone.begin();i != mHealingDone.end(); ++i)
        if(((*i)->GetModifier()->m_miscvalue & schoolMask) != 0)
            AdvertisedBenefit += (*i)->GetModifier()->m_amount;

    // Healing bonus of spirit, intellect and strength
    if (GetTypeId() == TYPEID_PLAYER)
    {
        // Base value
        AdvertisedBenefit +=((Player*)this)->GetBaseSpellPowerBonus();

        // Healing bonus from stats
        AuraList const& mHealingDoneOfStatPercent = GetAurasByType(SPELL_AURA_MOD_SPELL_HEALING_OF_STAT_PERCENT);
        for(AuraList::const_iterator i = mHealingDoneOfStatPercent.begin();i != mHealingDoneOfStatPercent.end(); ++i)
        {
            // stat used dependent from misc value (stat index)
            Stats usedStat = Stats((*i)->GetSpellProto()->EffectMiscValue[(*i)->GetEffIndex()]);
            AdvertisedBenefit += int32(GetStat(usedStat) * (*i)->GetModifier()->m_amount / 100.0f);
        }

        // ... and attack power
        AuraList const& mHealingDonebyAP = GetAurasByType(SPELL_AURA_MOD_SPELL_HEALING_OF_ATTACK_POWER);
        for(AuraList::const_iterator i = mHealingDonebyAP.begin();i != mHealingDonebyAP.end(); ++i)
            if ((*i)->GetModifier()->m_miscvalue & schoolMask)
                AdvertisedBenefit += int32(GetTotalAttackPowerValue(BASE_ATTACK) * (*i)->GetModifier()->m_amount / 100.0f);
    }
    return AdvertisedBenefit;
}

int32 Unit::SpellBaseHealingBonusForVictim(SpellSchoolMask schoolMask, Unit *pVictim)
{
    int32 AdvertisedBenefit = 0;
    AuraList const& mDamageTaken = pVictim->GetAurasByType(SPELL_AURA_MOD_HEALING);
    for(AuraList::const_iterator i = mDamageTaken.begin();i != mDamageTaken.end(); ++i)
        if ((*i)->GetModifier()->m_miscvalue & schoolMask)
            AdvertisedBenefit += (*i)->GetModifier()->m_amount;

    return AdvertisedBenefit;
}

bool Unit::IsImmunedToDamage(SpellSchoolMask shoolMask)
{
    //If m_immuneToSchool type contain this school type, IMMUNE damage.
    SpellImmuneList const& schoolList = m_spellImmune[IMMUNITY_SCHOOL];
    for (SpellImmuneList::const_iterator itr = schoolList.begin(); itr != schoolList.end(); ++itr)
        if (itr->type & shoolMask)
            return true;

    //If m_immuneToDamage type contain magic, IMMUNE damage.
    SpellImmuneList const& damageList = m_spellImmune[IMMUNITY_DAMAGE];
    for (SpellImmuneList::const_iterator itr = damageList.begin(); itr != damageList.end(); ++itr)
        if (itr->type & shoolMask)
            return true;

    return false;
}

bool Unit::IsImmunedToSpell(SpellEntry const* spellInfo)
{
    if (!spellInfo)
        return false;

    //TODO add spellEffect immunity checks!, player with flag in bg is imune to imunity buffs from other friendly players!
    //SpellImmuneList const& dispelList = m_spellImmune[IMMUNITY_EFFECT];

    SpellImmuneList const& dispelList = m_spellImmune[IMMUNITY_DISPEL];
    for(SpellImmuneList::const_iterator itr = dispelList.begin(); itr != dispelList.end(); ++itr)
        if (itr->type == spellInfo->Dispel)
            return true;

    if (!(spellInfo->AttributesEx & SPELL_ATTR_EX_UNAFFECTED_BY_SCHOOL_IMMUNE) &&         // unaffected by school immunity
        !(spellInfo->AttributesEx & SPELL_ATTR_EX_DISPEL_AURAS_ON_IMMUNITY))              // can remove immune (by dispell or immune it)
    {
        SpellImmuneList const& schoolList = m_spellImmune[IMMUNITY_SCHOOL];
        for(SpellImmuneList::const_iterator itr = schoolList.begin(); itr != schoolList.end(); ++itr)
            if (!(IsPositiveSpell(itr->spellId) && IsPositiveSpell(spellInfo->Id)) &&
                (itr->type & GetSpellSchoolMask(spellInfo)))
                return true;
    }

    if(uint32 mechanic = spellInfo->Mechanic)
    {
        SpellImmuneList const& mechanicList = m_spellImmune[IMMUNITY_MECHANIC];
        for(SpellImmuneList::const_iterator itr = mechanicList.begin(); itr != mechanicList.end(); ++itr)
            if (itr->type == mechanic)
                return true;

        AuraList const& immuneAuraApply = GetAurasByType(SPELL_AURA_MECHANIC_IMMUNITY_MASK);
        for(AuraList::const_iterator iter = immuneAuraApply.begin(); iter != immuneAuraApply.end(); ++iter)
            if ((*iter)->GetModifier()->m_miscvalue & (1 << (mechanic-1)))
                return true;
    }

    return false;
}

bool Unit::IsImmunedToSpellEffect(SpellEntry const* spellInfo, SpellEffectIndex index) const
{
    //If m_immuneToEffect type contain this effect type, IMMUNE effect.
    uint32 effect = spellInfo->Effect[index];
    SpellImmuneList const& effectList = m_spellImmune[IMMUNITY_EFFECT];
    for (SpellImmuneList::const_iterator itr = effectList.begin(); itr != effectList.end(); ++itr)
        if (itr->type == effect)
            return true;

    if(uint32 mechanic = spellInfo->EffectMechanic[index])
    {
        SpellImmuneList const& mechanicList = m_spellImmune[IMMUNITY_MECHANIC];
        for (SpellImmuneList::const_iterator itr = mechanicList.begin(); itr != mechanicList.end(); ++itr)
            if (itr->type == mechanic)
                return true;

        AuraList const& immuneAuraApply = GetAurasByType(SPELL_AURA_MECHANIC_IMMUNITY_MASK);
        for(AuraList::const_iterator iter = immuneAuraApply.begin(); iter != immuneAuraApply.end(); ++iter)
            if ((*iter)->GetModifier()->m_miscvalue & (1 << (mechanic-1)))
                return true;
    }

    if(uint32 aura = spellInfo->EffectApplyAuraName[index])
    {
        SpellImmuneList const& list = m_spellImmune[IMMUNITY_STATE];
        for(SpellImmuneList::const_iterator itr = list.begin(); itr != list.end(); ++itr)
            if (itr->type == aura)
                return true;

        // Check for immune to application of harmful magical effects
        AuraList const& immuneAuraApply = GetAurasByType(SPELL_AURA_MOD_IMMUNE_AURA_APPLY_SCHOOL);
        for(AuraList::const_iterator iter = immuneAuraApply.begin(); iter != immuneAuraApply.end(); ++iter)
            if (spellInfo->Dispel == DISPEL_MAGIC &&                                      // Magic debuff
                ((*iter)->GetModifier()->m_miscvalue & GetSpellSchoolMask(spellInfo)) &&  // Check school
                !IsPositiveEffect(spellInfo->Id, index))                                  // Harmful
                return true;
    }

    return false;
}

bool Unit::IsDamageToThreatSpell(SpellEntry const * spellInfo) const
{
    if (!spellInfo)
        return false;

    uint32 family = spellInfo->SpellFamilyName;
    uint64 flags = spellInfo->SpellFamilyFlags;

    if ((family == 5 && flags == 256) ||                    //Searing Pain
        (family == 6 && flags == 8192) ||                   //Mind Blast
        (family == 11 && flags == 1048576))                 //Earth Shock
        return true;

    return false;
}

uint32 Unit::MeleeDamageBonus(Unit *pVictim, uint32 pdamage,WeaponAttackType attType, SpellEntry const *spellProto, DamageEffectType damagetype, uint32 stack)
{
    if (!pVictim)
        return pdamage;

    if (pdamage == 0)
        return pdamage;

    // differentiate for weapon damage based spells
    bool isWeaponDamageBasedSpell = !(spellProto && (damagetype == DOT || IsSpellHaveEffect(spellProto, SPELL_EFFECT_SCHOOL_DAMAGE)));
    Item*  pWeapon          = GetTypeId() == TYPEID_PLAYER ? ((Player*)this)->GetWeaponForAttack(attType,true,false) : NULL;
    uint32 creatureTypeMask = pVictim->GetCreatureTypeMask();
    uint32 schoolMask       = spellProto ? spellProto->SchoolMask : GetMeleeDamageSchoolMask();
    uint32 mechanicMask     = spellProto ? GetAllSpellMechanicMask(spellProto) : 0;

    // Shred also have bonus as MECHANIC_BLEED damages
    if (spellProto && spellProto->SpellFamilyName==SPELLFAMILY_DRUID && spellProto->SpellFamilyFlags & UI64LIT(0x00008000))
        mechanicMask |= (1 << (MECHANIC_BLEED-1));


    // FLAT damage bonus auras
    // =======================
    int32 DoneFlat  = 0;
    int32 TakenFlat = 0;
    int32 APbonus   = 0;

    // ..done flat, already included in wepon damage based spells
    if (!isWeaponDamageBasedSpell)
    {
        AuraList const& mModDamageDone = GetAurasByType(SPELL_AURA_MOD_DAMAGE_DONE);
        for(AuraList::const_iterator i = mModDamageDone.begin(); i != mModDamageDone.end(); ++i)
        {
            if ((*i)->GetModifier()->m_miscvalue & schoolMask &&                                    // schoolmask has to fit with the intrinsic spell school
                (*i)->GetModifier()->m_miscvalue & GetMeleeDamageSchoolMask() &&                    // AND schoolmask has to fit with weapon damage school (essential for non-physical spells)
                ((*i)->GetSpellProto()->EquippedItemClass == -1 ||                                  // general, weapon independent
                pWeapon && pWeapon->IsFitToSpellRequirements((*i)->GetSpellProto())))               // OR used weapon fits aura requirements
            {
                DoneFlat += (*i)->GetModifier()->m_amount;
            }
        }

        // Pets just add their bonus damage to their melee damage
        if (GetTypeId() == TYPEID_UNIT && ((Creature*)this)->isPet())
            DoneFlat += ((Pet*)this)->GetBonusDamage();
    }

    // ..done flat (by creature type mask)
    DoneFlat += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_DAMAGE_DONE_CREATURE, creatureTypeMask);

    // ..done flat (base at attack power for marked target and base at attack power for creature type)
    if (attType == RANGED_ATTACK)
    {
        APbonus += pVictim->GetTotalAuraModifier(SPELL_AURA_RANGED_ATTACK_POWER_ATTACKER_BONUS);
        APbonus += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_RANGED_ATTACK_POWER_VERSUS, creatureTypeMask);
        TakenFlat += pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_RANGED_DAMAGE_TAKEN);
    }
    else
    {
        APbonus += pVictim->GetTotalAuraModifier(SPELL_AURA_MELEE_ATTACK_POWER_ATTACKER_BONUS);
        APbonus += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_MELEE_ATTACK_POWER_VERSUS, creatureTypeMask);
        TakenFlat += pVictim->GetTotalAuraModifier(SPELL_AURA_MOD_MELEE_DAMAGE_TAKEN);
    }

    // ..taken flat (by school mask)
    TakenFlat += pVictim->GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_DAMAGE_TAKEN, schoolMask);

    // PERCENT damage auras
    // ====================
    float DonePercent   = 1.0f;
    float TakenPercent  = 1.0f;

    // ..done pct, already included in weapon damage based spells
    if(!isWeaponDamageBasedSpell)
    {
        AuraList const& mModDamagePercentDone = GetAurasByType(SPELL_AURA_MOD_DAMAGE_PERCENT_DONE);
        for(AuraList::const_iterator i = mModDamagePercentDone.begin(); i != mModDamagePercentDone.end(); ++i)
        {
            if ((*i)->GetModifier()->m_miscvalue & schoolMask &&                                    // schoolmask has to fit with the intrinsic spell school
                (*i)->GetModifier()->m_miscvalue & GetMeleeDamageSchoolMask() &&                    // AND schoolmask has to fit with weapon damage school (essential for non-physical spells)
                ((*i)->GetSpellProto()->EquippedItemClass == -1 ||                                  // general, weapon independent
                pWeapon && pWeapon->IsFitToSpellRequirements((*i)->GetSpellProto())))               // OR used weapon fits aura requirements
            {
                DonePercent *= ((*i)->GetModifier()->m_amount+100.0f) / 100.0f;
            }
        }

        if (attType == OFF_ATTACK)
            DonePercent *= GetModifierValue(UNIT_MOD_DAMAGE_OFFHAND, TOTAL_PCT);                    // no school check required
    }

    // ..done pct (by creature type mask)
    DonePercent *= GetTotalAuraMultiplierByMiscMask(SPELL_AURA_MOD_DAMAGE_DONE_VERSUS, creatureTypeMask);

    // ..taken pct (by school mask)
    TakenPercent *= pVictim->GetTotalAuraMultiplierByMiscMask(SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN, schoolMask);

    // ..taken pct (by mechanic mask)
    TakenPercent *= pVictim->GetTotalAuraMultiplierByMiscValueForMask(SPELL_AURA_MOD_MECHANIC_DAMAGE_TAKEN_PERCENT,mechanicMask);

    // ..taken pct (melee/ranged)
    if(attType == RANGED_ATTACK)
        TakenPercent *= pVictim->GetTotalAuraMultiplier(SPELL_AURA_MOD_RANGED_DAMAGE_TAKEN_PCT);
    else
        TakenPercent *= pVictim->GetTotalAuraMultiplier(SPELL_AURA_MOD_MELEE_DAMAGE_TAKEN_PCT);

    // ..taken pct (aoe avoidance)
    if(spellProto && IsAreaOfEffectSpell(spellProto))
        TakenPercent *= pVictim->GetTotalAuraMultiplier(SPELL_AURA_MOD_AOE_DAMAGE_AVOIDANCE);


    // special dummys/class sripts and other effects
    // =============================================
    Unit *owner = GetOwner();
    if (!owner)
        owner = this;

    // ..done (class scripts)
    if(spellProto)
    {
        AuraList const& mOverrideClassScript= owner->GetAurasByType(SPELL_AURA_OVERRIDE_CLASS_SCRIPTS);
        for(AuraList::const_iterator i = mOverrideClassScript.begin(); i != mOverrideClassScript.end(); ++i)
        {
            if (!(*i)->isAffectedOnSpell(spellProto))
                continue;

            switch((*i)->GetModifier()->m_miscvalue)
            {
                // Tundra Stalker
                // Merciless Combat
                case 7277:
                {
                    // Merciless Combat
                    if ((*i)->GetSpellProto()->SpellIconID == 2656)
                    {
                        if(pVictim->HasAuraState(AURA_STATE_HEALTHLESS_35_PERCENT))
                            DonePercent *= (100.0f+(*i)->GetModifier()->m_amount)/100.0f;
                    }
                    else // Tundra Stalker
                    {
                        // Frost Fever (target debuff)
                        if (pVictim->GetAura(SPELL_AURA_MOD_HASTE, SPELLFAMILY_DEATHKNIGHT, UI64LIT(0x0000000000000000), 0x00000002))
                            DonePercent *= ((*i)->GetModifier()->m_amount+100.0f)/100.0f;
                        break;
                    }
                    break;
                }
                case 7293: // Rage of Rivendare
                {
                    if (pVictim->GetAura(SPELL_AURA_PERIODIC_DAMAGE, SPELLFAMILY_DEATHKNIGHT, UI64LIT(0x0200000000000000)))
                        DonePercent *= ((*i)->GetModifier()->m_amount+100.0f)/100.0f;
                    break;
                }
                // Marked for Death
                case 7598:
                case 7599:
                case 7600:
                case 7601:
                case 7602:
                {
                    if (pVictim->GetAura(SPELL_AURA_MOD_STALKED, SPELLFAMILY_HUNTER, UI64LIT(0x0000000000000400)))
                        DonePercent *= ((*i)->GetModifier()->m_amount+100.0f)/100.0f;
                    break;
                }
            }
        }
    }

    // .. taken (dummy auras)
    AuraList const& mDummyAuras = pVictim->GetAurasByType(SPELL_AURA_DUMMY);
    for(AuraList::const_iterator i = mDummyAuras.begin(); i != mDummyAuras.end(); ++i)
    {
        switch((*i)->GetSpellProto()->SpellIconID)
        {
            //Cheat Death
            case 2109:
                if((*i)->GetModifier()->m_miscvalue & SPELL_SCHOOL_MASK_NORMAL)
                {
                    if(pVictim->GetTypeId() != TYPEID_PLAYER)
                        continue;

                    float mod = ((Player*)pVictim)->GetRatingBonusValue(CR_CRIT_TAKEN_MELEE)*(-8.0f);
                    if (mod < float((*i)->GetModifier()->m_amount))
                        mod = float((*i)->GetModifier()->m_amount);

                    TakenPercent *= (mod + 100.0f) / 100.0f;
                }
                break;
        }
    }

    // .. taken (class scripts)
    AuraList const& mclassScritAuras = GetAurasByType(SPELL_AURA_OVERRIDE_CLASS_SCRIPTS);
    for(AuraList::const_iterator i = mclassScritAuras.begin(); i != mclassScritAuras.end(); ++i)
    {
        switch((*i)->GetMiscValue())
        {
            // Dirty Deeds
            case 6427:
            case 6428:
                if(pVictim->HasAuraState(AURA_STATE_HEALTHLESS_35_PERCENT))
                {
                    Aura* eff0 = GetAura((*i)->GetId(), EFFECT_INDEX_0);
                    if (!eff0 || (*i)->GetEffIndex() != EFFECT_INDEX_1)
                    {
                        sLog.outError("Spell structure of DD (%u) changed.",(*i)->GetId());
                        continue;
                    }

                    // effect 0 have expected value but in negative state
                    TakenPercent *= (-eff0->GetModifier()->m_amount + 100.0f) / 100.0f;
                }
                break;
        }
    }


    // final calculation
    // =================

    // scaling of non weapon based spells
    if (!isWeaponDamageBasedSpell)
    {
        float LvlPenalty = CalculateLevelPenalty(spellProto);

        // Check for table values
        if (SpellBonusEntry const* bonus = sSpellMgr.GetSpellBonusData(spellProto->Id))
        {
            float coeff;
            if (damagetype == DOT)
                coeff = bonus->dot_damage * LvlPenalty * stack;
            else
                coeff = bonus->direct_damage * LvlPenalty * stack;

            if (bonus->ap_bonus)
                DoneFlat += bonus->ap_bonus * (GetTotalAttackPowerValue(BASE_ATTACK) + APbonus) * stack;

            DoneFlat  *= coeff;
            TakenFlat *= coeff;
        }
        // Default calculation
        else if (DoneFlat || TakenFlat)
        {
            // Damage over Time spells bonus calculation
            float DotFactor = 1.0f;
            if(damagetype == DOT)
            {
                if(!IsChanneledSpell(spellProto))
                    DotFactor = GetSpellDuration(spellProto) / 15000.0f;
                uint16 DotTicks = GetSpellAuraMaxTicks(spellProto);
                if(DotTicks)
                {
                    DoneFlat  = DoneFlat * int32(stack) / DotTicks;
                    TakenFlat = TakenFlat * int32(stack) / DotTicks;
                }
            }
            // Distribute Damage over multiple effects, reduce by AoE
            uint32 CastingTime = !IsChanneledSpell(spellProto) ? GetSpellCastTime(spellProto) : GetSpellDuration(spellProto);
            CastingTime = GetCastingTimeForBonus( spellProto, damagetype, CastingTime );
            DoneFlat *= (CastingTime / 3500.0f) * DotFactor * LvlPenalty;
            TakenFlat*= (CastingTime / 3500.0f) * DotFactor * LvlPenalty;
        }
    }
    // weapon damage based spells
    else if( APbonus || DoneFlat )
    {
        bool normalized = spellProto ? IsSpellHaveEffect(spellProto, SPELL_EFFECT_NORMALIZED_WEAPON_DMG) : false;
        DoneFlat += int32(APbonus / 14.0f * GetAPMultiplier(attType,normalized));

        // for weapon damage based spells we still have to apply damage done percent mods
        // (that are already included into pdamage) to not-yet included DoneFlat
        // e.g. from doneVersusCreature, apBonusVs...
        UnitMods unitMod;
        switch(attType)
        {
            default:
            case BASE_ATTACK:   unitMod = UNIT_MOD_DAMAGE_MAINHAND; break;
            case OFF_ATTACK:    unitMod = UNIT_MOD_DAMAGE_OFFHAND;  break;
            case RANGED_ATTACK: unitMod = UNIT_MOD_DAMAGE_RANGED;   break;
        }

        DoneFlat *= GetModifierValue(unitMod, TOTAL_PCT);
    }

    float tmpDamage = float(int32(pdamage) + DoneFlat) * DonePercent;

    // apply spellmod to Done damage
    if(spellProto)
    {
        if(Player* modOwner = GetSpellModOwner())
            modOwner->ApplySpellMod(spellProto->Id, damagetype == DOT ? SPELLMOD_DOT : SPELLMOD_DAMAGE, tmpDamage);
    }

    tmpDamage = (tmpDamage + TakenFlat) * TakenPercent;

    // bonus result can be negative
    return tmpDamage > 0 ? uint32(tmpDamage) : 0;
}

void Unit::ApplySpellImmune(uint32 spellId, uint32 op, uint32 type, bool apply)
{
    if (apply)
    {
        for (SpellImmuneList::iterator itr = m_spellImmune[op].begin(), next; itr != m_spellImmune[op].end(); itr = next)
        {
            next = itr; ++next;
            if(itr->type == type)
            {
                m_spellImmune[op].erase(itr);
                next = m_spellImmune[op].begin();
            }
        }
        SpellImmune Immune;
        Immune.spellId = spellId;
        Immune.type = type;
        m_spellImmune[op].push_back(Immune);
    }
    else
    {
        for (SpellImmuneList::iterator itr = m_spellImmune[op].begin(); itr != m_spellImmune[op].end(); ++itr)
        {
            if(itr->spellId == spellId)
            {
                m_spellImmune[op].erase(itr);
                break;
            }
        }
    }

}

void Unit::ApplySpellDispelImmunity(const SpellEntry * spellProto, DispelType type, bool apply)
{
    ApplySpellImmune(spellProto->Id,IMMUNITY_DISPEL, type, apply);

    if (apply && spellProto->AttributesEx & SPELL_ATTR_EX_DISPEL_AURAS_ON_IMMUNITY)
        RemoveAurasWithDispelType(type);
}

float Unit::GetWeaponProcChance() const
{
    // normalized proc chance for weapon attack speed
    // (odd formula...)
    if (isAttackReady(BASE_ATTACK))
        return (GetAttackTime(BASE_ATTACK) * 1.8f / 1000.0f);
    else if (haveOffhandWeapon() && isAttackReady(OFF_ATTACK))
        return (GetAttackTime(OFF_ATTACK) * 1.6f / 1000.0f);

    return 0.0f;
}

float Unit::GetPPMProcChance(uint32 WeaponSpeed, float PPM) const
{
    // proc per minute chance calculation
    if (PPM <= 0.0f)
        return 0.0f;
    return WeaponSpeed * PPM / 600.0f;                      // result is chance in percents (probability = Speed_in_sec * (PPM / 60))
}

void Unit::Mount(uint32 mount, uint32 spellId)
{
    if (!mount)
        return;

    RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_MOUNTING);

    SetUInt32Value(UNIT_FIELD_MOUNTDISPLAYID, mount);

    SetFlag( UNIT_FIELD_FLAGS, UNIT_FLAG_MOUNT );

    if (GetTypeId() == TYPEID_PLAYER)
    {
        // Called by Taxi system / GM command
        if (!spellId)
            ((Player*)this)->UnsummonPetTemporaryIfAny();
        // Called by mount aura
        else if (SpellEntry const* spellInfo = sSpellStore.LookupEntry(spellId))
        {
            // Flying case (Unsummon any pet)
            if (IsSpellHaveAura(spellInfo, SPELL_AURA_MOD_INCREASE_FLIGHT_SPEED))
                ((Player*)this)->UnsummonPetTemporaryIfAny();
            // Normal case (Unsummon only permanent pet)
            else if (Pet* pet = GetPet())
            {
                if (pet->IsPermanentPetFor((Player*)this) && !((Player*)this)->InArena())
                    ((Player*)this)->UnsummonPetTemporaryIfAny();
                else
                    pet->ApplyModeFlags(PET_MODE_DISABLE_ACTIONS,true);
            }
        }
    }
}

void Unit::Unmount()
{
    if (!IsMounted())
        return;

    RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_NOT_MOUNTED);

    SetUInt32Value(UNIT_FIELD_MOUNTDISPLAYID, 0);
    RemoveFlag( UNIT_FIELD_FLAGS, UNIT_FLAG_MOUNT );

    // only resummon old pet if the player is already added to a map
    // this prevents adding a pet to a not created map which would otherwise cause a crash
    // (it could probably happen when logging in after a previous crash)
    if(GetTypeId() == TYPEID_PLAYER)
    {
        if(Pet* pet = GetPet())
            pet->ApplyModeFlags(PET_MODE_DISABLE_ACTIONS,false);
        else
            ((Player*)this)->ResummonPetTemporaryUnSummonedIfAny();
    }
}

void Unit::SetInCombatWith(Unit* enemy)
{
    Unit* eOwner = enemy->GetCharmerOrOwnerOrSelf();
    if (eOwner->IsPvP())
    {
        SetInCombatState(true,enemy);
        return;
    }

    //check for duel
    if (eOwner->GetTypeId() == TYPEID_PLAYER && ((Player*)eOwner)->duel)
    {
        Unit const* myOwner = GetCharmerOrOwnerOrSelf();
        if(((Player const*)eOwner)->duel->opponent == myOwner)
        {
            SetInCombatState(true,enemy);
            return;
        }
    }

    SetInCombatState(false,enemy);
}

void Unit::SetInCombatState(bool PvP, Unit* enemy)
{
    // only alive units can be in combat
    if (!isAlive())
        return;

    if (PvP)
        m_CombatTimer = 5000;

    bool creatureNotInCombat = GetTypeId()==TYPEID_UNIT && !HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT);

    SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT);

    if (isCharmed() || (GetTypeId()!=TYPEID_PLAYER && ((Creature*)this)->isPet()))
        SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PET_IN_COMBAT);

    if (creatureNotInCombat)
    {
        RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_OOC_NOT_ATTACKABLE);

        if (((Creature*)this)->AI())
            ((Creature*)this)->AI()->EnterCombat(enemy);
    }
}

void Unit::ClearInCombat()
{
    m_CombatTimer = 0;
    RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT);

    if(isCharmed() || (GetTypeId()!=TYPEID_PLAYER && ((Creature*)this)->isPet()))
        RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PET_IN_COMBAT);

    // Player's state will be cleared in Player::UpdateContestedPvP
    if (GetTypeId() != TYPEID_PLAYER)
    {
        Creature* creature = (Creature*)this;
        if (creature->GetCreatureInfo() && creature->GetCreatureInfo()->unit_flags & UNIT_FLAG_OOC_NOT_ATTACKABLE)
            SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_OOC_NOT_ATTACKABLE);

        clearUnitState(UNIT_STAT_ATTACK_PLAYER);
    }
    else
        ((Player*)this)->UpdatePotionCooldown();
}

bool Unit::isTargetableForAttack(bool inverseAlive /*=false*/) const
{
    if (GetTypeId()==TYPEID_PLAYER && ((Player *)this)->isGameMaster())
        return false;

    if (HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE))
        return false;

    // to be removed if unit by any reason enter combat
    if (HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_OOC_NOT_ATTACKABLE))
        return false;

    // inversealive is needed for some spells which need to be casted at dead targets (aoe)
    if (isAlive() == inverseAlive)
        return false;

    return IsInWorld() && !hasUnitState(UNIT_STAT_DIED) && !isInFlight();
}

int32 Unit::ModifyHealth(int32 dVal)
{
    int32 gain = 0;

    if(dVal==0)
        return 0;

    int32 curHealth = (int32)GetHealth();

    int32 val = dVal + curHealth;
    if(val <= 0)
    {
        SetHealth(0);
        return -curHealth;
    }

    int32 maxHealth = (int32)GetMaxHealth();

    if(val < maxHealth)
    {
        SetHealth(val);
        gain = val - curHealth;
    }
    else if(curHealth != maxHealth)
    {
        SetHealth(maxHealth);
        gain = maxHealth - curHealth;
    }

    return gain;
}

int32 Unit::ModifyPower(Powers power, int32 dVal)
{
    int32 gain = 0;

    if(dVal==0)
        return 0;

    int32 curPower = (int32)GetPower(power);

    int32 val = dVal + curPower;
    if(val <= 0)
    {
        SetPower(power,0);
        return -curPower;
    }

    int32 maxPower = (int32)GetMaxPower(power);

    if(val < maxPower)
    {
        SetPower(power,val);
        gain = val - curPower;
    }
    else if(curPower != maxPower)
    {
        SetPower(power,maxPower);
        gain = maxPower - curPower;
    }

    return gain;
}

bool Unit::isVisibleForOrDetect(Unit const* u, WorldObject const* viewPoint, bool detect, bool inVisibleList, bool is3dDistance) const
{
    if(!u || !IsInMap(u))
        return false;

    // Always can see self
    if (u==this)
        return true;

    // player visible for other player if not logout and at same transport
    // including case when player is out of world
    bool at_same_transport =
        GetTypeId() == TYPEID_PLAYER &&  u->GetTypeId()==TYPEID_PLAYER &&
        !((Player*)this)->GetSession()->PlayerLogout() && !((Player*)u)->GetSession()->PlayerLogout() &&
        !((Player*)this)->GetSession()->PlayerLoading() && !((Player*)u)->GetSession()->PlayerLoading() &&
        ((Player*)this)->GetTransport() && ((Player*)this)->GetTransport() == ((Player*)u)->GetTransport();

    // not in world
    if(!at_same_transport && (!IsInWorld() || !u->IsInWorld()))
        return false;

    // forbidden to seen (at GM respawn command)
    if(m_Visibility==VISIBILITY_RESPAWN)
        return false;

    Map& _map = *u->GetMap();
    // Grid dead/alive checks
    if (u->GetTypeId()==TYPEID_PLAYER)
    {
        // non visible at grid for any stealth state
        if(!IsVisibleInGridForPlayer((Player *)u))
            return false;

        // if player is dead then he can't detect anyone in any cases
        if(!u->isAlive())
            detect = false;
    }
    else
    {
        // all dead creatures/players not visible for any creatures
        if(!u->isAlive() || !isAlive())
            return false;
    }

    // always seen by far sight caster
    if (u->GetTypeId()==TYPEID_PLAYER && ((Player*)u)->GetFarSight()==GetGUID())
        return true;

    // different visible distance checks
    if (u->isInFlight())                                    // what see player in flight
    {
        // use object grey distance for all (only see objects any way)
        if (!IsWithinDistInMap(viewPoint,World::GetMaxVisibleDistanceInFlight()+(inVisibleList ? World::GetVisibleObjectGreyDistance() : 0.0f), is3dDistance))
            return false;
    }
    else if(!isAlive())                                     // distance for show body
    {
        if (!IsWithinDistInMap(viewPoint,World::GetMaxVisibleDistanceForObject()+(inVisibleList ? World::GetVisibleObjectGreyDistance() : 0.0f), is3dDistance))
            return false;
    }
    else if(GetTypeId()==TYPEID_PLAYER)                     // distance for show player
    {
        if(u->GetTypeId()==TYPEID_PLAYER)
        {
            // Players far than max visible distance for player or not in our map are not visible too
            if (!at_same_transport && !IsWithinDistInMap(viewPoint, _map.GetVisibilityDistance() + (inVisibleList ? World::GetVisibleUnitGreyDistance() : 0.0f), is3dDistance))
                return false;
        }
        else
        {
            // Units far than max visible distance for creature or not in our map are not visible too
            if (!IsWithinDistInMap(viewPoint, _map.GetVisibilityDistance() + (inVisibleList ? World::GetVisibleUnitGreyDistance() : 0.0f), is3dDistance))
                return false;
        }
    }
    else if(GetCharmerOrOwnerGUID())                        // distance for show pet/charmed
    {
        // Pet/charmed far than max visible distance for player or not in our map are not visible too
        if (!IsWithinDistInMap(viewPoint, _map.GetVisibilityDistance() + (inVisibleList ? World::GetVisibleUnitGreyDistance() : 0.0f), is3dDistance))
            return false;
    }
    else                                                    // distance for show creature
    {
        // Units far than max visible distance for creature or not in our map are not visible too
        if (!IsWithinDistInMap(viewPoint, _map.GetVisibilityDistance() + (inVisibleList ? World::GetVisibleUnitGreyDistance() : 0.0f), is3dDistance))
            return false;
    }

    // always seen by owner
    if (GetCharmerOrOwnerGUID()==u->GetGUID())
        return true;

    // isInvisibleForAlive() those units can only be seen by dead or if other
    // unit is also invisible for alive.. if an isinvisibleforalive unit dies we
    // should be able to see it too
    if (u->isAlive() && isAlive() && isInvisibleForAlive() != u->isInvisibleForAlive())
        if (u->GetTypeId() != TYPEID_PLAYER || !((Player *)u)->isGameMaster())
            return false;

    // Visible units, always are visible for all units, except for units under invisibility and phases
    if (m_Visibility == VISIBILITY_ON && u->m_invisibilityMask==0 && InSamePhase(u))
        return true;

    // GMs see any players, not higher GMs and all units in any phase
    if (u->GetTypeId() == TYPEID_PLAYER && ((Player *)u)->isGameMaster())
    {
        if(GetTypeId() == TYPEID_PLAYER)
            return ((Player *)this)->GetSession()->GetSecurity() <= ((Player *)u)->GetSession()->GetSecurity();
        else
            return true;
    }

    // non faction visibility non-breakable for non-GMs
    if (m_Visibility == VISIBILITY_OFF)
        return false;

    // phased visibility (both must phased in same way)
    if(!InSamePhase(u))
        return false;

    // raw invisibility
    bool invisible = (m_invisibilityMask != 0 || u->m_invisibilityMask !=0);

    // detectable invisibility case
    if( invisible && (
        // Invisible units, always are visible for units under same invisibility type
        (m_invisibilityMask & u->m_invisibilityMask)!=0 ||
        // Invisible units, always are visible for unit that can detect this invisibility (have appropriate level for detect)
        u->canDetectInvisibilityOf(this) ||
        // Units that can detect invisibility always are visible for units that can be detected
        canDetectInvisibilityOf(u) ))
    {
        invisible = false;
    }

    // special cases for always overwrite invisibility/stealth
    if(invisible || m_Visibility == VISIBILITY_GROUP_STEALTH)
    {
        // non-hostile case
        if (!u->IsHostileTo(this))
        {
            // player see other player with stealth/invisibility only if he in same group or raid or same team (raid/team case dependent from conf setting)
            if(GetTypeId()==TYPEID_PLAYER && u->GetTypeId()==TYPEID_PLAYER)
            {
                if(((Player*)this)->IsGroupVisibleFor(((Player*)u)))
                    return true;

                // else apply same rules as for hostile case (detecting check for stealth)
            }
        }
        // hostile case
        else
        {
            // Hunter mark functionality
            AuraList const& auras = GetAurasByType(SPELL_AURA_MOD_STALKED);
            for(AuraList::const_iterator iter = auras.begin(); iter != auras.end(); ++iter)
                if((*iter)->GetCasterGUID()==u->GetGUID())
                    return true;

            // else apply detecting check for stealth
        }

        // none other cases for detect invisibility, so invisible
        if(invisible)
            return false;

        // else apply stealth detecting check
    }

    // unit got in stealth in this moment and must ignore old detected state
    if (m_Visibility == VISIBILITY_GROUP_NO_DETECT)
        return false;

    // GM invisibility checks early, invisibility if any detectable, so if not stealth then visible
    if (m_Visibility != VISIBILITY_GROUP_STEALTH)
        return true;

    // NOW ONLY STEALTH CASE

    //if in non-detect mode then invisible for unit
    //mobs always detect players (detect == true)... return 'false' for those mobs which have (detect == false)
    //players detect players only in Player::HandleStealthedUnitsDetection()
    if (!detect)
        return (u->GetTypeId() == TYPEID_PLAYER) ? ((Player*)u)->HaveAtClient(this) : false;

    // Special cases

    // If is attacked then stealth is lost, some creature can use stealth too
    if( !getAttackers().empty() )
        return true;

    // If there is collision rogue is seen regardless of level difference
    if (IsWithinDist(u,0.24f))
        return true;

    //If a mob or player is stunned he will not be able to detect stealth
    if (u->hasUnitState(UNIT_STAT_STUNNED) && (u != this))
        return false;

    // set max ditance
    float visibleDistance = (u->GetTypeId() == TYPEID_PLAYER) ? MAX_PLAYER_STEALTH_DETECT_RANGE : ((Creature const*)u)->GetAttackDistance(this);

    //Always invisible from back (when stealth detection is on), also filter max distance cases
    bool isInFront = viewPoint->isInFrontInMap(this, visibleDistance);
    if(!isInFront)
        return false;

    // if doesn't have stealth detection (Shadow Sight), then check how stealthy the unit is, otherwise just check los
    if(!u->HasAuraType(SPELL_AURA_DETECT_STEALTH))
    {
        //Calculation if target is in front

        //Visible distance based on stealth value (stealth rank 4 300MOD, 10.5 - 3 = 7.5)
        visibleDistance = 10.5f - (GetTotalAuraModifier(SPELL_AURA_MOD_STEALTH)/100.0f);

        //Visible distance is modified by
        //-Level Diff (every level diff = 1.0f in visible distance)
        visibleDistance += int32(u->getLevelForTarget(this)) - int32(getLevelForTarget(u));

        //This allows to check talent tree and will add addition stealth dependent on used points)
        int32 stealthMod = GetTotalAuraModifier(SPELL_AURA_MOD_STEALTH_LEVEL);
        if(stealthMod < 0)
            stealthMod = 0;

        //-Stealth Mod(positive like Master of Deception) and Stealth Detection(negative like paranoia)
        //based on wowwiki every 5 mod we have 1 more level diff in calculation
        visibleDistance += (int32(u->GetTotalAuraModifier(SPELL_AURA_MOD_STEALTH_DETECT)) - stealthMod)/5.0f;
        visibleDistance = visibleDistance > MAX_PLAYER_STEALTH_DETECT_RANGE ? MAX_PLAYER_STEALTH_DETECT_RANGE : visibleDistance;

        // recheck new distance
        if(visibleDistance <= 0 || !IsWithinDist(viewPoint,visibleDistance))
            return false;
    }

    // Now check is target visible with LoS
    float ox,oy,oz;
    viewPoint->GetPosition(ox,oy,oz);
    return IsWithinLOS(ox,oy,oz);
}

void Unit::SetVisibility(UnitVisibility x)
{
    m_Visibility = x;

    if(IsInWorld())
    {
        Map *m = GetMap();

        if(GetTypeId()==TYPEID_PLAYER)
            m->PlayerRelocation((Player*)this,GetPositionX(),GetPositionY(),GetPositionZ(),GetOrientation());
        else
            m->CreatureRelocation((Creature*)this,GetPositionX(),GetPositionY(),GetPositionZ(),GetOrientation());
    }
}

bool Unit::canDetectInvisibilityOf(Unit const* u) const
{
    if(uint32 mask = (m_detectInvisibilityMask & u->m_invisibilityMask))
    {
        for(uint32 i = 0; i < 10; ++i)
        {
            if(((1 << i) & mask)==0)
                continue;

            // find invisibility level
            int32 invLevel = 0;
            Unit::AuraList const& iAuras = u->GetAurasByType(SPELL_AURA_MOD_INVISIBILITY);
            for(Unit::AuraList::const_iterator itr = iAuras.begin(); itr != iAuras.end(); ++itr)
                if(((*itr)->GetModifier()->m_miscvalue)==i && invLevel < (*itr)->GetModifier()->m_amount)
                    invLevel = (*itr)->GetModifier()->m_amount;

            // find invisibility detect level
            int32 detectLevel = 0;
            Unit::AuraList const& dAuras = GetAurasByType(SPELL_AURA_MOD_INVISIBILITY_DETECTION);
            for(Unit::AuraList::const_iterator itr = dAuras.begin(); itr != dAuras.end(); ++itr)
                if(((*itr)->GetModifier()->m_miscvalue)==i && detectLevel < (*itr)->GetModifier()->m_amount)
                    detectLevel = (*itr)->GetModifier()->m_amount;

            if(i==6 && GetTypeId()==TYPEID_PLAYER)          // special drunk detection case
            {
                detectLevel = ((Player*)this)->GetDrunkValue();
            }

            if(invLevel <= detectLevel)
                return true;
        }
    }

    return false;
}

struct UpdateWalkModeHelper
{
    explicit UpdateWalkModeHelper(Unit* _source) : source(_source) {}
    void operator()(Unit* unit) const { unit->UpdateWalkMode(source, true); }
    Unit* source;
};

void Unit::UpdateWalkMode(Unit* source, bool self)
{
    if (GetTypeId() == TYPEID_PLAYER)
        ((Player*)this)->CallForAllControlledUnits(UpdateWalkModeHelper(source), false, true, true, true);
    else if (self)
    {
        bool on = source->GetTypeId() == TYPEID_PLAYER
            ? ((Player*)source)->HasMovementFlag(MOVEFLAG_WALK_MODE)
            : ((Creature*)source)->HasSplineFlag(SPLINEFLAG_WALKMODE);

        if (on)
        {
            if (((Creature*)this)->isPet() && hasUnitState(UNIT_STAT_FOLLOW))
                ((Creature*)this)->AddSplineFlag(SPLINEFLAG_WALKMODE);
        }
        else
        {
            if (((Creature*)this)->isPet())
                ((Creature*)this)->RemoveSplineFlag(SPLINEFLAG_WALKMODE);
        }
    }
    else
        CallForAllControlledUnits(UpdateWalkModeHelper(source), false, true, true);
}

void Unit::UpdateSpeed(UnitMoveType mtype, bool forced)
{
    // not in combat pet have same speed as owner
    switch(mtype)
    {
        case MOVE_RUN:
        case MOVE_WALK:
        case MOVE_SWIM:
            if (GetTypeId() == TYPEID_UNIT && ((Creature*)this)->isPet() && hasUnitState(UNIT_STAT_FOLLOW))
            {
                if(Unit* owner = GetOwner())
                {
                    SetSpeedRate(mtype, owner->GetSpeedRate(mtype), forced);
                    return;
                }
            }
            break;
    }

    int32 main_speed_mod  = 0;
    float stack_bonus     = 1.0f;
    float non_stack_bonus = 1.0f;

    switch(mtype)
    {
        case MOVE_WALK:
            return;
        case MOVE_RUN:
        {
            if (IsMounted()) // Use on mount auras
            {
                main_speed_mod  = GetMaxPositiveAuraModifier(SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED);
                stack_bonus     = GetTotalAuraMultiplier(SPELL_AURA_MOD_MOUNTED_SPEED_ALWAYS);
                non_stack_bonus = (100.0f + GetMaxPositiveAuraModifier(SPELL_AURA_MOD_MOUNTED_SPEED_NOT_STACK))/100.0f;
            }
            else
            {
                main_speed_mod  = GetMaxPositiveAuraModifier(SPELL_AURA_MOD_INCREASE_SPEED);
                stack_bonus     = GetTotalAuraMultiplier(SPELL_AURA_MOD_SPEED_ALWAYS);
                non_stack_bonus = (100.0f + GetMaxPositiveAuraModifier(SPELL_AURA_MOD_SPEED_NOT_STACK))/100.0f;
            }
            break;
        }
        case MOVE_RUN_BACK:
            return;
        case MOVE_SWIM:
        {
            main_speed_mod  = GetMaxPositiveAuraModifier(SPELL_AURA_MOD_INCREASE_SWIM_SPEED);
            break;
        }
        case MOVE_SWIM_BACK:
            return;
        case MOVE_FLIGHT:
        {
            if (IsMounted()) // Use on mount auras
                main_speed_mod  = GetMaxPositiveAuraModifier(SPELL_AURA_MOD_INCREASE_FLIGHT_SPEED);
            else             // Use not mount (shapeshift for example) auras (should stack)
                main_speed_mod  = GetTotalAuraModifier(SPELL_AURA_MOD_SPEED_FLIGHT);
            stack_bonus     = GetTotalAuraMultiplier(SPELL_AURA_MOD_FLIGHT_SPEED_ALWAYS);
            non_stack_bonus = (100.0f + GetMaxPositiveAuraModifier(SPELL_AURA_MOD_FLIGHT_SPEED_NOT_STACK))/100.0f;
            break;
        }
        case MOVE_FLIGHT_BACK:
            return;
        default:
            sLog.outError("Unit::UpdateSpeed: Unsupported move type (%d)", mtype);
            return;
    }

    float bonus = non_stack_bonus > stack_bonus ? non_stack_bonus : stack_bonus;
    // now we ready for speed calculation
    float speed  = main_speed_mod ? bonus*(100.0f + main_speed_mod)/100.0f : bonus;

    switch(mtype)
    {
        case MOVE_RUN:
        case MOVE_SWIM:
        case MOVE_FLIGHT:
        {
            // Normalize speed by 191 aura SPELL_AURA_USE_NORMAL_MOVEMENT_SPEED if need
            // TODO: possible affect only on MOVE_RUN
            if(int32 normalization = GetMaxPositiveAuraModifier(SPELL_AURA_USE_NORMAL_MOVEMENT_SPEED))
            {
                // Use speed from aura
                float max_speed = normalization / baseMoveSpeed[mtype];
                if (speed > max_speed)
                    speed = max_speed;
            }
            break;
        }
        default:
            break;
    }

    // for creature case, we check explicit if mob searched for assistance
    if (GetTypeId() == TYPEID_UNIT)
    {
        if (((Creature*)this)->HasSearchedAssistance())
            speed *= 0.66f;                                 // best guessed value, so this will be 33% reduction. Based off initial speed, mob can then "run", "walk fast" or "walk".
    }

    // Apply strongest slow aura mod to speed
    int32 slow = GetMaxNegativeAuraModifier(SPELL_AURA_MOD_DECREASE_SPEED);
    if (slow)
    {
        speed *=(100.0f + slow)/100.0f;
        float min_speed = (float)GetMaxPositiveAuraModifier(SPELL_AURA_MOD_MINIMUM_SPEED) / 100.0f;
        if (speed < min_speed)
            speed = min_speed;
    }
    SetSpeedRate(mtype, speed, forced);
}

float Unit::GetSpeed( UnitMoveType mtype ) const
{
    return m_speed_rate[mtype]*baseMoveSpeed[mtype];
}

struct SetSpeedRateHelper
{
    explicit SetSpeedRateHelper(UnitMoveType _mtype, bool _forced) : mtype(_mtype), forced(_forced) {}
    void operator()(Unit* unit) const { unit->UpdateSpeed(mtype,forced); }
    UnitMoveType mtype;
    bool forced;
};

void Unit::SetSpeedRate(UnitMoveType mtype, float rate, bool forced)
{
    if (rate < 0)
        rate = 0.0f;

    // Update speed only on change
    if (m_speed_rate[mtype] == rate)
        return;

    m_speed_rate[mtype] = rate;

    propagateSpeedChange();

    WorldPacket data;
    if(!forced)
    {
        switch(mtype)
        {
            case MOVE_WALK:
                data.Initialize(MSG_MOVE_SET_WALK_SPEED, 8+4+2+4+4+4+4+4+4+4);
                break;
            case MOVE_RUN:
                data.Initialize(MSG_MOVE_SET_RUN_SPEED, 8+4+2+4+4+4+4+4+4+4);
                break;
            case MOVE_RUN_BACK:
                data.Initialize(MSG_MOVE_SET_RUN_BACK_SPEED, 8+4+2+4+4+4+4+4+4+4);
                break;
            case MOVE_SWIM:
                data.Initialize(MSG_MOVE_SET_SWIM_SPEED, 8+4+2+4+4+4+4+4+4+4);
                break;
            case MOVE_SWIM_BACK:
                data.Initialize(MSG_MOVE_SET_SWIM_BACK_SPEED, 8+4+2+4+4+4+4+4+4+4);
                break;
            case MOVE_TURN_RATE:
                data.Initialize(MSG_MOVE_SET_TURN_RATE, 8+4+2+4+4+4+4+4+4+4);
                break;
            case MOVE_FLIGHT:
                data.Initialize(MSG_MOVE_SET_FLIGHT_SPEED, 8+4+2+4+4+4+4+4+4+4);
                break;
            case MOVE_FLIGHT_BACK:
                data.Initialize(MSG_MOVE_SET_FLIGHT_BACK_SPEED, 8+4+2+4+4+4+4+4+4+4);
                break;
            case MOVE_PITCH_RATE:
                data.Initialize(MSG_MOVE_SET_PITCH_RATE, 8+4+2+4+4+4+4+4+4+4);
                break;
            default:
                sLog.outError("Unit::SetSpeedRate: Unsupported move type (%d), data not sent to client.",mtype);
                return;
        }

        data.append(GetPackGUID());
        data << uint32(0);                                  // movement flags
        data << uint16(0);                                  // unk flags
        data << uint32(getMSTime());
        data << float(GetPositionX());
        data << float(GetPositionY());
        data << float(GetPositionZ());
        data << float(GetOrientation());
        data << uint32(0);                                  // fall time
        data << float(GetSpeed(mtype));
        SendMessageToSet( &data, true );
    }
    else
    {
        if(GetTypeId() == TYPEID_PLAYER)
        {
            // register forced speed changes for WorldSession::HandleForceSpeedChangeAck
            // and do it only for real sent packets and use run for run/mounted as client expected
            ++((Player*)this)->m_forced_speed_changes[mtype];
        }

        switch(mtype)
        {
            case MOVE_WALK:
                data.Initialize(SMSG_FORCE_WALK_SPEED_CHANGE, 16);
                break;
            case MOVE_RUN:
                data.Initialize(SMSG_FORCE_RUN_SPEED_CHANGE, 17);
                break;
            case MOVE_RUN_BACK:
                data.Initialize(SMSG_FORCE_RUN_BACK_SPEED_CHANGE, 16);
                break;
            case MOVE_SWIM:
                data.Initialize(SMSG_FORCE_SWIM_SPEED_CHANGE, 16);
                break;
            case MOVE_SWIM_BACK:
                data.Initialize(SMSG_FORCE_SWIM_BACK_SPEED_CHANGE, 16);
                break;
            case MOVE_TURN_RATE:
                data.Initialize(SMSG_FORCE_TURN_RATE_CHANGE, 16);
                break;
            case MOVE_FLIGHT:
                data.Initialize(SMSG_FORCE_FLIGHT_SPEED_CHANGE, 16);
                break;
            case MOVE_FLIGHT_BACK:
                data.Initialize(SMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE, 16);
                break;
            case MOVE_PITCH_RATE:
                data.Initialize(SMSG_FORCE_PITCH_RATE_CHANGE, 16);
                break;
            default:
                sLog.outError("Unit::SetSpeedRate: Unsupported move type (%d), data not sent to client.",mtype);
                return;
        }
        data.append(GetPackGUID());
        data << (uint32)0;                                  // moveEvent, NUM_PMOVE_EVTS = 0x39
        if (mtype == MOVE_RUN)
            data << uint8(0);                               // new 2.1.0
        data << float(GetSpeed(mtype));
        SendMessageToSet( &data, true );
    }

    if (GetTypeId() == TYPEID_PLAYER)                       // need include minpet
        ((Player*)this)->CallForAllControlledUnits(SetSpeedRateHelper(mtype,forced),false,true,true,true);
    else
        CallForAllControlledUnits(SetSpeedRateHelper(mtype,forced),false,true,true);
}

void Unit::SetHover(bool on)
{
    if(on)
        CastSpell(this, 11010, true);
    else
        RemoveAurasDueToSpell(11010);
}

void Unit::setDeathState(DeathState s)
{
    if (s != ALIVE && s!= JUST_ALIVED)
    {
        CombatStop();
        DeleteThreatList();
        ClearComboPointHolders();                           // any combo points pointed to unit lost at it death

        if(IsNonMeleeSpellCasted(false))
            InterruptNonMeleeSpells(false);
    }

    if (s == JUST_DIED)
    {
        RemoveAllAurasOnDeath();
        RemoveGuardians();
        UnsummonAllTotems();

        // after removing a Fearaura (in RemoveAllAurasOnDeath)
        // Unit::SetFeared is called and makes that creatures attack player again
        StopMoving();

        ModifyAuraState(AURA_STATE_HEALTHLESS_20_PERCENT, false);
        ModifyAuraState(AURA_STATE_HEALTHLESS_35_PERCENT, false);
        // remove aurastates allowing special moves
        ClearAllReactives();
        ClearDiminishings();
    }
    else if(s == JUST_ALIVED)
    {
        RemoveFlag (UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE); // clear skinnable for creature and player (at battleground)
    }

    if (m_deathState != ALIVE && s == ALIVE)
    {
        //_ApplyAllAuraMods();
    }
    m_deathState = s;
}

/*########################################
########                          ########
########       AGGRO SYSTEM       ########
########                          ########
########################################*/
bool Unit::CanHaveThreatList() const
{
    // only creatures can have threat list
    if( GetTypeId() != TYPEID_UNIT )
        return false;

    // only alive units can have threat list
    if( !isAlive() )
        return false;

    // totems can not have threat list
    if( ((Creature*)this)->isTotem() )
        return false;

    // vehicles can not have threat list
    if( ((Creature*)this)->isVehicle() )
        return false;

    // pets can not have a threat list, unless they are controlled by a creature
    if( ((Creature*)this)->isPet() && IS_PLAYER_GUID(((Pet*)this)->GetOwnerGUID()) )
        return false;

    return true;
}

//======================================================================

float Unit::ApplyTotalThreatModifier(float threat, SpellSchoolMask schoolMask)
{
    if (!HasAuraType(SPELL_AURA_MOD_THREAT))
        return threat;

    if (schoolMask == SPELL_SCHOOL_MASK_NONE)
        return threat;

    SpellSchools school = GetFirstSchoolInMask(schoolMask);

    return threat * m_threatModifier[school];
}

//======================================================================

void Unit::AddThreat(Unit* pVictim, float threat /*= 0.0f*/, bool crit /*= false*/, SpellSchoolMask schoolMask /*= SPELL_SCHOOL_MASK_NONE*/, SpellEntry const *threatSpell /*= NULL*/)
{
    // Only mobs can manage threat lists
    if(CanHaveThreatList())
        m_ThreatManager.addThreat(pVictim, threat, crit, schoolMask, threatSpell);
}

//======================================================================

void Unit::DeleteThreatList()
{
    if(CanHaveThreatList() && !m_ThreatManager.isThreatListEmpty())
        SendThreatClear();
    m_ThreatManager.clearReferences();
}

//======================================================================

void Unit::TauntApply(Unit* taunter)
{
    assert(GetTypeId()== TYPEID_UNIT);

    if(!taunter || (taunter->GetTypeId() == TYPEID_PLAYER && ((Player*)taunter)->isGameMaster()))
        return;

    if(!CanHaveThreatList())
        return;

    Unit *target = getVictim();
    if(target && target == taunter)
        return;

    SetInFront(taunter);
    if (((Creature*)this)->AI())
        ((Creature*)this)->AI()->AttackStart(taunter);

    m_ThreatManager.tauntApply(taunter);
}

//======================================================================

void Unit::TauntFadeOut(Unit *taunter)
{
    assert(GetTypeId()== TYPEID_UNIT);

    if(!taunter || (taunter->GetTypeId() == TYPEID_PLAYER && ((Player*)taunter)->isGameMaster()))
        return;

    if(!CanHaveThreatList())
        return;

    Unit *target = getVictim();
    if(!target || target != taunter)
        return;

    if(m_ThreatManager.isThreatListEmpty())
    {
        if(((Creature*)this)->AI())
            ((Creature*)this)->AI()->EnterEvadeMode();
        return;
    }

    m_ThreatManager.tauntFadeOut(taunter);
    target = m_ThreatManager.getHostileTarget();

    if (target && target != taunter)
    {
        SetInFront(target);
        if (((Creature*)this)->AI())
            ((Creature*)this)->AI()->AttackStart(target);
    }
}

//======================================================================

bool Unit::SelectHostileTarget()
{
    //function provides main threat functionality
    //next-victim-selection algorithm and evade mode are called
    //threat list sorting etc.

    assert(GetTypeId()== TYPEID_UNIT);

    if (!this->isAlive())
        return false;
    //This function only useful once AI has been initialized
    if (!((Creature*)this)->AI())
        return false;

    Unit* target = NULL;

    // First checking if we have some taunt on us
    const AuraList& tauntAuras = GetAurasByType(SPELL_AURA_MOD_TAUNT);
    if ( !tauntAuras.empty() )
    {
        Unit* caster;

        // The last taunt aura caster is alive an we are happy to attack him
        if ( (caster = tauntAuras.back()->GetCaster()) && caster->isAlive() )
            return true;
        else if (tauntAuras.size() > 1)
        {
            // We do not have last taunt aura caster but we have more taunt auras,
            // so find first available target

            // Auras are pushed_back, last caster will be on the end
            AuraList::const_iterator aura = --tauntAuras.end();
            do
            {
                --aura;
                if ( (caster = (*aura)->GetCaster()) &&
                     caster->IsInMap(this) && caster->isTargetableForAttack() && caster->isInAccessablePlaceFor((Creature*)this) )
                {
                    target = caster;
                    break;
                }
            }while (aura != tauntAuras.begin());
        }
    }

    if ( !target && !m_ThreatManager.isThreatListEmpty() )
        // No taunt aura or taunt aura caster is dead standart target selection
        target = m_ThreatManager.getHostileTarget();

    if(target)
    {
        if(!hasUnitState(UNIT_STAT_STUNNED | UNIT_STAT_DIED))
            SetInFront(target);
        ((Creature*)this)->AI()->AttackStart(target);
        return true;
    }

    // no target but something prevent go to evade mode
    if( !isInCombat() || HasAuraType(SPELL_AURA_MOD_TAUNT) )
        return false;

    // last case when creature don't must go to evade mode:
    // it in combat but attacker not make any damage and not enter to aggro radius to have record in threat list
    // for example at owner command to pet attack some far away creature
    // Note: creature not have targeted movement generator but have attacker in this case
    if (GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
    {
        for(AttackerSet::const_iterator itr = m_attackers.begin(); itr != m_attackers.end(); ++itr)
        {
            if ((*itr)->IsInMap(this) && (*itr)->isTargetableForAttack() && (*itr)->isInAccessablePlaceFor((Creature*)this))
                return false;
        }
    }

    // enter in evade mode in other case
    ((Creature*)this)->AI()->EnterEvadeMode();

    return false;
}

//======================================================================
//======================================================================
//======================================================================

int32 Unit::CalculateSpellDamage(SpellEntry const* spellProto, SpellEffectIndex effect_index, int32 effBasePoints, Unit const* target)
{
    Player* unitPlayer = (GetTypeId() == TYPEID_PLAYER) ? (Player*)this : NULL;

    uint8 comboPoints = unitPlayer ? unitPlayer->GetComboPoints() : 0;

    int32 level = int32(getLevel());
    if (level > (int32)spellProto->maxLevel && spellProto->maxLevel > 0)
        level = (int32)spellProto->maxLevel;
    else if (level < (int32)spellProto->baseLevel)
        level = (int32)spellProto->baseLevel;
    level-= (int32)spellProto->spellLevel;

    float basePointsPerLevel = spellProto->EffectRealPointsPerLevel[effect_index];
    float randomPointsPerLevel = spellProto->EffectDicePerLevel[effect_index];
    int32 basePoints = int32(effBasePoints + level * basePointsPerLevel);
    int32 randomPoints = int32(spellProto->EffectDieSides[effect_index] + level * randomPointsPerLevel);
    float comboDamage = spellProto->EffectPointsPerComboPoint[effect_index];

    // range can have possitive and negative values, so order its for irand
    int32 randvalue = int32(spellProto->EffectBaseDice[effect_index]) >= randomPoints
        ? irand(randomPoints, int32(spellProto->EffectBaseDice[effect_index]))
        : irand(int32(spellProto->EffectBaseDice[effect_index]), randomPoints);

    int32 value = basePoints + randvalue;
    //random damage
    if(comboDamage != 0 && unitPlayer && target && (target->GetGUID() == unitPlayer->GetComboTarget()))
        value += (int32)(comboDamage * comboPoints);

    if(Player* modOwner = GetSpellModOwner())
    {
        modOwner->ApplySpellMod(spellProto->Id,SPELLMOD_ALL_EFFECTS, value);
        switch(effect_index)
        {
            case 0:
                modOwner->ApplySpellMod(spellProto->Id,SPELLMOD_EFFECT1, value);
                break;
            case 1:
                modOwner->ApplySpellMod(spellProto->Id,SPELLMOD_EFFECT2, value);
                break;
            case 2:
                modOwner->ApplySpellMod(spellProto->Id,SPELLMOD_EFFECT3, value);
                break;
        }
    }

    if(spellProto->Attributes & SPELL_ATTR_LEVEL_DAMAGE_CALCULATION && spellProto->spellLevel &&
            spellProto->Effect[effect_index] != SPELL_EFFECT_WEAPON_PERCENT_DAMAGE &&
            spellProto->Effect[effect_index] != SPELL_EFFECT_KNOCK_BACK &&
            (spellProto->Effect[effect_index] != SPELL_EFFECT_APPLY_AURA || spellProto->EffectApplyAuraName[effect_index] != SPELL_AURA_MOD_DECREASE_SPEED))
        value = int32(value*0.25f*exp(getLevel()*(70-spellProto->spellLevel)/1000.0f));

    return value;
}

int32 Unit::CalculateSpellDuration(SpellEntry const* spellProto, SpellEffectIndex effect_index, Unit const* target)
{
    Player* unitPlayer = (GetTypeId() == TYPEID_PLAYER) ? (Player*)this : NULL;

    uint8 comboPoints = unitPlayer ? unitPlayer->GetComboPoints() : 0;

    int32 minduration = GetSpellDuration(spellProto);
    int32 maxduration = GetSpellMaxDuration(spellProto);

    int32 duration;

    if( minduration != -1 && minduration != maxduration )
        duration = minduration + int32((maxduration - minduration) * comboPoints / 5);
    else
        duration = minduration;

    if (duration > 0)
    {
        int32 mechanic = GetEffectMechanic(spellProto, effect_index);
        // Find total mod value (negative bonus)
        int32 durationMod_always = target->GetTotalAuraModifierByMiscValue(SPELL_AURA_MECHANIC_DURATION_MOD, mechanic);
        // Modify from SPELL_AURA_MOD_DURATION_OF_EFFECTS_BY_DISPEL aura for negatve effects (stack always ?)
        if (!IsPositiveEffect(spellProto->Id, effect_index))
            durationMod_always+=target->GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_DURATION_OF_EFFECTS_BY_DISPEL, spellProto->Dispel);
        // Find max mod (negative bonus)
        int32 durationMod_not_stack = target->GetMaxNegativeAuraModifierByMiscValue(SPELL_AURA_MECHANIC_DURATION_MOD_NOT_STACK, mechanic);

        if (!IsPositiveSpell(spellProto->Id))
             durationMod_always += target->GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_DURATION_OF_MAGIC_EFFECTS, spellProto->DmgClass);

        int32 durationMod = 0;
        // Select strongest negative mod
        if (durationMod_always > durationMod_not_stack)
            durationMod = durationMod_not_stack;
        else
            durationMod = durationMod_always;

        if (durationMod != 0)
            duration = int32(int64(duration) * (100+durationMod) /100);

        if (duration < 0) duration = 0;
    }

    return duration;
}

DiminishingLevels Unit::GetDiminishing(DiminishingGroup group)
{
    for(Diminishing::iterator i = m_Diminishing.begin(); i != m_Diminishing.end(); ++i)
    {
        if(i->DRGroup != group)
            continue;

        if(!i->hitCount)
            return DIMINISHING_LEVEL_1;

        if(!i->hitTime)
            return DIMINISHING_LEVEL_1;

        // If last spell was casted more than 15 seconds ago - reset the count.
        if(i->stack==0 && getMSTimeDiff(i->hitTime,getMSTime()) > 15000)
        {
            i->hitCount = DIMINISHING_LEVEL_1;
            return DIMINISHING_LEVEL_1;
        }
        // or else increase the count.
        else
        {
            return DiminishingLevels(i->hitCount);
        }
    }
    return DIMINISHING_LEVEL_1;
}

void Unit::IncrDiminishing(DiminishingGroup group)
{
    // Checking for existing in the table
    for(Diminishing::iterator i = m_Diminishing.begin(); i != m_Diminishing.end(); ++i)
    {
        if(i->DRGroup != group)
            continue;
        if(i->hitCount < DIMINISHING_LEVEL_IMMUNE)
            i->hitCount += 1;
        return;
    }
    m_Diminishing.push_back(DiminishingReturn(group,getMSTime(),DIMINISHING_LEVEL_2));
}

void Unit::ApplyDiminishingToDuration(DiminishingGroup group, int32 &duration,Unit* caster,DiminishingLevels Level, int32 limitduration)
{
    if(duration == -1 || group == DIMINISHING_NONE || caster->IsFriendlyTo(this) )
        return;

    // Duration of crowd control abilities on pvp target is limited by 10 sec. (2.2.0)
    if(limitduration > 0 && duration > limitduration)
    {
        // test pet/charm masters instead pets/charmeds
        Unit const* targetOwner = GetCharmerOrOwner();
        Unit const* casterOwner = caster->GetCharmerOrOwner();

        Unit const* target = targetOwner ? targetOwner : this;
        Unit const* source = casterOwner ? casterOwner : caster;

        if(target->GetTypeId() == TYPEID_PLAYER && source->GetTypeId() == TYPEID_PLAYER)
            duration = limitduration;
    }

    float mod = 1.0f;

    // Some diminishings applies to mobs too (for example, Stun)
    if((GetDiminishingReturnsGroupType(group) == DRTYPE_PLAYER && GetTypeId() == TYPEID_PLAYER) || GetDiminishingReturnsGroupType(group) == DRTYPE_ALL)
    {
        DiminishingLevels diminish = Level;
        switch(diminish)
        {
            case DIMINISHING_LEVEL_1: break;
            case DIMINISHING_LEVEL_2: mod = 0.5f; break;
            case DIMINISHING_LEVEL_3: mod = 0.25f; break;
            case DIMINISHING_LEVEL_IMMUNE: mod = 0.0f;break;
            default: break;
        }
    }

    duration = int32(duration * mod);
}

void Unit::ApplyDiminishingAura( DiminishingGroup group, bool apply )
{
    // Checking for existing in the table
    for(Diminishing::iterator i = m_Diminishing.begin(); i != m_Diminishing.end(); ++i)
    {
        if(i->DRGroup != group)
            continue;

        if(apply)
            i->stack += 1;
        else if(i->stack)
        {
            i->stack -= 1;
            // Remember time after last aura from group removed
            if (i->stack == 0)
                i->hitTime = getMSTime();
        }
        break;
    }
}

Unit* Unit::GetUnit(WorldObject const& object, uint64 guid)
{
    return ObjectAccessor::GetUnit(object,guid);
}

bool Unit::isVisibleForInState( Player const* u, WorldObject const* viewPoint, bool inVisibleList ) const
{
    return isVisibleForOrDetect(u, viewPoint, false, inVisibleList, false);
}

/// returns true if creature can't be seen by alive units
bool Unit::isInvisibleForAlive() const
{
    if (m_AuraFlags & UNIT_AURAFLAG_ALIVE_INVISIBLE)
        return true;
    // TODO: maybe spiritservices also have just an aura
    return isSpiritService();
}

uint32 Unit::GetCreatureType() const
{
    if(GetTypeId() == TYPEID_PLAYER)
    {
        SpellShapeshiftEntry const* ssEntry = sSpellShapeshiftStore.LookupEntry(m_form);
        if(ssEntry && ssEntry->creatureType > 0)
            return ssEntry->creatureType;
        else
            return CREATURE_TYPE_HUMANOID;
    }
    else
        return ((Creature*)this)->GetCreatureInfo()->type;
}

/*#######################################
########                         ########
########       STAT SYSTEM       ########
########                         ########
#######################################*/

bool Unit::HandleStatModifier(UnitMods unitMod, UnitModifierType modifierType, float amount, bool apply)
{
    if(unitMod >= UNIT_MOD_END || modifierType >= MODIFIER_TYPE_END)
    {
        sLog.outError("ERROR in HandleStatModifier(): non existed UnitMods or wrong UnitModifierType!");
        return false;
    }

    float val = 1.0f;

    switch(modifierType)
    {
        case BASE_VALUE:
        case TOTAL_VALUE:
            m_auraModifiersGroup[unitMod][modifierType] += apply ? amount : -amount;
            break;
        case BASE_PCT:
        case TOTAL_PCT:
            if(amount <= -100.0f)                           //small hack-fix for -100% modifiers
                amount = -200.0f;

            val = (100.0f + amount) / 100.0f;
            m_auraModifiersGroup[unitMod][modifierType] *= apply ? val : (1.0f/val);
            break;

        default:
            break;
    }

    if(!CanModifyStats())
        return false;

    switch(unitMod)
    {
        case UNIT_MOD_STAT_STRENGTH:
        case UNIT_MOD_STAT_AGILITY:
        case UNIT_MOD_STAT_STAMINA:
        case UNIT_MOD_STAT_INTELLECT:
        case UNIT_MOD_STAT_SPIRIT:         UpdateStats(GetStatByAuraGroup(unitMod));  break;

        case UNIT_MOD_ARMOR:               UpdateArmor();           break;
        case UNIT_MOD_HEALTH:              UpdateMaxHealth();       break;

        case UNIT_MOD_MANA:
        case UNIT_MOD_RAGE:
        case UNIT_MOD_FOCUS:
        case UNIT_MOD_ENERGY:
        case UNIT_MOD_HAPPINESS:
        case UNIT_MOD_RUNE:
        case UNIT_MOD_RUNIC_POWER:          UpdateMaxPower(GetPowerTypeByAuraGroup(unitMod));          break;

        case UNIT_MOD_RESISTANCE_HOLY:
        case UNIT_MOD_RESISTANCE_FIRE:
        case UNIT_MOD_RESISTANCE_NATURE:
        case UNIT_MOD_RESISTANCE_FROST:
        case UNIT_MOD_RESISTANCE_SHADOW:
        case UNIT_MOD_RESISTANCE_ARCANE:   UpdateResistances(GetSpellSchoolByAuraGroup(unitMod));      break;

        case UNIT_MOD_ATTACK_POWER:        UpdateAttackPowerAndDamage();         break;
        case UNIT_MOD_ATTACK_POWER_RANGED: UpdateAttackPowerAndDamage(true);     break;

        case UNIT_MOD_DAMAGE_MAINHAND:     UpdateDamagePhysical(BASE_ATTACK);    break;
        case UNIT_MOD_DAMAGE_OFFHAND:      UpdateDamagePhysical(OFF_ATTACK);     break;
        case UNIT_MOD_DAMAGE_RANGED:       UpdateDamagePhysical(RANGED_ATTACK);  break;

        default:
            break;
    }

    return true;
}

float Unit::GetModifierValue(UnitMods unitMod, UnitModifierType modifierType) const
{
    if( unitMod >= UNIT_MOD_END || modifierType >= MODIFIER_TYPE_END)
    {
        sLog.outError("trial to access non existed modifier value from UnitMods!");
        return 0.0f;
    }

    if(modifierType == TOTAL_PCT && m_auraModifiersGroup[unitMod][modifierType] <= 0.0f)
        return 0.0f;

    return m_auraModifiersGroup[unitMod][modifierType];
}

float Unit::GetTotalStatValue(Stats stat) const
{
    UnitMods unitMod = UnitMods(UNIT_MOD_STAT_START + stat);

    if(m_auraModifiersGroup[unitMod][TOTAL_PCT] <= 0.0f)
        return 0.0f;

    // value = ((base_value * base_pct) + total_value) * total_pct
    float value  = m_auraModifiersGroup[unitMod][BASE_VALUE] + GetCreateStat(stat);
    value *= m_auraModifiersGroup[unitMod][BASE_PCT];
    value += m_auraModifiersGroup[unitMod][TOTAL_VALUE];
    value *= m_auraModifiersGroup[unitMod][TOTAL_PCT];

    return value;
}

float Unit::GetTotalAuraModValue(UnitMods unitMod) const
{
    if(unitMod >= UNIT_MOD_END)
    {
        sLog.outError("trial to access non existed UnitMods in GetTotalAuraModValue()!");
        return 0.0f;
    }

    if(m_auraModifiersGroup[unitMod][TOTAL_PCT] <= 0.0f)
        return 0.0f;

    float value  = m_auraModifiersGroup[unitMod][BASE_VALUE];
    value *= m_auraModifiersGroup[unitMod][BASE_PCT];
    value += m_auraModifiersGroup[unitMod][TOTAL_VALUE];
    value *= m_auraModifiersGroup[unitMod][TOTAL_PCT];

    return value;
}

SpellSchools Unit::GetSpellSchoolByAuraGroup(UnitMods unitMod) const
{
    SpellSchools school = SPELL_SCHOOL_NORMAL;

    switch(unitMod)
    {
        case UNIT_MOD_RESISTANCE_HOLY:     school = SPELL_SCHOOL_HOLY;          break;
        case UNIT_MOD_RESISTANCE_FIRE:     school = SPELL_SCHOOL_FIRE;          break;
        case UNIT_MOD_RESISTANCE_NATURE:   school = SPELL_SCHOOL_NATURE;        break;
        case UNIT_MOD_RESISTANCE_FROST:    school = SPELL_SCHOOL_FROST;         break;
        case UNIT_MOD_RESISTANCE_SHADOW:   school = SPELL_SCHOOL_SHADOW;        break;
        case UNIT_MOD_RESISTANCE_ARCANE:   school = SPELL_SCHOOL_ARCANE;        break;

        default:
            break;
    }

    return school;
}

Stats Unit::GetStatByAuraGroup(UnitMods unitMod) const
{
    Stats stat = STAT_STRENGTH;

    switch(unitMod)
    {
        case UNIT_MOD_STAT_STRENGTH:    stat = STAT_STRENGTH;      break;
        case UNIT_MOD_STAT_AGILITY:     stat = STAT_AGILITY;       break;
        case UNIT_MOD_STAT_STAMINA:     stat = STAT_STAMINA;       break;
        case UNIT_MOD_STAT_INTELLECT:   stat = STAT_INTELLECT;     break;
        case UNIT_MOD_STAT_SPIRIT:      stat = STAT_SPIRIT;        break;

        default:
            break;
    }

    return stat;
}

Powers Unit::GetPowerTypeByAuraGroup(UnitMods unitMod) const
{
    switch(unitMod)
    {
        case UNIT_MOD_MANA:       return POWER_MANA;
        case UNIT_MOD_RAGE:       return POWER_RAGE;
        case UNIT_MOD_FOCUS:      return POWER_FOCUS;
        case UNIT_MOD_ENERGY:     return POWER_ENERGY;
        case UNIT_MOD_HAPPINESS:  return POWER_HAPPINESS;
        case UNIT_MOD_RUNE:       return POWER_RUNE;
        case UNIT_MOD_RUNIC_POWER:return POWER_RUNIC_POWER;
        default:                  return POWER_MANA;
    }

    return POWER_MANA;
}

float Unit::GetTotalAttackPowerValue(WeaponAttackType attType) const
{
    if (attType == RANGED_ATTACK)
    {
        int32 ap = GetInt32Value(UNIT_FIELD_RANGED_ATTACK_POWER) + GetInt32Value(UNIT_FIELD_RANGED_ATTACK_POWER_MODS);
        if (ap < 0)
            return 0.0f;
        return ap * (1.0f + GetFloatValue(UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER));
    }
    else
    {
        int32 ap = GetInt32Value(UNIT_FIELD_ATTACK_POWER) + GetInt32Value(UNIT_FIELD_ATTACK_POWER_MODS);
        if (ap < 0)
            return 0.0f;
        return ap * (1.0f + GetFloatValue(UNIT_FIELD_ATTACK_POWER_MULTIPLIER));
    }
}

float Unit::GetWeaponDamageRange(WeaponAttackType attType ,WeaponDamageRange type) const
{
    if (attType == OFF_ATTACK && !haveOffhandWeapon())
        return 0.0f;

    return m_weaponDamage[attType][type];
}

void Unit::SetLevel(uint32 lvl)
{
    SetUInt32Value(UNIT_FIELD_LEVEL, lvl);

    // group update
    if ((GetTypeId() == TYPEID_PLAYER) && ((Player*)this)->GetGroup())
        ((Player*)this)->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_LEVEL);
}

void Unit::SetHealth(uint32 val)
{
    uint32 maxHealth = GetMaxHealth();
    if(maxHealth < val)
        val = maxHealth;

    SetUInt32Value(UNIT_FIELD_HEALTH, val);

    // group update
    if(GetTypeId() == TYPEID_PLAYER)
    {
        if(((Player*)this)->GetGroup())
            ((Player*)this)->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_CUR_HP);
    }
    else if(((Creature*)this)->isPet())
    {
        Pet *pet = ((Pet*)this);
        if(pet->isControlled())
        {
            Unit *owner = GetOwner();
            if(owner && (owner->GetTypeId() == TYPEID_PLAYER) && ((Player*)owner)->GetGroup())
                ((Player*)owner)->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_CUR_HP);
        }
    }
}

void Unit::SetMaxHealth(uint32 val)
{
    uint32 health = GetHealth();
    SetUInt32Value(UNIT_FIELD_MAXHEALTH, val);

    // group update
    if(GetTypeId() == TYPEID_PLAYER)
    {
        if(((Player*)this)->GetGroup())
            ((Player*)this)->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_MAX_HP);
    }
    else if(((Creature*)this)->isPet())
    {
        Pet *pet = ((Pet*)this);
        if(pet->isControlled())
        {
            Unit *owner = GetOwner();
            if(owner && (owner->GetTypeId() == TYPEID_PLAYER) && ((Player*)owner)->GetGroup())
                ((Player*)owner)->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_MAX_HP);
        }
    }

    if(val < health)
        SetHealth(val);
}

void Unit::SetHealthPercent(float percent)
{
    uint32 newHealth = GetMaxHealth() * percent/100.0f;
    SetHealth(newHealth);
}

void Unit::SetPower(Powers power, uint32 val)
{
    if(GetPower(power) == val)
        return;

    uint32 maxPower = GetMaxPower(power);
    if(maxPower < val)
        val = maxPower;

    SetStatInt32Value(UNIT_FIELD_POWER1 + power, val);

    WorldPacket data(SMSG_POWER_UPDATE);
    data.append(GetPackGUID());
    data << uint8(power);
    data << uint32(val);
    SendMessageToSet(&data, GetTypeId() == TYPEID_PLAYER ? true : false);

    // group update
    if(GetTypeId() == TYPEID_PLAYER)
    {
        if(((Player*)this)->GetGroup())
            ((Player*)this)->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_CUR_POWER);
    }
    else if(((Creature*)this)->isPet())
    {
        Pet *pet = ((Pet*)this);
        if(pet->isControlled())
        {
            Unit *owner = GetOwner();
            if(owner && (owner->GetTypeId() == TYPEID_PLAYER) && ((Player*)owner)->GetGroup())
                ((Player*)owner)->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_CUR_POWER);
        }

        // Update the pet's character sheet with happiness damage bonus
        if(pet->getPetType() == HUNTER_PET && power == POWER_HAPPINESS)
        {
            pet->UpdateDamagePhysical(BASE_ATTACK);
        }
    }
}

void Unit::SetMaxPower(Powers power, uint32 val)
{
    uint32 cur_power = GetPower(power);
    SetStatInt32Value(UNIT_FIELD_MAXPOWER1 + power, val);

    // group update
    if(GetTypeId() == TYPEID_PLAYER)
    {
        if(((Player*)this)->GetGroup())
            ((Player*)this)->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_MAX_POWER);
    }
    else if(((Creature*)this)->isPet())
    {
        Pet *pet = ((Pet*)this);
        if(pet->isControlled())
        {
            Unit *owner = GetOwner();
            if(owner && (owner->GetTypeId() == TYPEID_PLAYER) && ((Player*)owner)->GetGroup())
                ((Player*)owner)->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_MAX_POWER);
        }
    }

    if(val < cur_power)
        SetPower(power, val);
}

void Unit::ApplyPowerMod(Powers power, uint32 val, bool apply)
{
    ApplyModUInt32Value(UNIT_FIELD_POWER1+power, val, apply);

    // group update
    if(GetTypeId() == TYPEID_PLAYER)
    {
        if(((Player*)this)->GetGroup())
            ((Player*)this)->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_CUR_POWER);
    }
    else if(((Creature*)this)->isPet())
    {
        Pet *pet = ((Pet*)this);
        if(pet->isControlled())
        {
            Unit *owner = GetOwner();
            if(owner && (owner->GetTypeId() == TYPEID_PLAYER) && ((Player*)owner)->GetGroup())
                ((Player*)owner)->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_CUR_POWER);
        }
    }
}

void Unit::ApplyMaxPowerMod(Powers power, uint32 val, bool apply)
{
    ApplyModUInt32Value(UNIT_FIELD_MAXPOWER1+power, val, apply);

    // group update
    if(GetTypeId() == TYPEID_PLAYER)
    {
        if(((Player*)this)->GetGroup())
            ((Player*)this)->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_MAX_POWER);
    }
    else if(((Creature*)this)->isPet())
    {
        Pet *pet = ((Pet*)this);
        if(pet->isControlled())
        {
            Unit *owner = GetOwner();
            if(owner && (owner->GetTypeId() == TYPEID_PLAYER) && ((Player*)owner)->GetGroup())
                ((Player*)owner)->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_MAX_POWER);
        }
    }
}

void Unit::ApplyAuraProcTriggerDamage( Aura* aura, bool apply )
{
    AuraList& tAuraProcTriggerDamage = m_modAuras[SPELL_AURA_PROC_TRIGGER_DAMAGE];
    if(apply)
        tAuraProcTriggerDamage.push_back(aura);
    else
        tAuraProcTriggerDamage.remove(aura);
}

uint32 Unit::GetCreatePowers( Powers power ) const
{
    // POWER_FOCUS and POWER_HAPPINESS only have hunter pet
    switch(power)
    {
        case POWER_MANA:      return GetCreateMana();
        case POWER_RAGE:      return 1000;
        case POWER_FOCUS:     return (GetTypeId()==TYPEID_PLAYER || !((Creature const*)this)->isPet() || ((Pet const*)this)->getPetType()!=HUNTER_PET ? 0 : 100);
        case POWER_ENERGY:    return 100;
        case POWER_HAPPINESS: return (GetTypeId()==TYPEID_PLAYER || !((Creature const*)this)->isPet() || ((Pet const*)this)->getPetType()!=HUNTER_PET ? 0 : 1050000);
        case POWER_RUNIC_POWER: return 1000;
        case POWER_RUNE:      return 0;
        case POWER_HEALTH:    return 0;
    }

    return 0;
}

void Unit::AddToWorld()
{
    Object::AddToWorld();
}

void Unit::RemoveFromWorld()
{
    // cleanup
    if (IsInWorld())
    {
        Uncharm();
        RemoveNotOwnSingleTargetAuras();
        RemoveGuardians();
        RemoveAllGameObjects();
        RemoveAllDynObjects();
        CleanupDeletedAuras();
    }

    Object::RemoveFromWorld();
}

void Unit::CleanupsBeforeDelete()
{
    if(m_uint32Values)                                      // only for fully created object
    {
        InterruptNonMeleeSpells(true);
        m_Events.KillAllEvents(false);                      // non-delatable (currently casted spells) will not deleted now but it will deleted at call in Map::RemoveAllObjectsInRemoveList
        CombatStop();
        ClearComboPointHolders();
        DeleteThreatList();
        getHostileRefManager().setOnlineOfflineState(false);
        RemoveAllAuras(AURA_REMOVE_BY_DELETE);
        GetMotionMaster()->Clear(false);                    // remove different non-standard movement generators.
    }
    WorldObject::CleanupsBeforeDelete();
}

CharmInfo* Unit::InitCharmInfo(Unit *charm)
{
    if(!m_charmInfo)
        m_charmInfo = new CharmInfo(charm);
    return m_charmInfo;
}

CharmInfo::CharmInfo(Unit* unit)
: m_unit(unit), m_CommandState(COMMAND_FOLLOW), m_reactState(REACT_PASSIVE), m_petnumber(0)
{
    for(int i = 0; i < CREATURE_MAX_SPELLS; ++i)
        m_charmspells[i].SetActionAndType(0,ACT_DISABLED);
}

void CharmInfo::InitPetActionBar()
{
    // the first 3 SpellOrActions are attack, follow and stay
    for(uint32 i = 0; i < ACTION_BAR_INDEX_PET_SPELL_START - ACTION_BAR_INDEX_START; ++i)
        SetActionBar(ACTION_BAR_INDEX_START + i,COMMAND_ATTACK - i,ACT_COMMAND);

    // middle 4 SpellOrActions are spells/special attacks/abilities
    for(uint32 i = 0; i < ACTION_BAR_INDEX_PET_SPELL_END-ACTION_BAR_INDEX_PET_SPELL_START; ++i)
        SetActionBar(ACTION_BAR_INDEX_PET_SPELL_START + i,0,ACT_DISABLED);

    // last 3 SpellOrActions are reactions
    for(uint32 i = 0; i < ACTION_BAR_INDEX_END - ACTION_BAR_INDEX_PET_SPELL_END; ++i)
        SetActionBar(ACTION_BAR_INDEX_PET_SPELL_END + i,COMMAND_ATTACK - i,ACT_REACTION);
}

void CharmInfo::InitEmptyActionBar()
{
    SetActionBar(ACTION_BAR_INDEX_START,COMMAND_ATTACK,ACT_COMMAND);
    for(uint32 x = ACTION_BAR_INDEX_START+1; x < ACTION_BAR_INDEX_END; ++x)
        SetActionBar(x,0,ACT_PASSIVE);
}

void CharmInfo::InitPossessCreateSpells()
{
    InitEmptyActionBar();                                   //charm action bar

    if(m_unit->GetTypeId() == TYPEID_PLAYER)                //possessed players don't have spells, keep the action bar empty
        return;

    for(uint32 x = 0; x < CREATURE_MAX_SPELLS; ++x)
    {
        if (IsPassiveSpell(((Creature*)m_unit)->m_spells[x]))
            m_unit->CastSpell(m_unit, ((Creature*)m_unit)->m_spells[x], true);
        else
            AddSpellToActionBar(((Creature*)m_unit)->m_spells[x], ACT_PASSIVE);
    }
}

void CharmInfo::InitCharmCreateSpells()
{
    if(m_unit->GetTypeId() == TYPEID_PLAYER)                //charmed players don't have spells
    {
        InitEmptyActionBar();
        return;
    }

    InitPetActionBar();

    for(uint32 x = 0; x < CREATURE_MAX_SPELLS; ++x)
    {
        uint32 spellId = ((Creature*)m_unit)->m_spells[x];

        if(!spellId)
        {
            m_charmspells[x].SetActionAndType(spellId,ACT_DISABLED);
            continue;
        }

        if (IsPassiveSpell(spellId))
        {
            m_unit->CastSpell(m_unit, spellId, true);
            m_charmspells[x].SetActionAndType(spellId,ACT_PASSIVE);
        }
        else
        {
            m_charmspells[x].SetActionAndType(spellId,ACT_DISABLED);

            ActiveStates newstate;
            bool onlyselfcast = true;
            SpellEntry const *spellInfo = sSpellStore.LookupEntry(spellId);

            if(!spellInfo) onlyselfcast = false;
            for(uint32 i = 0;i<3 && onlyselfcast;++i)       //non existent spell will not make any problems as onlyselfcast would be false -> break right away
            {
                if(spellInfo->EffectImplicitTargetA[i] != TARGET_SELF && spellInfo->EffectImplicitTargetA[i] != 0)
                    onlyselfcast = false;
            }

            if(onlyselfcast || !IsPositiveSpell(spellId))   //only self cast and spells versus enemies are autocastable
                newstate = ACT_DISABLED;
            else
                newstate = ACT_PASSIVE;

            AddSpellToActionBar(spellId, newstate);
        }
    }
}

bool CharmInfo::AddSpellToActionBar(uint32 spell_id, ActiveStates newstate)
{
    uint32 first_id = sSpellMgr.GetFirstSpellInChain(spell_id);

    // new spell rank can be already listed
    for(uint8 i = 0; i < MAX_UNIT_ACTION_BAR_INDEX; ++i)
    {
        if (uint32 action = PetActionBar[i].GetAction())
        {
            if (PetActionBar[i].IsActionBarForSpell() && sSpellMgr.GetFirstSpellInChain(action) == first_id)
            {
                PetActionBar[i].SetAction(spell_id);
                return true;
            }
        }
    }

    // or use empty slot in other case
    for(uint8 i = 0; i < MAX_UNIT_ACTION_BAR_INDEX; ++i)
    {
        if (!PetActionBar[i].GetAction() && PetActionBar[i].IsActionBarForSpell())
        {
            SetActionBar(i,spell_id,newstate == ACT_DECIDE ? ACT_DISABLED : newstate);
            return true;
        }
    }
    return false;
}

bool CharmInfo::RemoveSpellFromActionBar(uint32 spell_id)
{
    uint32 first_id = sSpellMgr.GetFirstSpellInChain(spell_id);

    for(uint8 i = 0; i < MAX_UNIT_ACTION_BAR_INDEX; ++i)
    {
        if (uint32 action = PetActionBar[i].GetAction())
        {
            if (PetActionBar[i].IsActionBarForSpell() && sSpellMgr.GetFirstSpellInChain(action) == first_id)
            {
                SetActionBar(i,0,ACT_DISABLED);
                return true;
            }
        }
    }

    return false;
}

void CharmInfo::ToggleCreatureAutocast(uint32 spellid, bool apply)
{
    if(IsPassiveSpell(spellid))
        return;

    for(uint32 x = 0; x < CREATURE_MAX_SPELLS; ++x)
        if(spellid == m_charmspells[x].GetAction())
            m_charmspells[x].SetType(apply ? ACT_ENABLED : ACT_DISABLED);
}

void CharmInfo::SetPetNumber(uint32 petnumber, bool statwindow)
{
    m_petnumber = petnumber;
    if(statwindow)
        m_unit->SetUInt32Value(UNIT_FIELD_PETNUMBER, m_petnumber);
    else
        m_unit->SetUInt32Value(UNIT_FIELD_PETNUMBER, 0);
}

void CharmInfo::LoadPetActionBar(const std::string& data )
{
    InitPetActionBar();

    Tokens tokens = StrSplit(data, " ");

    if (tokens.size() != (ACTION_BAR_INDEX_END-ACTION_BAR_INDEX_START)*2)
        return;                                             // non critical, will reset to default

    int index;
    Tokens::iterator iter;
    for(iter = tokens.begin(), index = ACTION_BAR_INDEX_START; index < ACTION_BAR_INDEX_END; ++iter, ++index )
    {
        // use unsigned cast to avoid sign negative format use at long-> ActiveStates (int) conversion
        uint8 type  = (uint8)atol((*iter).c_str());
        ++iter;
        uint32 action = atol((*iter).c_str());

        PetActionBar[index].SetActionAndType(action,ActiveStates(type));

        // check correctness
        if(PetActionBar[index].IsActionBarForSpell() && !sSpellStore.LookupEntry(PetActionBar[index].GetAction()))
            SetActionBar(index,0,ACT_DISABLED);
    }
}

void CharmInfo::BuildActionBar( WorldPacket* data )
{
    for(uint32 i = 0; i < MAX_UNIT_ACTION_BAR_INDEX; ++i)
        *data << uint32(PetActionBar[i].packedData);
}

void CharmInfo::SetSpellAutocast( uint32 spell_id, bool state )
{
    for(int i = 0; i < MAX_UNIT_ACTION_BAR_INDEX; ++i)
    {
        if(spell_id == PetActionBar[i].GetAction() && PetActionBar[i].IsActionBarForSpell())
        {
            PetActionBar[i].SetType(state ? ACT_ENABLED : ACT_DISABLED);
            break;
        }
    }
}

bool Unit::isFrozen() const
{
    return HasAuraState(AURA_STATE_FROZEN);
}

struct ProcTriggeredData
{
    ProcTriggeredData(SpellProcEventEntry const * _spellProcEvent, Aura* _triggeredByAura)
        : spellProcEvent(_spellProcEvent), triggeredByAura(_triggeredByAura),
        triggeredByAura_SpellPair(Unit::spellEffectPair(triggeredByAura->GetId(),triggeredByAura->GetEffIndex()))
        {}
    SpellProcEventEntry const *spellProcEvent;
    Aura* triggeredByAura;
    Unit::spellEffectPair triggeredByAura_SpellPair;
};

typedef std::list< ProcTriggeredData > ProcTriggeredList;
typedef std::list< uint32> RemoveSpellList;

// List of auras that CAN be trigger but may not exist in spell_proc_event
// in most case need for drop charges
// in some types of aura need do additional check
// for example SPELL_AURA_MECHANIC_IMMUNITY - need check for mechanic
bool InitTriggerAuraData()
{
    for (int i=0;i<TOTAL_AURAS;++i)
    {
      isTriggerAura[i]=false;
      isNonTriggerAura[i] = false;
    }
    isTriggerAura[SPELL_AURA_DUMMY] = true;
    isTriggerAura[SPELL_AURA_MOD_CONFUSE] = true;
    isTriggerAura[SPELL_AURA_MOD_THREAT] = true;
    isTriggerAura[SPELL_AURA_MOD_STUN] = true; // Aura not have charges but need remove him on trigger
    isTriggerAura[SPELL_AURA_MOD_DAMAGE_DONE] = true;
    isTriggerAura[SPELL_AURA_MOD_DAMAGE_TAKEN] = true;
    isTriggerAura[SPELL_AURA_MOD_RESISTANCE] = true;
    isTriggerAura[SPELL_AURA_MOD_ROOT] = true;
    isTriggerAura[SPELL_AURA_REFLECT_SPELLS] = true;
    isTriggerAura[SPELL_AURA_DAMAGE_IMMUNITY] = true;
    isTriggerAura[SPELL_AURA_PROC_TRIGGER_SPELL] = true;
    isTriggerAura[SPELL_AURA_PROC_TRIGGER_DAMAGE] = true;
    isTriggerAura[SPELL_AURA_MOD_CASTING_SPEED_NOT_STACK] = true;
    isTriggerAura[SPELL_AURA_MOD_POWER_COST_SCHOOL_PCT] = true;
    isTriggerAura[SPELL_AURA_MOD_POWER_COST_SCHOOL] = true;
    isTriggerAura[SPELL_AURA_REFLECT_SPELLS_SCHOOL] = true;
    isTriggerAura[SPELL_AURA_MECHANIC_IMMUNITY] = true;
    isTriggerAura[SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN] = true;
    isTriggerAura[SPELL_AURA_SPELL_MAGNET] = true;
    isTriggerAura[SPELL_AURA_MOD_ATTACK_POWER] = true;
    isTriggerAura[SPELL_AURA_ADD_CASTER_HIT_TRIGGER] = true;
    isTriggerAura[SPELL_AURA_OVERRIDE_CLASS_SCRIPTS] = true;
    isTriggerAura[SPELL_AURA_MOD_MECHANIC_RESISTANCE] = true;
    isTriggerAura[SPELL_AURA_RANGED_ATTACK_POWER_ATTACKER_BONUS] = true;
    isTriggerAura[SPELL_AURA_MOD_HASTE] = true;
    isTriggerAura[SPELL_AURA_MOD_ATTACKER_MELEE_HIT_CHANCE]=true;
    isTriggerAura[SPELL_AURA_PRAYER_OF_MENDING] = true;
    isTriggerAura[SPELL_AURA_PROC_TRIGGER_SPELL_WITH_VALUE] = true;
    isTriggerAura[SPELL_AURA_MOD_DAMAGE_FROM_CASTER] = true;
    isTriggerAura[SPELL_AURA_MOD_SPELL_CRIT_CHANCE] = true;
    isTriggerAura[SPELL_AURA_MAELSTROM_WEAPON] = true;

    isNonTriggerAura[SPELL_AURA_MOD_POWER_REGEN]=true;
    isNonTriggerAura[SPELL_AURA_REDUCE_PUSHBACK]=true;

    return true;
}

uint32 createProcExtendMask(SpellNonMeleeDamage *damageInfo, SpellMissInfo missCondition)
{
    uint32 procEx = PROC_EX_NONE;
    // Check victim state
    if (missCondition!=SPELL_MISS_NONE)
    switch (missCondition)
    {
        case SPELL_MISS_MISS:    procEx|=PROC_EX_MISS;   break;
        case SPELL_MISS_RESIST:  procEx|=PROC_EX_RESIST; break;
        case SPELL_MISS_DODGE:   procEx|=PROC_EX_DODGE;  break;
        case SPELL_MISS_PARRY:   procEx|=PROC_EX_PARRY;  break;
        case SPELL_MISS_BLOCK:   procEx|=PROC_EX_BLOCK;  break;
        case SPELL_MISS_EVADE:   procEx|=PROC_EX_EVADE;  break;
        case SPELL_MISS_IMMUNE:  procEx|=PROC_EX_IMMUNE; break;
        case SPELL_MISS_IMMUNE2: procEx|=PROC_EX_IMMUNE; break;
        case SPELL_MISS_DEFLECT: procEx|=PROC_EX_DEFLECT;break;
        case SPELL_MISS_ABSORB:  procEx|=PROC_EX_ABSORB; break;
        case SPELL_MISS_REFLECT: procEx|=PROC_EX_REFLECT;break;
        default:
            break;
    }
    else
    {
        // On block
        if (damageInfo->blocked)
            procEx|=PROC_EX_BLOCK;
        // On absorb
        if (damageInfo->absorb)
            procEx|=PROC_EX_ABSORB;
        // On crit
        if (damageInfo->HitInfo & SPELL_HIT_TYPE_CRIT)
            procEx|=PROC_EX_CRITICAL_HIT;
        else
            procEx|=PROC_EX_NORMAL_HIT;
    }
    return procEx;
}

void Unit::ProcDamageAndSpellFor( bool isVictim, Unit * pTarget, uint32 procFlag, uint32 procExtra, WeaponAttackType attType, SpellEntry const * procSpell, uint32 damage )
{
    // For melee/ranged based attack need update skills and set some Aura states
    if (procFlag & MELEE_BASED_TRIGGER_MASK)
    {
        // Update skills here for players
        if (GetTypeId() == TYPEID_PLAYER)
        {
            // On melee based hit/miss/resist need update skill (for victim and attacker)
            if (procExtra&(PROC_EX_NORMAL_HIT|PROC_EX_MISS|PROC_EX_RESIST))
            {
                if (pTarget->GetTypeId() != TYPEID_PLAYER && pTarget->GetCreatureType() != CREATURE_TYPE_CRITTER)
                    ((Player*)this)->UpdateCombatSkills(pTarget, attType, isVictim);
            }
            // Update defence if player is victim and parry/dodge/block
            if (isVictim && procExtra&(PROC_EX_DODGE|PROC_EX_PARRY|PROC_EX_BLOCK))
                ((Player*)this)->UpdateDefense();
        }
        // If exist crit/parry/dodge/block need update aura state (for victim and attacker)
        if (procExtra & (PROC_EX_CRITICAL_HIT|PROC_EX_PARRY|PROC_EX_DODGE|PROC_EX_BLOCK))
        {
            // for victim
            if (isVictim)
            {
                // if victim and dodge attack
                if (procExtra&PROC_EX_DODGE)
                {
                    //Update AURA_STATE on dodge
                    if (getClass() != CLASS_ROGUE) // skip Rogue Riposte
                    {
                        ModifyAuraState(AURA_STATE_DEFENSE, true);
                        StartReactiveTimer( REACTIVE_DEFENSE );
                    }
                }
                // if victim and parry attack
                if (procExtra & PROC_EX_PARRY)
                {
                    // For Hunters only Counterattack (skip Mongoose bite)
                    if (getClass() == CLASS_HUNTER)
                    {
                        ModifyAuraState(AURA_STATE_HUNTER_PARRY, true);
                        StartReactiveTimer( REACTIVE_HUNTER_PARRY );
                    }
                    else
                    {
                        ModifyAuraState(AURA_STATE_DEFENSE, true);
                        StartReactiveTimer( REACTIVE_DEFENSE );
                    }
                }
                // if and victim block attack
                if (procExtra & PROC_EX_BLOCK)
                {
                    ModifyAuraState(AURA_STATE_DEFENSE,true);
                    StartReactiveTimer( REACTIVE_DEFENSE );
                }
            }
            else //For attacker
            {
                // Overpower on victim dodge
                if (procExtra&PROC_EX_DODGE && GetTypeId() == TYPEID_PLAYER && getClass() == CLASS_WARRIOR)
                {
                    ((Player*)this)->AddComboPoints(pTarget, 1);
                    StartReactiveTimer( REACTIVE_OVERPOWER );
                }
            }
        }
    }

    RemoveSpellList removedSpells;
    ProcTriggeredList procTriggered;
    // Fill procTriggered list
    for(AuraMap::const_iterator itr = GetAuras().begin(); itr!= GetAuras().end(); ++itr)
    {
        // skip deleted auras (possible at recursive triggered call
        if(itr->second->IsDeleted())
            continue;

        SpellProcEventEntry const* spellProcEvent = NULL;
        if(!IsTriggeredAtSpellProcEvent(pTarget, itr->second, procSpell, procFlag, procExtra, attType, isVictim, (damage > 0), spellProcEvent))
           continue;

        itr->second->SetInUse(true);                        // prevent aura deletion
        procTriggered.push_back( ProcTriggeredData(spellProcEvent, itr->second) );
    }

    // Nothing found
    if (procTriggered.empty())
        return;

    // Handle effects proceed this time
    for(ProcTriggeredList::const_iterator i = procTriggered.begin(); i != procTriggered.end(); ++i)
    {
        // Some auras can be deleted in function called in this loop (except first, ofc)
        Aura *triggeredByAura = i->triggeredByAura;
        if(triggeredByAura->IsDeleted())
            continue;

        SpellProcEventEntry const *spellProcEvent = i->spellProcEvent;
        Modifier *auraModifier = triggeredByAura->GetModifier();
        SpellEntry const *spellInfo = triggeredByAura->GetSpellProto();
        bool useCharges = triggeredByAura->GetAuraCharges() > 0;
        // For players set spell cooldown if need
        uint32 cooldown = 0;
        if (GetTypeId() == TYPEID_PLAYER && spellProcEvent && spellProcEvent->cooldown)
            cooldown = spellProcEvent->cooldown;

        switch(auraModifier->m_auraname)
        {
            case SPELL_AURA_PROC_TRIGGER_SPELL:
            {
                sLog.outDebug("ProcDamageAndSpell: casting spell %u (triggered by %s aura of spell %u)", spellInfo->Id,(isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());
                // Don`t drop charge or add cooldown for not started trigger
                if (!HandleProcTriggerSpell(pTarget, damage, triggeredByAura, procSpell, procFlag, procExtra, cooldown))
                {
                    triggeredByAura->SetInUse(false);
                    continue;
                }
                break;
            }
            case SPELL_AURA_PROC_TRIGGER_DAMAGE:
            {
                sLog.outDebug("ProcDamageAndSpell: doing %u damage from spell id %u (triggered by %s aura of spell %u)", auraModifier->m_amount, spellInfo->Id, (isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());
                SpellNonMeleeDamage damageInfo(this, pTarget, spellInfo->Id, spellInfo->SchoolMask);
                CalculateSpellDamage(&damageInfo, auraModifier->m_amount, spellInfo);
                DealDamageMods(damageInfo.target,damageInfo.damage,&damageInfo.absorb);
                SendSpellNonMeleeDamageLog(&damageInfo);
                DealSpellDamage(&damageInfo, true);
                break;
            }
            case SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN:
            case SPELL_AURA_MANA_SHIELD:
            case SPELL_AURA_OBS_MOD_MANA:
            case SPELL_AURA_ADD_PCT_MODIFIER:
            case SPELL_AURA_DUMMY:
            {
                sLog.outDebug("ProcDamageAndSpell: casting spell id %u (triggered by %s dummy aura of spell %u)", spellInfo->Id,(isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());
                if (!HandleDummyAuraProc(pTarget, damage, triggeredByAura, procSpell, procFlag, procExtra, cooldown))
                {
                    triggeredByAura->SetInUse(false);
                    continue;
                }
                break;
            }
            case SPELL_AURA_MOD_HASTE:
            {
                sLog.outDebug("ProcDamageAndSpell: casting spell id %u (triggered by %s haste aura of spell %u)", spellInfo->Id,(isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());
                if (!HandleHasteAuraProc(pTarget, damage, triggeredByAura, procSpell, procFlag, procExtra, cooldown))
                {
                    triggeredByAura->SetInUse(false);
                    continue;
                }
                break;
            }
            case SPELL_AURA_OVERRIDE_CLASS_SCRIPTS:
            {
                sLog.outDebug("ProcDamageAndSpell: casting spell id %u (triggered by %s aura of spell %u)", spellInfo->Id,(isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());
                if (!HandleOverrideClassScriptAuraProc(pTarget, damage, triggeredByAura, procSpell, cooldown))
                {
                    triggeredByAura->SetInUse(false);
                    continue;
                }
                break;
            }
            case SPELL_AURA_PRAYER_OF_MENDING:
            {
                sLog.outDebug("ProcDamageAndSpell: casting mending (triggered by %s dummy aura of spell %u)",
                    (isVictim?"a victim's":"an attacker's"),triggeredByAura->GetId());

                HandleMendingAuraProc(triggeredByAura);
                break;
            }
            case SPELL_AURA_PROC_TRIGGER_SPELL_WITH_VALUE:
            {
                sLog.outDebug("ProcDamageAndSpell: casting spell %u (triggered with value by %s aura of spell %u)", spellInfo->Id,(isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());

                if (!HandleProcTriggerSpell(pTarget, damage, triggeredByAura, procSpell, procFlag, procExtra, cooldown))
                {
                    triggeredByAura->SetInUse(false);
                    continue;
                }
                break;
            }
            case SPELL_AURA_MOD_CASTING_SPEED_NOT_STACK:
                // Skip melee hits or instant cast spells
                if (procSpell == NULL || GetSpellCastTime(procSpell) == 0)
                {
                    triggeredByAura->SetInUse(false);
                    continue;
                }
                break;
            case SPELL_AURA_REFLECT_SPELLS_SCHOOL:
                // Skip Melee hits and spells ws wrong school
                if (procSpell == NULL || (auraModifier->m_miscvalue & procSpell->SchoolMask) == 0)
                {
                    triggeredByAura->SetInUse(false);
                    continue;
                }
                break;
            case SPELL_AURA_MOD_POWER_COST_SCHOOL_PCT:
            case SPELL_AURA_MOD_POWER_COST_SCHOOL:
                // Skip melee hits and spells ws wrong school or zero cost
                if (procSpell == NULL ||
                    (procSpell->manaCost == 0 && procSpell->ManaCostPercentage == 0) || // Cost check
                    (auraModifier->m_miscvalue & procSpell->SchoolMask) == 0)         // School check
                {
                    triggeredByAura->SetInUse(false);
                    continue;
                }
                break;
            case SPELL_AURA_MECHANIC_IMMUNITY:
                // Compare mechanic
                if (procSpell==NULL || procSpell->Mechanic != auraModifier->m_miscvalue)
                {
                    triggeredByAura->SetInUse(false);
                    continue;
                }
                break;
            case SPELL_AURA_MOD_MECHANIC_RESISTANCE:
                // Compare mechanic
                if (procSpell==NULL || procSpell->Mechanic != auraModifier->m_miscvalue)
                {
                    triggeredByAura->SetInUse(false);
                    continue;
                }
                break;
            case SPELL_AURA_MOD_DAMAGE_FROM_CASTER:
                // Compare casters
                if (triggeredByAura->GetCasterGUID() != pTarget->GetGUID())
                {
                    triggeredByAura->SetInUse(false);
                    continue;
                }
                break;
            case SPELL_AURA_MOD_SPELL_CRIT_CHANCE:
                if (!procSpell)
                {
                    triggeredByAura->SetInUse(false);
                    continue;
                }
                sLog.outDebug("ProcDamageAndSpell: casting spell id %u (triggered by %s spell crit chance aura of spell %u)", spellInfo->Id,(isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());
                if (!HandleSpellCritChanceAuraProc(pTarget, damage, triggeredByAura, procSpell, procFlag, procExtra, cooldown))
                {
                    triggeredByAura->SetInUse(false);
                    continue;
                }
                break;
            case SPELL_AURA_MAELSTROM_WEAPON:
                sLog.outDebug("ProcDamageAndSpell: casting spell id %u (triggered by %s maelstrom aura of spell %u)", spellInfo->Id,(isVictim?"a victim's":"an attacker's"), triggeredByAura->GetId());

                // remove all stack;
                RemoveSpellsCausingAura(SPELL_AURA_MAELSTROM_WEAPON);
                triggeredByAura->SetInUse(false);           // this safe, aura locked
                continue;                                   // avoid re-remove attempts
            default:
                // nothing do, just charges counter
                break;
        }

        // Remove charge (aura can be removed by triggers)
        if(useCharges && !triggeredByAura->IsDeleted())
        {
            // If last charge dropped add spell to remove list
            if(triggeredByAura->DropAuraCharge())
                removedSpells.push_back(triggeredByAura->GetId());
        }

        triggeredByAura->SetInUse(false);
    }
    if (!removedSpells.empty())
    {
        // Sort spells and remove dublicates
        removedSpells.sort();
        removedSpells.unique();
        // Remove auras from removedAuras
        for(RemoveSpellList::const_iterator i = removedSpells.begin(); i != removedSpells.end();++i)
            RemoveSingleSpellAurasFromStack(*i);
    }
}

SpellSchoolMask Unit::GetMeleeDamageSchoolMask() const
{
    return SPELL_SCHOOL_MASK_NORMAL;
}

Player* Unit::GetSpellModOwner()
{
    if(GetTypeId()==TYPEID_PLAYER)
        return (Player*)this;
    if(((Creature*)this)->isPet() || ((Creature*)this)->isTotem())
    {
        Unit* owner = GetOwner();
        if(owner && owner->GetTypeId()==TYPEID_PLAYER)
            return (Player*)owner;
    }
    return NULL;
}

///----------Pet responses methods-----------------
void Unit::SendPetCastFail(uint32 spellid, SpellCastResult msg)
{
    if(msg == SPELL_CAST_OK)
        return;

    Unit *owner = GetCharmerOrOwner();
    if(!owner || owner->GetTypeId() != TYPEID_PLAYER)
        return;

    WorldPacket data(SMSG_PET_CAST_FAILED, 1 + 4 + 1);
    data << uint8(0);                                       // cast count?
    data << uint32(spellid);
    data << uint8(msg);
    // uint32 for some reason
    // uint32 for some reason
    ((Player*)owner)->GetSession()->SendPacket(&data);
}

void Unit::SendPetActionFeedback (uint8 msg)
{
    Unit* owner = GetOwner();
    if(!owner || owner->GetTypeId() != TYPEID_PLAYER)
        return;

    WorldPacket data(SMSG_PET_ACTION_FEEDBACK, 1);
    data << uint8(msg);
    ((Player*)owner)->GetSession()->SendPacket(&data);
}

void Unit::SendPetTalk (uint32 pettalk)
{
    Unit* owner = GetOwner();
    if(!owner || owner->GetTypeId() != TYPEID_PLAYER)
        return;

    WorldPacket data(SMSG_PET_ACTION_SOUND, 8 + 4);
    data << uint64(GetGUID());
    data << uint32(pettalk);
    ((Player*)owner)->GetSession()->SendPacket(&data);
}

void Unit::SendPetAIReaction(uint64 guid)
{
    Unit* owner = GetOwner();
    if(!owner || owner->GetTypeId() != TYPEID_PLAYER)
        return;

    WorldPacket data(SMSG_AI_REACTION, 8 + 4);
    data << uint64(guid);
    data << uint32(AI_REACTION_AGGRO);
    ((Player*)owner)->GetSession()->SendPacket(&data);
}

///----------End of Pet responses methods----------

void Unit::StopMoving()
{
    clearUnitState(UNIT_STAT_MOVING);

    // send explicit stop packet
    // player expected for correct work SPLINEFLAG_WALKMODE
    SendMonsterMove(GetPositionX(), GetPositionY(), GetPositionZ(), SPLINETYPE_NORMAL, GetTypeId() == TYPEID_PLAYER ? SPLINEFLAG_WALKMODE : SPLINEFLAG_NONE, 0);

    // update position and orientation for near players
    WorldPacket data;
    BuildHeartBeatMsg(&data);
    SendMessageToSet(&data, false);
}

void Unit::SetFeared(bool apply, uint64 const& casterGUID, uint32 spellID, uint32 time)
{
    if( apply )
    {
        if(HasAuraType(SPELL_AURA_PREVENTS_FLEEING))
            return;

        SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_FLEEING);

        GetMotionMaster()->MovementExpired(false);
        CastStop(GetGUID() == casterGUID ? spellID : 0);

        Unit* caster = ObjectAccessor::GetUnit(*this,casterGUID);

        GetMotionMaster()->MoveFleeing(caster, time);       // caster==NULL processed in MoveFleeing
    }
    else
    {
        RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_FLEEING);

        GetMotionMaster()->MovementExpired(false);

        if( GetTypeId() != TYPEID_PLAYER && isAlive() )
        {
            // restore appropriate movement generator
            if(getVictim())
                GetMotionMaster()->MoveChase(getVictim());
            else
                GetMotionMaster()->Initialize();

            // attack caster if can
            Unit* caster = Unit::GetUnit(*this, casterGUID);
            if(caster && ((Creature*)this)->AI())
                ((Creature*)this)->AI()->AttackedBy(caster);
        }
    }

    if (GetTypeId() == TYPEID_PLAYER)
        ((Player*)this)->SetClientControl(this, !apply);
}

void Unit::SetConfused(bool apply, uint64 const& casterGUID, uint32 spellID)
{
    if( apply )
    {
        SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_CONFUSED);

        CastStop(GetGUID()==casterGUID ? spellID : 0);

        GetMotionMaster()->MoveConfused();
    }
    else
    {
        RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_CONFUSED);

        GetMotionMaster()->MovementExpired(false);

        if (GetTypeId() != TYPEID_PLAYER && isAlive())
        {
            // restore appropriate movement generator
            if(getVictim())
                GetMotionMaster()->MoveChase(getVictim());
            else
                GetMotionMaster()->Initialize();
        }
    }

    if(GetTypeId() == TYPEID_PLAYER)
        ((Player*)this)->SetClientControl(this, !apply);
}

void Unit::SetFeignDeath(bool apply, uint64 const& casterGUID, uint32 /*spellID*/)
{
    if( apply )
    {
        /*
        WorldPacket data(SMSG_FEIGN_DEATH_RESISTED, 9);
        data<<GetGUID();
        data<<uint8(0);
        SendMessageToSet(&data,true);
        */

        if(GetTypeId() != TYPEID_PLAYER)
            StopMoving();
        else
            ((Player*)this)->m_movementInfo.SetMovementFlags(MOVEFLAG_NONE);

                                                            // blizz like 2.0.x
        SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNK_29);
                                                            // blizz like 2.0.x
        SetFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_FEIGN_DEATH);
                                                            // blizz like 2.0.x
        SetFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_DEAD);

        addUnitState(UNIT_STAT_DIED);
        CombatStop();
        RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_IMMUNE_OR_LOST_SELECTION);

        // prevent interrupt message
        if (casterGUID == GetGUID())
            FinishSpell(CURRENT_GENERIC_SPELL,false);
        InterruptNonMeleeSpells(true);
        getHostileRefManager().deleteReferences();
    }
    else
    {
        /*
        WorldPacket data(SMSG_FEIGN_DEATH_RESISTED, 9);
        data<<GetGUID();
        data<<uint8(1);
        SendMessageToSet(&data,true);
        */
                                                            // blizz like 2.0.x
        RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNK_29);
                                                            // blizz like 2.0.x
        RemoveFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_FEIGN_DEATH);
                                                            // blizz like 2.0.x
        RemoveFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_DEAD);

        clearUnitState(UNIT_STAT_DIED);

        if (GetTypeId() != TYPEID_PLAYER && isAlive())
        {
            // restore appropriate movement generator
            if(getVictim())
                GetMotionMaster()->MoveChase(getVictim());
            else
                GetMotionMaster()->Initialize();
        }

    }
}

bool Unit::IsSitState() const
{
    uint8 s = getStandState();
    return
        s == UNIT_STAND_STATE_SIT_CHAIR        || s == UNIT_STAND_STATE_SIT_LOW_CHAIR  ||
        s == UNIT_STAND_STATE_SIT_MEDIUM_CHAIR || s == UNIT_STAND_STATE_SIT_HIGH_CHAIR ||
        s == UNIT_STAND_STATE_SIT;
}

bool Unit::IsStandState() const
{
    uint8 s = getStandState();
    return !IsSitState() && s != UNIT_STAND_STATE_SLEEP && s != UNIT_STAND_STATE_KNEEL;
}

void Unit::SetStandState(uint8 state)
{
    SetByteValue(UNIT_FIELD_BYTES_1, 0, state);

    if (IsStandState())
       RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_NOT_SEATED);

    if(GetTypeId()==TYPEID_PLAYER)
    {
        WorldPacket data(SMSG_STANDSTATE_UPDATE, 1);
        data << (uint8)state;
        ((Player*)this)->GetSession()->SendPacket(&data);
    }
}

bool Unit::IsPolymorphed() const
{
    return GetSpellSpecific(getTransForm())==SPELL_MAGE_POLYMORPH;
}

void Unit::SetDisplayId(uint32 modelId)
{
    SetUInt32Value(UNIT_FIELD_DISPLAYID, modelId);

    if(GetTypeId() == TYPEID_UNIT && ((Creature*)this)->isPet())
    {
        Pet *pet = ((Pet*)this);
        if(!pet->isControlled())
            return;
        Unit *owner = GetOwner();
        if(owner && (owner->GetTypeId() == TYPEID_PLAYER) && ((Player*)owner)->GetGroup())
            ((Player*)owner)->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_MODEL_ID);
    }
}

void Unit::ClearComboPointHolders()
{
    while(!m_ComboPointHolders.empty())
    {
        uint32 lowguid = *m_ComboPointHolders.begin();

        Player* plr = sObjectMgr.GetPlayer(MAKE_NEW_GUID(lowguid, 0, HIGHGUID_PLAYER));
        if(plr && plr->GetComboTarget()==GetGUID())         // recheck for safe
            plr->ClearComboPoints();                        // remove also guid from m_ComboPointHolders;
        else
            m_ComboPointHolders.erase(lowguid);             // or remove manually
    }
}

void Unit::ClearAllReactives()
{
    for(int i=0; i < MAX_REACTIVE; ++i)
        m_reactiveTimer[i] = 0;

    if (HasAuraState( AURA_STATE_DEFENSE))
        ModifyAuraState(AURA_STATE_DEFENSE, false);
    if (getClass() == CLASS_HUNTER && HasAuraState( AURA_STATE_HUNTER_PARRY))
        ModifyAuraState(AURA_STATE_HUNTER_PARRY, false);
    if(getClass() == CLASS_WARRIOR && GetTypeId() == TYPEID_PLAYER)
        ((Player*)this)->ClearComboPoints();
}

void Unit::UpdateReactives( uint32 p_time )
{
    for(int i = 0; i < MAX_REACTIVE; ++i)
    {
        ReactiveType reactive = ReactiveType(i);

        if(!m_reactiveTimer[reactive])
            continue;

        if ( m_reactiveTimer[reactive] <= p_time)
        {
            m_reactiveTimer[reactive] = 0;

            switch ( reactive )
            {
                case REACTIVE_DEFENSE:
                    if (HasAuraState(AURA_STATE_DEFENSE))
                        ModifyAuraState(AURA_STATE_DEFENSE, false);
                    break;
                case REACTIVE_HUNTER_PARRY:
                    if ( getClass() == CLASS_HUNTER && HasAuraState(AURA_STATE_HUNTER_PARRY))
                        ModifyAuraState(AURA_STATE_HUNTER_PARRY, false);
                    break;
                case REACTIVE_OVERPOWER:
                    if(getClass() == CLASS_WARRIOR && GetTypeId() == TYPEID_PLAYER)
                        ((Player*)this)->ClearComboPoints();
                    break;
                default:
                    break;
            }
        }
        else
        {
            m_reactiveTimer[reactive] -= p_time;
        }
    }
}

Unit* Unit::SelectRandomUnfriendlyTarget(Unit* except /*= NULL*/, float radius /*= ATTACK_DISTANCE*/) const
{
    CellPair p(MaNGOS::ComputeCellPair(GetPositionX(), GetPositionY()));
    Cell cell(p);
    cell.data.Part.reserved = ALL_DISTRICT;
    cell.SetNoCreate();

    std::list<Unit *> targets;

    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck u_check(this, this, radius);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(this, targets, u_check);

    TypeContainerVisitor<MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck>, WorldTypeMapContainer > world_unit_searcher(searcher);
    TypeContainerVisitor<MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck>, GridTypeMapContainer >  grid_unit_searcher(searcher);

    cell.Visit(p, world_unit_searcher, *GetMap(), *this, radius);
    cell.Visit(p, grid_unit_searcher, *GetMap(), *this, radius);

    // remove current target
    if(except)
        targets.remove(except);

    // remove not LoS targets
    for(std::list<Unit *>::iterator tIter = targets.begin(); tIter != targets.end();)
    {
        if(!IsWithinLOSInMap(*tIter))
        {
            std::list<Unit *>::iterator tIter2 = tIter;
            ++tIter;
            targets.erase(tIter2);
        }
        else
            ++tIter;
    }

    // no appropriate targets
    if(targets.empty())
        return NULL;

    // select random
    uint32 rIdx = urand(0,targets.size()-1);
    std::list<Unit *>::const_iterator tcIter = targets.begin();
    for(uint32 i = 0; i < rIdx; ++i)
        ++tcIter;

    return *tcIter;
}

Unit* Unit::SelectRandomFriendlyTarget(Unit* except /*= NULL*/, float radius /*= ATTACK_DISTANCE*/) const
{
    CellPair p(MaNGOS::ComputeCellPair(GetPositionX(), GetPositionY()));
    Cell cell(p);
    cell.data.Part.reserved = ALL_DISTRICT;
    cell.SetNoCreate();

    std::list<Unit *> targets;

    MaNGOS::AnyFriendlyUnitInObjectRangeCheck u_check(this, this, radius);
    MaNGOS::UnitListSearcher<MaNGOS::AnyFriendlyUnitInObjectRangeCheck> searcher(this, targets, u_check);

    TypeContainerVisitor<MaNGOS::UnitListSearcher<MaNGOS::AnyFriendlyUnitInObjectRangeCheck>, WorldTypeMapContainer > world_unit_searcher(searcher);
    TypeContainerVisitor<MaNGOS::UnitListSearcher<MaNGOS::AnyFriendlyUnitInObjectRangeCheck>, GridTypeMapContainer >  grid_unit_searcher(searcher);

    cell.Visit(p, world_unit_searcher, *GetMap(), *this, radius);
    cell.Visit(p, grid_unit_searcher, *GetMap(), *this, radius);

    // remove current target
    if(except)
        targets.remove(except);

    // remove not LoS targets
    for(std::list<Unit *>::iterator tIter = targets.begin(); tIter != targets.end();)
    {
        if(!IsWithinLOSInMap(*tIter))
        {
            std::list<Unit *>::iterator tIter2 = tIter;
            ++tIter;
            targets.erase(tIter2);
        }
        else
            ++tIter;
    }

    // no appropriate targets
    if(targets.empty())
        return NULL;

    // select random
    uint32 rIdx = urand(0,targets.size()-1);
    std::list<Unit *>::const_iterator tcIter = targets.begin();
    for(uint32 i = 0; i < rIdx; ++i)
        ++tcIter;

    return *tcIter;
}

bool Unit::hasNegativeAuraWithInterruptFlag(uint32 flag)
{
    for (AuraMap::const_iterator iter = m_Auras.begin(); iter != m_Auras.end(); ++iter)
    {
        if (!iter->second->IsPositive() && iter->second->GetSpellProto()->AuraInterruptFlags & flag)
            return true;
    }
    return false;
}

void Unit::ApplyAttackTimePercentMod( WeaponAttackType att,float val, bool apply )
{
    if(val > 0)
    {
        ApplyPercentModFloatVar(m_modAttackSpeedPct[att], val, !apply);
        ApplyPercentModFloatValue(UNIT_FIELD_BASEATTACKTIME+att,val,!apply);
    }
    else
    {
        ApplyPercentModFloatVar(m_modAttackSpeedPct[att], -val, apply);
        ApplyPercentModFloatValue(UNIT_FIELD_BASEATTACKTIME+att,-val,apply);
    }
}

void Unit::ApplyCastTimePercentMod(float val, bool apply )
{
    if(val > 0)
        ApplyPercentModFloatValue(UNIT_MOD_CAST_SPEED,val,!apply);
    else
        ApplyPercentModFloatValue(UNIT_MOD_CAST_SPEED,-val,apply);
}

uint32 Unit::GetCastingTimeForBonus( SpellEntry const *spellProto, DamageEffectType damagetype, uint32 CastingTime )
{
    // Not apply this to creature casted spells with casttime==0
    if(CastingTime==0 && GetTypeId()==TYPEID_UNIT && !((Creature*)this)->isPet())
        return 3500;

    if (CastingTime > 7000) CastingTime = 7000;
    if (CastingTime < 1500) CastingTime = 1500;

    if(damagetype == DOT && !IsChanneledSpell(spellProto))
        CastingTime = 3500;

    int32 overTime    = 0;
    uint8 effects     = 0;
    bool DirectDamage = false;
    bool AreaEffect   = false;

    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        switch (spellProto->Effect[i])
        {
            case SPELL_EFFECT_SCHOOL_DAMAGE:
            case SPELL_EFFECT_POWER_DRAIN:
            case SPELL_EFFECT_HEALTH_LEECH:
            case SPELL_EFFECT_ENVIRONMENTAL_DAMAGE:
            case SPELL_EFFECT_POWER_BURN:
            case SPELL_EFFECT_HEAL:
                DirectDamage = true;
                break;
            case SPELL_EFFECT_APPLY_AURA:
                switch (spellProto->EffectApplyAuraName[i])
                {
                    case SPELL_AURA_PERIODIC_DAMAGE:
                    case SPELL_AURA_PERIODIC_HEAL:
                    case SPELL_AURA_PERIODIC_LEECH:
                        if ( GetSpellDuration(spellProto) )
                            overTime = GetSpellDuration(spellProto);
                        break;
                    default:
                        // -5% per additional effect
                        ++effects;
                        break;
                }
            default:
                break;
        }

        if (IsAreaEffectTarget(Targets(spellProto->EffectImplicitTargetA[i])) || IsAreaEffectTarget(Targets(spellProto->EffectImplicitTargetB[i])))
            AreaEffect = true;
    }

    // Combined Spells with Both Over Time and Direct Damage
    if (overTime > 0 && CastingTime > 0 && DirectDamage)
    {
        // mainly for DoTs which are 3500 here otherwise
        uint32 OriginalCastTime = GetSpellCastTime(spellProto);
        if (OriginalCastTime > 7000) OriginalCastTime = 7000;
        if (OriginalCastTime < 1500) OriginalCastTime = 1500;
        // Portion to Over Time
        float PtOT = (overTime / 15000.0f) / ((overTime / 15000.0f) + (OriginalCastTime / 3500.0f));

        if (damagetype == DOT)
            CastingTime = uint32(CastingTime * PtOT);
        else if (PtOT < 1.0f)
            CastingTime  = uint32(CastingTime * (1 - PtOT));
        else
            CastingTime = 0;
    }

    // Area Effect Spells receive only half of bonus
    if (AreaEffect)
        CastingTime /= 2;

    // -5% of total per any additional effect
    for (uint8 i = 0; i < effects; ++i)
    {
        if (CastingTime > 175)
        {
            CastingTime -= 175;
        }
        else
        {
            CastingTime = 0;
            break;
        }
    }

    return CastingTime;
}

void Unit::UpdateAuraForGroup(uint8 slot)
{
    if(GetTypeId() == TYPEID_PLAYER)
    {
        Player* player = (Player*)this;
        if(player->GetGroup())
        {
            player->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_AURAS);
            player->SetAuraUpdateMask(slot);
        }
    }
    else if(GetTypeId() == TYPEID_UNIT && ((Creature*)this)->isPet())
    {
        Pet *pet = ((Pet*)this);
        if(pet->isControlled())
        {
            Unit *owner = GetOwner();
            if(owner && (owner->GetTypeId() == TYPEID_PLAYER) && ((Player*)owner)->GetGroup())
            {
                ((Player*)owner)->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_AURAS);
                pet->SetAuraUpdateMask(slot);
            }
        }
    }
}

float Unit::GetAPMultiplier(WeaponAttackType attType, bool normalized)
{
    if (!normalized || GetTypeId() != TYPEID_PLAYER)
        return float(GetAttackTime(attType))/1000.0f;

    Item *Weapon = ((Player*)this)->GetWeaponForAttack(attType, true, false);
    if (!Weapon)
        return 2.4f;                                         // fist attack

    switch (Weapon->GetProto()->InventoryType)
    {
        case INVTYPE_2HWEAPON:
            return 3.3f;
        case INVTYPE_RANGED:
        case INVTYPE_RANGEDRIGHT:
        case INVTYPE_THROWN:
            return 2.8f;
        case INVTYPE_WEAPON:
        case INVTYPE_WEAPONMAINHAND:
        case INVTYPE_WEAPONOFFHAND:
        default:
            return Weapon->GetProto()->SubClass==ITEM_SUBCLASS_WEAPON_DAGGER ? 1.7f : 2.4f;
    }
}

Aura* Unit::GetDummyAura( uint32 spell_id ) const
{
    Unit::AuraList const& mDummy = GetAurasByType(SPELL_AURA_DUMMY);
    for(Unit::AuraList::const_iterator itr = mDummy.begin(); itr != mDummy.end(); ++itr)
        if ((*itr)->GetId() == spell_id)
            return *itr;

    return NULL;
}

void Unit::SetContestedPvP(Player *attackedPlayer)
{
    Player* player = GetCharmerOrOwnerPlayerOrPlayerItself();

    if(!player || attackedPlayer && (attackedPlayer == player || player->duel && player->duel->opponent == attackedPlayer))
        return;

    player->SetContestedPvPTimer(30000);
    if(!player->hasUnitState(UNIT_STAT_ATTACK_PLAYER))
    {
        player->addUnitState(UNIT_STAT_ATTACK_PLAYER);
        player->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_CONTESTED_PVP);
        // call MoveInLineOfSight for nearby contested guards
        SetVisibility(GetVisibility());
    }
    if(!hasUnitState(UNIT_STAT_ATTACK_PLAYER))
    {
        addUnitState(UNIT_STAT_ATTACK_PLAYER);
        // call MoveInLineOfSight for nearby contested guards
        SetVisibility(GetVisibility());
    }
}

void Unit::AddPetAura(PetAura const* petSpell)
{
    m_petAuras.insert(petSpell);
    if(Pet* pet = GetPet())
        pet->CastPetAura(petSpell);
}

void Unit::RemovePetAura(PetAura const* petSpell)
{
    m_petAuras.erase(petSpell);
    if(Pet* pet = GetPet())
        pet->RemoveAurasDueToSpell(petSpell->GetAura(pet->GetEntry()));
}

Pet* Unit::CreateTamedPetFrom(Creature* creatureTarget,uint32 spell_id)
{
    Pet* pet = new Pet(HUNTER_PET);

    if(!pet->CreateBaseAtCreature(creatureTarget))
    {
        delete pet;
        return NULL;
    }

    pet->SetOwnerGUID(GetGUID());
    pet->SetCreatorGUID(GetGUID());
    pet->setFaction(getFaction());
    pet->SetUInt32Value(UNIT_CREATED_BY_SPELL, spell_id);

    if(GetTypeId()==TYPEID_PLAYER)
        pet->SetUInt32Value(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP_ATTACKABLE);

    if(IsPvP())
        pet->SetPvP(true);

    if(IsFFAPvP())
        pet->SetFFAPvP(true);

    uint32 level = (creatureTarget->getLevel() < (getLevel() - 5)) ? (getLevel() - 5) : creatureTarget->getLevel();

    if(!pet->InitStatsForLevel(level))
    {
        sLog.outError("Pet::InitStatsForLevel() failed for creature (Entry: %u)!",creatureTarget->GetEntry());
        delete pet;
        return NULL;
    }

    pet->GetCharmInfo()->SetPetNumber(sObjectMgr.GeneratePetNumber(), true);
    // this enables pet details window (Shift+P)
    pet->AIM_Initialize();
    pet->InitPetCreateSpells();
    pet->InitLevelupSpellsForLevel();
    pet->InitTalentForLevel();
    pet->SetHealth(pet->GetMaxHealth());

    return pet;
}

bool Unit::IsTriggeredAtSpellProcEvent(Unit *pVictim, Aura* aura, SpellEntry const* procSpell, uint32 procFlag, uint32 procExtra, WeaponAttackType attType, bool isVictim, bool active, SpellProcEventEntry const*& spellProcEvent )
{
    SpellEntry const* spellProto = aura->GetSpellProto ();

    // Get proc Event Entry
    spellProcEvent = sSpellMgr.GetSpellProcEvent(spellProto->Id);

    // Aura info stored here
    Modifier *mod = aura->GetModifier();
    // Skip this auras
    if (isNonTriggerAura[mod->m_auraname])
        return false;
    // If not trigger by default and spellProcEvent==NULL - skip
    if (!isTriggerAura[mod->m_auraname] && spellProcEvent==NULL)
        return false;

    // Get EventProcFlag
    uint32 EventProcFlag;
    if (spellProcEvent && spellProcEvent->procFlags) // if exist get custom spellProcEvent->procFlags
        EventProcFlag = spellProcEvent->procFlags;
    else
        EventProcFlag = spellProto->procFlags;       // else get from spell proto
    // Continue if no trigger exist
    if (!EventProcFlag)
        return false;

    // Check spellProcEvent data requirements
    if(!SpellMgr::IsSpellProcEventCanTriggeredBy(spellProcEvent, EventProcFlag, procSpell, procFlag, procExtra, active))
        return false;

    // In most cases req get honor or XP from kill
    if (EventProcFlag & PROC_FLAG_KILL && GetTypeId() == TYPEID_PLAYER)
    {
        bool allow = ((Player*)this)->isHonorOrXPTarget(pVictim);
        // Shadow Word: Death - can trigger from every kill
        if (aura->GetId() == 32409)
            allow = true;
        if (!allow)
            return false;
    }
    // Aura added by spell can`t trogger from self (prevent drop charges/do triggers)
    // But except periodic triggers (can triggered from self)
    if(procSpell && procSpell->Id == spellProto->Id && !(spellProto->procFlags & PROC_FLAG_ON_TAKE_PERIODIC))
        return false;

    // Check if current equipment allows aura to proc
    if(!isVictim && GetTypeId() == TYPEID_PLAYER)
    {
        if(spellProto->EquippedItemClass == ITEM_CLASS_WEAPON)
        {
            Item *item = NULL;
            if(attType == BASE_ATTACK)
                item = ((Player*)this)->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
            else if (attType == OFF_ATTACK)
                item = ((Player*)this)->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
            else
                item = ((Player*)this)->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);

            if(!item || item->IsBroken() || item->GetProto()->Class != ITEM_CLASS_WEAPON || !((1<<item->GetProto()->SubClass) & spellProto->EquippedItemSubClassMask))
                return false;
        }
        else if(spellProto->EquippedItemClass == ITEM_CLASS_ARMOR)
        {
            // Check if player is wearing shield
            Item *item = ((Player*)this)->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
            if(!item || item->IsBroken() || item->GetProto()->Class != ITEM_CLASS_ARMOR || !((1<<item->GetProto()->SubClass) & spellProto->EquippedItemSubClassMask))
                return false;
        }
    }
    // Get chance from spell
    float chance = (float)spellProto->procChance;
    // If in spellProcEvent exist custom chance, chance = spellProcEvent->customChance;
    if(spellProcEvent && spellProcEvent->customChance)
        chance = spellProcEvent->customChance;
    // If PPM exist calculate chance from PPM
    if(!isVictim && spellProcEvent && spellProcEvent->ppmRate != 0)
    {
        uint32 WeaponSpeed = GetAttackTime(attType);
        chance = GetPPMProcChance(WeaponSpeed, spellProcEvent->ppmRate);
    }
    // Apply chance modifer aura
    if(Player* modOwner = GetSpellModOwner())
    {
        modOwner->ApplySpellMod(spellProto->Id,SPELLMOD_CHANCE_OF_SUCCESS,chance);
        modOwner->ApplySpellMod(spellProto->Id,SPELLMOD_FREQUENCY_OF_SUCCESS,chance);
    }

    return roll_chance_f(chance);
}

bool Unit::HandleMendingAuraProc( Aura* triggeredByAura )
{
    // aura can be deleted at casts
    SpellEntry const* spellProto = triggeredByAura->GetSpellProto();
    SpellEffectIndex effIdx = triggeredByAura->GetEffIndex();
    int32 heal = triggeredByAura->GetModifier()->m_amount;
    uint64 caster_guid = triggeredByAura->GetCasterGUID();

    // jumps
    int32 jumps = triggeredByAura->GetAuraCharges()-1;

    // current aura expire
    triggeredByAura->SetAuraCharges(1);             // will removed at next charges decrease

    // next target selection
    if(jumps > 0 && GetTypeId()==TYPEID_PLAYER && IS_PLAYER_GUID(caster_guid))
    {
        float radius;
        if (spellProto->EffectRadiusIndex[effIdx])
            radius = GetSpellRadius(sSpellRadiusStore.LookupEntry(spellProto->EffectRadiusIndex[effIdx]));
        else
            radius = GetSpellMaxRange(sSpellRangeStore.LookupEntry(spellProto->rangeIndex));

        if(Player* caster = ((Player*)triggeredByAura->GetCaster()))
        {
            caster->ApplySpellMod(spellProto->Id, SPELLMOD_RADIUS, radius,NULL);

            if(Player* target = ((Player*)this)->GetNextRandomRaidMember(radius))
            {
                // aura will applied from caster, but spell casted from current aura holder
                SpellModifier *mod = new SpellModifier(SPELLMOD_CHARGES,SPELLMOD_FLAT,jumps-5,spellProto->Id,spellProto->SpellFamilyFlags,spellProto->SpellFamilyFlags2);

                // remove before apply next (locked against deleted)
                triggeredByAura->SetInUse(true);
                RemoveAurasByCasterSpell(spellProto->Id,caster->GetGUID());

                caster->AddSpellMod(mod, true);
                CastCustomSpell(target,spellProto->Id,&heal,NULL,NULL,true,NULL,triggeredByAura,caster->GetGUID());
                caster->AddSpellMod(mod, false);
                triggeredByAura->SetInUse(false);
            }
        }
    }

    // heal
    CastCustomSpell(this,33110,&heal,NULL,NULL,true,NULL,NULL,caster_guid);
    return true;
}

void Unit::RemoveAurasAtMechanicImmunity(uint32 mechMask, uint32 exceptSpellId, bool non_positive /*= false*/)
{
    Unit::AuraMap& auras = GetAuras();
    for(Unit::AuraMap::iterator iter = auras.begin(); iter != auras.end();)
    {
        SpellEntry const *spell = iter->second->GetSpellProto();
        if (spell->Id == exceptSpellId)
            ++iter;
        else if (non_positive && iter->second->IsPositive())
            ++iter;
        else if (spell->Attributes & SPELL_ATTR_UNAFFECTED_BY_INVULNERABILITY)
            ++iter;
        else if (GetSpellMechanicMask(spell, iter->second->GetEffIndex()) & mechMask)
        {
            RemoveAurasDueToSpell(spell->Id);
            if(auras.empty())
                break;
            else
                iter = auras.begin();
        }
        else
            ++iter;
    }
}

void Unit::SetPhaseMask(uint32 newPhaseMask, bool update)
{
    if(newPhaseMask==GetPhaseMask())
        return;

    if(IsInWorld())
        RemoveNotOwnSingleTargetAuras(newPhaseMask);        // we can lost access to caster or target

    WorldObject::SetPhaseMask(newPhaseMask,update);

    if(IsInWorld())
        if(Pet* pet = GetPet())
            pet->SetPhaseMask(newPhaseMask,true);
}

void Unit::NearTeleportTo( float x, float y, float z, float orientation, bool casting /*= false*/ )
{
    if(GetTypeId() == TYPEID_PLAYER)
        ((Player*)this)->TeleportTo(GetMapId(), x, y, z, orientation, TELE_TO_NOT_LEAVE_TRANSPORT | TELE_TO_NOT_LEAVE_COMBAT | TELE_TO_NOT_UNSUMMON_PET | (casting ? TELE_TO_SPELL : 0));
    else
    {
        Creature* c = (Creature*)this;
        // Creature relocation acts like instant movement generator, so current generator expects interrupt/reset calls to react properly
        if (!c->GetMotionMaster()->empty())
            if (MovementGenerator *movgen = c->GetMotionMaster()->top())
                movgen->Interrupt(*c);

        GetMap()->CreatureRelocation((Creature*)this, x, y, z, orientation);

        WorldPacket data;
        BuildHeartBeatMsg(&data);
        SendMessageToSet(&data, false);
        // finished relocation, movegen can different from top before creature relocation,
        // but apply Reset expected to be safe in any case
        if (!c->GetMotionMaster()->empty())
            if (MovementGenerator *movgen = c->GetMotionMaster()->top())
                movgen->Reset(*c);
    }
}

void Unit::MonsterMove(float x, float y, float z, uint32 transitTime)
{
    SplineFlags flags = GetTypeId() == TYPEID_PLAYER ? SPLINEFLAG_WALKMODE : ((Creature*)this)->GetSplineFlags();
    SendMonsterMove(x, y, z, SPLINETYPE_NORMAL, flags, transitTime);

    if (GetTypeId() != TYPEID_PLAYER)
    {
        Creature* c = (Creature*)this;
        // Creature relocation acts like instant movement generator, so current generator expects interrupt/reset calls to react properly
        if (!c->GetMotionMaster()->empty())
            if (MovementGenerator *movgen = c->GetMotionMaster()->top())
                movgen->Interrupt(*c);

        GetMap()->CreatureRelocation((Creature*)this, x, y, z, 0.0f);

        // finished relocation, movegen can different from top before creature relocation,
        // but apply Reset expected to be safe in any case
        if (!c->GetMotionMaster()->empty())
            if (MovementGenerator *movgen = c->GetMotionMaster()->top())
                movgen->Reset(*c);
    }
}

void Unit::MonsterMoveWithSpeed(float x, float y, float z, uint32 transitTime)
{
    SendMonsterMoveWithSpeed(x, y, z, transitTime );

    if (GetTypeId() != TYPEID_PLAYER)
    {
        Creature* c = (Creature*)this;
        // Creature relocation acts like instant movement generator, so current generator expects interrupt/reset calls to react properly
        if (!c->GetMotionMaster()->empty())
            if (MovementGenerator *movgen = c->GetMotionMaster()->top())
                movgen->Interrupt(*c);

        GetMap()->CreatureRelocation((Creature*)this, x, y, z, 0.0f);

        // finished relocation, movegen can different from top before creature relocation,
        // but apply Reset expected to be safe in any case
        if (!c->GetMotionMaster()->empty())
            if (MovementGenerator *movgen = c->GetMotionMaster()->top())
                movgen->Reset(*c);
    }
}

struct SetPvPHelper
{
    explicit SetPvPHelper(bool _state) : state(_state) {}
    void operator()(Unit* unit) const { unit->SetPvP(state); }
    bool state;
};

void Unit::SetPvP( bool state )
{
    if(state)
        SetByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_PVP);
    else
        RemoveByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_PVP);

    CallForAllControlledUnits(SetPvPHelper(state),true,true,true);
}

struct SetFFAPvPHelper
{
    explicit SetFFAPvPHelper(bool _state) : state(_state) {}
    void operator()(Unit* unit) const { unit->SetFFAPvP(state); }
    bool state;
};

void Unit::SetFFAPvP( bool state )
{
    if(state)
        SetByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_FFA_PVP);
    else
        RemoveByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_FFA_PVP);

    CallForAllControlledUnits(SetFFAPvPHelper(state),true,true,true);
}

void Unit::KnockBackFrom(Unit* target, float horizontalSpeed, float verticalSpeed)
{
    float angle = this == target ? GetOrientation() + M_PI_F : target->GetAngle(this);
    float vsin = sin(angle);
    float vcos = cos(angle);

    // Effect propertly implemented only for players
    if(GetTypeId()==TYPEID_PLAYER)
    {
        WorldPacket data(SMSG_MOVE_KNOCK_BACK, 8+4+4+4+4+4);
        data.append(GetPackGUID());
        data << uint32(0);                                  // Sequence
        data << float(vcos);                                // x direction
        data << float(vsin);                                // y direction
        data << float(horizontalSpeed);                     // Horizontal speed
        data << float(-verticalSpeed);                      // Z Movement speed (vertical)
        ((Player*)this)->GetSession()->SendPacket(&data);
    }
    else
    {
        float dis = horizontalSpeed;

        float ox, oy, oz;
        GetPosition(ox, oy, oz);

        float fx = ox + dis * vcos;
        float fy = oy + dis * vsin;
        float fz = oz;

        float fx2, fy2, fz2;                                // getObjectHitPos overwrite last args in any result case
        if(VMAP::VMapFactory::createOrGetVMapManager()->getObjectHitPos(GetMapId(), ox,oy,oz+0.5f, fx,fy,oz+0.5f,fx2,fy2,fz2, -0.5f))
        {
            fx = fx2;
            fy = fy2;
            fz = fz2;
        }

        UpdateGroundPositionZ(fx, fy, fz);

        //FIXME: this mostly hack, must exist some packet for proper creature move at client side
        //       with CreatureRelocation at server side
        NearTeleportTo(fx, fy, fz, GetOrientation(), this == target);
    }
}

float Unit::GetCombatRatingReduction(CombatRating cr) const
{
    if (GetTypeId() == TYPEID_PLAYER)
        return ((Player const*)this)->GetRatingBonusValue(cr);
    else if (((Creature const*)this)->isPet())
    {
        // Player's pet have 0.4 resilience  from owner
        if (Unit* owner = GetOwner())
            if(owner->GetTypeId() == TYPEID_PLAYER)
                return ((Player*)owner)->GetRatingBonusValue(cr) * 0.4f;
    }

    return 0.0f;
}

uint32 Unit::GetCombatRatingDamageReduction(CombatRating cr, float rate, float cap, uint32 damage) const
{
    float percent = GetCombatRatingReduction(cr) * rate;
    if (percent > cap)
        percent = cap;
    return uint32 (percent * damage / 100.0f);
}

void Unit::SendThreatUpdate()
{
    ThreatList const& tlist = getThreatManager().getThreatList();
    if (uint32 count = tlist.size())
    {
        sLog.outDebug( "WORLD: Send SMSG_THREAT_UPDATE Message" );
        WorldPacket data(SMSG_THREAT_UPDATE, 8 + count * 8);
        data.append(GetPackGUID());
        data << uint32(count);
        for (ThreatList::const_iterator itr = tlist.begin(); itr != tlist.end(); ++itr)
        {
            data.appendPackGUID((*itr)->getUnitGuid());
            data << uint32((*itr)->getThreat());
        }
        SendMessageToSet(&data, false);
    }
}

void Unit::SendHighestThreatUpdate(HostileReference* pHostilReference)
{
    ThreatList const& tlist = getThreatManager().getThreatList();
    if (uint32 count = tlist.size())
    {
        sLog.outDebug( "WORLD: Send SMSG_HIGHEST_THREAT_UPDATE Message" );
        WorldPacket data(SMSG_HIGHEST_THREAT_UPDATE, 8 + 8 + count * 8);
        data.append(GetPackGUID());
        data.appendPackGUID(pHostilReference->getUnitGuid());
        data << uint32(count);
        for (ThreatList::const_iterator itr = tlist.begin(); itr != tlist.end(); ++itr)
        {
            data.appendPackGUID((*itr)->getUnitGuid());
            data << uint32((*itr)->getThreat());
        }
        SendMessageToSet(&data, false);
    }
}

void Unit::SendThreatClear()
{
    sLog.outDebug( "WORLD: Send SMSG_THREAT_CLEAR Message" );
    WorldPacket data(SMSG_THREAT_CLEAR, 8);
    data.append(GetPackGUID());
    SendMessageToSet(&data, false);
}

void Unit::SendThreatRemove(HostileReference* pHostileReference)
{
    sLog.outDebug( "WORLD: Send SMSG_THREAT_REMOVE Message" );
    WorldPacket data(SMSG_THREAT_REMOVE, 8 + 8);
    data.append(GetPackGUID());
    data.appendPackGUID(pHostileReference->getUnitGuid());
    SendMessageToSet(&data, false);
}

struct StopAttackFactionHelper
{
    explicit StopAttackFactionHelper(uint32 _faction_id) : faction_id(_faction_id) {}
    void operator()(Unit* unit) const { unit->StopAttackFaction(faction_id); }
    uint32 faction_id;
};

void Unit::StopAttackFaction(uint32 faction_id)
{
    if (Unit* victim = getVictim())
    {
        if (victim->getFactionTemplateEntry()->faction==faction_id)
        {
            AttackStop();
            if (IsNonMeleeSpellCasted(false))
                InterruptNonMeleeSpells(false);

            // melee and ranged forced attack cancel
            if (GetTypeId() == TYPEID_PLAYER)
                ((Player*)this)->SendAttackSwingCancelAttack();
        }
    }

    AttackerSet const& attackers = getAttackers();
    for(AttackerSet::const_iterator itr = attackers.begin(); itr != attackers.end();)
    {
        if ((*itr)->getFactionTemplateEntry()->faction==faction_id)
        {
            (*itr)->AttackStop();
            itr = attackers.begin();
        }
        else
            ++itr;
    }

    getHostileRefManager().deleteReferencesForFaction(faction_id);

    CallForAllControlledUnits(StopAttackFactionHelper(faction_id),false,true,true);
}

void Unit::CleanupDeletedAuras()
{
    // really delete auras "deleted" while processing its ApplyModify code
    for(AuraList::const_iterator itr = m_deletedAuras.begin(); itr != m_deletedAuras.end(); ++itr)
        delete *itr;
    m_deletedAuras.clear();
}

bool Unit::CheckAndIncreaseCastCounter()
{
    uint32 maxCasts = sWorld.getConfig(CONFIG_UINT32_MAX_SPELL_CASTS_IN_CHAIN);

    if (maxCasts && m_castCounter >= maxCasts)
        return false;

    ++m_castCounter;
    return true;
}
