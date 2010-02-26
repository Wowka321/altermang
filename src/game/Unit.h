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

#ifndef __UNIT_H
#define __UNIT_H

#include "Common.h"
#include "Object.h"
#include "Opcodes.h"
#include "SpellAuraDefines.h"
#include "UpdateFields.h"
#include "SharedDefines.h"
#include "ThreatManager.h"
#include "HostileRefManager.h"
#include "FollowerReference.h"
#include "FollowerRefManager.h"
#include "Utilities/EventProcessor.h"
#include "MotionMaster.h"
#include "DBCStructure.h"
#include <list>

enum SpellInterruptFlags
{
    SPELL_INTERRUPT_FLAG_MOVEMENT     = 0x01,
    SPELL_INTERRUPT_FLAG_DAMAGE       = 0x02,
    SPELL_INTERRUPT_FLAG_INTERRUPT    = 0x04,
    SPELL_INTERRUPT_FLAG_AUTOATTACK   = 0x08,
    SPELL_INTERRUPT_FLAG_ABORT_ON_DMG = 0x10,               // _complete_ interrupt on direct damage
    //SPELL_INTERRUPT_UNK             = 0x20                // unk, 564 of 727 spells having this spell start with "Glyph"
};

enum SpellChannelInterruptFlags
{
    CHANNEL_FLAG_DAMAGE      = 0x0002,
    CHANNEL_FLAG_MOVEMENT    = 0x0008,
    CHANNEL_FLAG_TURNING     = 0x0010,
    CHANNEL_FLAG_DAMAGE2     = 0x0080,
    CHANNEL_FLAG_DELAY       = 0x4000
};

enum SpellAuraInterruptFlags
{
    AURA_INTERRUPT_FLAG_UNK0                        = 0x00000001,   // 0    removed when getting hit by a negative spell?
    AURA_INTERRUPT_FLAG_DAMAGE                      = 0x00000002,   // 1    removed by any damage
    AURA_INTERRUPT_FLAG_UNK2                        = 0x00000004,   // 2
    AURA_INTERRUPT_FLAG_MOVE                        = 0x00000008,   // 3    removed by any movement
    AURA_INTERRUPT_FLAG_TURNING                     = 0x00000010,   // 4    removed by any turning
    AURA_INTERRUPT_FLAG_ENTER_COMBAT                = 0x00000020,   // 5    removed by entering combat
    AURA_INTERRUPT_FLAG_NOT_MOUNTED                 = 0x00000040,   // 6    removed by unmounting
    AURA_INTERRUPT_FLAG_NOT_ABOVEWATER              = 0x00000080,   // 7    removed by entering water
    AURA_INTERRUPT_FLAG_NOT_UNDERWATER              = 0x00000100,   // 8    removed by leaving water
    AURA_INTERRUPT_FLAG_NOT_SHEATHED                = 0x00000200,   // 9    removed by unsheathing
    AURA_INTERRUPT_FLAG_UNK10                       = 0x00000400,   // 10
    AURA_INTERRUPT_FLAG_UNK11                       = 0x00000800,   // 11
    AURA_INTERRUPT_FLAG_UNK12                       = 0x00001000,   // 12   removed by attack?
    AURA_INTERRUPT_FLAG_UNK13                       = 0x00002000,   // 13
    AURA_INTERRUPT_FLAG_UNK14                       = 0x00004000,   // 14
    AURA_INTERRUPT_FLAG_UNK15                       = 0x00008000,   // 15   removed by casting a spell?
    AURA_INTERRUPT_FLAG_UNK16                       = 0x00010000,   // 16
    AURA_INTERRUPT_FLAG_MOUNTING                    = 0x00020000,   // 17   removed by mounting
    AURA_INTERRUPT_FLAG_NOT_SEATED                  = 0x00040000,   // 18   removed by standing up (used by food and drink mostly and sleep/Fake Death like)
    AURA_INTERRUPT_FLAG_CHANGE_MAP                  = 0x00080000,   // 19   leaving map/getting teleported
    AURA_INTERRUPT_FLAG_IMMUNE_OR_LOST_SELECTION    = 0x00100000,   // 20   removed by auras that make you invulnerable, or make other to loose selection on you
    AURA_INTERRUPT_FLAG_UNK21                       = 0x00200000,   // 21
    AURA_INTERRUPT_FLAG_UNK22                       = 0x00400000,   // 22
    AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT            = 0x00800000,   // 23   removed by entering pvp combat
    AURA_INTERRUPT_FLAG_DIRECT_DAMAGE               = 0x01000000    // 24   removed by any direct damage
};

enum SpellModOp
{
    SPELLMOD_DAMAGE                 = 0,
    SPELLMOD_DURATION               = 1,
    SPELLMOD_THREAT                 = 2,
    SPELLMOD_EFFECT1                = 3,
    SPELLMOD_CHARGES                = 4,
    SPELLMOD_RANGE                  = 5,
    SPELLMOD_RADIUS                 = 6,
    SPELLMOD_CRITICAL_CHANCE        = 7,
    SPELLMOD_ALL_EFFECTS            = 8,
    SPELLMOD_NOT_LOSE_CASTING_TIME  = 9,
    SPELLMOD_CASTING_TIME           = 10,
    SPELLMOD_COOLDOWN               = 11,
    SPELLMOD_EFFECT2                = 12,
    // spellmod 13 unused
    SPELLMOD_COST                   = 14,
    SPELLMOD_CRIT_DAMAGE_BONUS      = 15,
    SPELLMOD_RESIST_MISS_CHANCE     = 16,
    SPELLMOD_JUMP_TARGETS           = 17,
    SPELLMOD_CHANCE_OF_SUCCESS      = 18,                   // Only used with SPELL_AURA_ADD_FLAT_MODIFIER and affects proc spells
    SPELLMOD_ACTIVATION_TIME        = 19,
    SPELLMOD_EFFECT_PAST_FIRST      = 20,
    SPELLMOD_CASTING_TIME_OLD       = 21,
    SPELLMOD_DOT                    = 22,
    SPELLMOD_EFFECT3                = 23,
    SPELLMOD_SPELL_BONUS_DAMAGE     = 24,
    // spellmod 25 unused
    SPELLMOD_FREQUENCY_OF_SUCCESS   = 26,                   // Only used with SPELL_AURA_ADD_PCT_MODIFIER and affects used on proc spells
    SPELLMOD_MULTIPLE_VALUE         = 27,
    SPELLMOD_RESIST_DISPEL_CHANCE   = 28
};

#define MAX_SPELLMOD 32

enum SpellFacingFlags
{
    SPELL_FACING_FLAG_INFRONT = 0x0001
};

#define BASE_MINDAMAGE 1.0f
#define BASE_MAXDAMAGE 2.0f
#define BASE_ATTACK_TIME 2000

// byte value (UNIT_FIELD_BYTES_1,0)
enum UnitStandStateType
{
    UNIT_STAND_STATE_STAND             = 0,
    UNIT_STAND_STATE_SIT               = 1,
    UNIT_STAND_STATE_SIT_CHAIR         = 2,
    UNIT_STAND_STATE_SLEEP             = 3,
    UNIT_STAND_STATE_SIT_LOW_CHAIR     = 4,
    UNIT_STAND_STATE_SIT_MEDIUM_CHAIR  = 5,
    UNIT_STAND_STATE_SIT_HIGH_CHAIR    = 6,
    UNIT_STAND_STATE_DEAD              = 7,
    UNIT_STAND_STATE_KNEEL             = 8,
    UNIT_STAND_STATE_SUBMERGED         = 9
};

// byte flags value (UNIT_FIELD_BYTES_1,2)
enum UnitStandFlags
{
    UNIT_STAND_FLAGS_UNK1         = 0x01,
    UNIT_STAND_FLAGS_CREEP        = 0x02,
    UNIT_STAND_FLAGS_UNK3         = 0x04,
    UNIT_STAND_FLAGS_UNK4         = 0x08,
    UNIT_STAND_FLAGS_UNK5         = 0x10,
    UNIT_STAND_FLAGS_ALL          = 0xFF
};

// byte flags value (UNIT_FIELD_BYTES_1,3)
enum UnitBytes1_Flags
{
    UNIT_BYTE1_FLAG_ALWAYS_STAND = 0x01,
    UNIT_BYTE1_FLAG_UNK_2        = 0x02,
    UNIT_BYTE1_FLAG_UNTRACKABLE  = 0x04,
    UNIT_BYTE1_FLAG_ALL          = 0xFF
};

// byte value (UNIT_FIELD_BYTES_2,3)
enum ShapeshiftForm
{
    FORM_NONE               = 0x00,
    FORM_CAT                = 0x01,
    FORM_TREE               = 0x02,
    FORM_TRAVEL             = 0x03,
    FORM_AQUA               = 0x04,
    FORM_BEAR               = 0x05,
    FORM_AMBIENT            = 0x06,
    FORM_GHOUL              = 0x07,
    FORM_DIREBEAR           = 0x08,
    FORM_STEVES_GHOUL       = 0x09,
    FORM_THARONJA_SKELETON  = 0x0A,
    FORM_TEST_OF_STRENGTH   = 0x0B,
    FORM_BLB_PLAYER         = 0x0C,
    FORM_SHADOW_DANCE       = 0x0D,
    FORM_CREATUREBEAR       = 0x0E,
    FORM_CREATURECAT        = 0x0F,
    FORM_GHOSTWOLF          = 0x10,
    FORM_BATTLESTANCE       = 0x11,
    FORM_DEFENSIVESTANCE    = 0x12,
    FORM_BERSERKERSTANCE    = 0x13,
    FORM_TEST               = 0x14,
    FORM_ZOMBIE             = 0x15,
    FORM_METAMORPHOSIS      = 0x16,
    FORM_UNDEAD             = 0x19,
    FORM_FRENZY             = 0x1A,
    FORM_FLIGHT_EPIC        = 0x1B,
    FORM_SHADOW             = 0x1C,
    FORM_FLIGHT             = 0x1D,
    FORM_STEALTH            = 0x1E,
    FORM_MOONKIN            = 0x1F,
    FORM_SPIRITOFREDEMPTION = 0x20,
};

// byte value (UNIT_FIELD_BYTES_2,0)
enum SheathState
{
    SHEATH_STATE_UNARMED  = 0,                              // non prepared weapon
    SHEATH_STATE_MELEE    = 1,                              // prepared melee weapon
    SHEATH_STATE_RANGED   = 2                               // prepared ranged weapon
};

#define MAX_SHEATH_STATE    3

// byte flags value (UNIT_FIELD_BYTES_2,1)
enum UnitPVPStateFlags
{
    UNIT_BYTE2_FLAG_PVP         = 0x01,
    UNIT_BYTE2_FLAG_UNK1        = 0x02,
    UNIT_BYTE2_FLAG_FFA_PVP     = 0x04,
    UNIT_BYTE2_FLAG_SANCTUARY   = 0x08,
    UNIT_BYTE2_FLAG_UNK4        = 0x10,
    UNIT_BYTE2_FLAG_UNK5        = 0x20,
    UNIT_BYTE2_FLAG_UNK6        = 0x40,
    UNIT_BYTE2_FLAG_UNK7        = 0x80
};

// byte flags value (UNIT_FIELD_BYTES_2,2)
enum UnitRename
{
    UNIT_CAN_BE_RENAMED     = 0x01,
    UNIT_CAN_BE_ABANDONED   = 0x02,
};

#define CREATURE_MAX_SPELLS     4

enum Swing
{
    NOSWING                    = 0,
    SINGLEHANDEDSWING          = 1,
    TWOHANDEDSWING             = 2
};

enum VictimState
{
    VICTIMSTATE_UNKNOWN1       = 0,
    VICTIMSTATE_NORMAL         = 1,
    VICTIMSTATE_DODGE          = 2,
    VICTIMSTATE_PARRY          = 3,
    VICTIMSTATE_INTERRUPT      = 4,
    VICTIMSTATE_BLOCKS         = 5,
    VICTIMSTATE_EVADES         = 6,
    VICTIMSTATE_IS_IMMUNE      = 7,
    VICTIMSTATE_DEFLECTS       = 8
};

enum HitInfo
{
    HITINFO_NORMALSWING         = 0x00000000,
    HITINFO_UNK1                = 0x00000001,               // req correct packet structure
    HITINFO_NORMALSWING2        = 0x00000002,
    HITINFO_LEFTSWING           = 0x00000004,
    HITINFO_UNK2                = 0x00000008,
    HITINFO_MISS                = 0x00000010,
    HITINFO_ABSORB              = 0x00000020,               // absorbed damage
    HITINFO_ABSORB2             = 0x00000040,               // absorbed damage
    HITINFO_RESIST              = 0x00000080,               // resisted atleast some damage
    HITINFO_RESIST2             = 0x00000100,               // resisted atleast some damage
    HITINFO_CRITICALHIT         = 0x00000200,               // critical hit
    // 0x00000400
    // 0x00000800
    // 0x00001000
    HITINFO_BLOCK               = 0x00002000,               // blocked damage
    // 0x00004000
    // 0x00008000
    HITINFO_GLANCING            = 0x00010000,
    HITINFO_CRUSHING            = 0x00020000,
    HITINFO_NOACTION            = 0x00040000,               // guessed
    // 0x00080000
    // 0x00100000
    HITINFO_SWINGNOHITSOUND     = 0x00200000,               // guessed
    // 0x00400000
    HITINFO_UNK3                = 0x00800000
};

//i would like to remove this: (it is defined in item.h
enum InventorySlot
{
    NULL_BAG                   = 0,
    NULL_SLOT                  = 255
};

struct FactionTemplateEntry;
struct Modifier;
struct SpellEntry;
struct SpellEntryExt;

class Aura;
class Creature;
class Spell;
class DynamicObject;
class GameObject;
class Item;
class Pet;
class Path;
class PetAura;
class Totem;

struct SpellImmune
{
    uint32 type;
    uint32 spellId;
};

typedef std::list<SpellImmune> SpellImmuneList;

enum UnitModifierType
{
    BASE_VALUE = 0,
    BASE_PCT = 1,
    TOTAL_VALUE = 2,
    TOTAL_PCT = 3,
    MODIFIER_TYPE_END = 4
};

enum WeaponDamageRange
{
    MINDAMAGE,
    MAXDAMAGE
};

enum DamageTypeToSchool
{
    RESISTANCE,
    DAMAGE_DEALT,
    DAMAGE_TAKEN
};

enum AuraRemoveMode
{
    AURA_REMOVE_BY_DEFAULT,
    AURA_REMOVE_BY_STACK,                                   // at replace by similar aura
    AURA_REMOVE_BY_CANCEL,
    AURA_REMOVE_BY_DISPEL,
    AURA_REMOVE_BY_DEATH,
    AURA_REMOVE_BY_DELETE,                                  // use for speedup and prevent unexpected effects at player logout/pet unsummon (must be used _only_ after save), delete.
};

enum UnitMods
{
    UNIT_MOD_STAT_STRENGTH,                                 // UNIT_MOD_STAT_STRENGTH..UNIT_MOD_STAT_SPIRIT must be in existed order, it's accessed by index values of Stats enum.
    UNIT_MOD_STAT_AGILITY,
    UNIT_MOD_STAT_STAMINA,
    UNIT_MOD_STAT_INTELLECT,
    UNIT_MOD_STAT_SPIRIT,
    UNIT_MOD_HEALTH,
    UNIT_MOD_MANA,                                          // UNIT_MOD_MANA..UNIT_MOD_RUNIC_POWER must be in existed order, it's accessed by index values of Powers enum.
    UNIT_MOD_RAGE,
    UNIT_MOD_FOCUS,
    UNIT_MOD_ENERGY,
    UNIT_MOD_HAPPINESS,
    UNIT_MOD_RUNE,
    UNIT_MOD_RUNIC_POWER,
    UNIT_MOD_ARMOR,                                         // UNIT_MOD_ARMOR..UNIT_MOD_RESISTANCE_ARCANE must be in existed order, it's accessed by index values of SpellSchools enum.
    UNIT_MOD_RESISTANCE_HOLY,
    UNIT_MOD_RESISTANCE_FIRE,
    UNIT_MOD_RESISTANCE_NATURE,
    UNIT_MOD_RESISTANCE_FROST,
    UNIT_MOD_RESISTANCE_SHADOW,
    UNIT_MOD_RESISTANCE_ARCANE,
    UNIT_MOD_ATTACK_POWER,
    UNIT_MOD_ATTACK_POWER_RANGED,
    UNIT_MOD_DAMAGE_MAINHAND,
    UNIT_MOD_DAMAGE_OFFHAND,
    UNIT_MOD_DAMAGE_RANGED,
    UNIT_MOD_END,
    // synonyms
    UNIT_MOD_STAT_START = UNIT_MOD_STAT_STRENGTH,
    UNIT_MOD_STAT_END = UNIT_MOD_STAT_SPIRIT + 1,
    UNIT_MOD_RESISTANCE_START = UNIT_MOD_ARMOR,
    UNIT_MOD_RESISTANCE_END = UNIT_MOD_RESISTANCE_ARCANE + 1,
    UNIT_MOD_POWER_START = UNIT_MOD_MANA,
    UNIT_MOD_POWER_END = UNIT_MOD_RUNIC_POWER + 1
};

enum BaseModGroup
{
    CRIT_PERCENTAGE,
    RANGED_CRIT_PERCENTAGE,
    OFFHAND_CRIT_PERCENTAGE,
    SHIELD_BLOCK_VALUE,
    BASEMOD_END
};

enum BaseModType
{
    FLAT_MOD,
    PCT_MOD
};

#define MOD_END (PCT_MOD+1)

enum DeathState
{
    ALIVE       = 0,
    JUST_DIED   = 1,
    CORPSE      = 2,
    DEAD        = 3,
    JUST_ALIVED = 4,
    DEAD_FALLING= 5
};

// internal state flags for some auras and movement generators, other.
enum UnitState
{
    // persistent state (applied by aura/etc until expire)
    UNIT_STAT_MELEE_ATTACKING = 0x00000001,                     // unit is melee attacking someone Unit::Attack
    UNIT_STAT_ATTACK_PLAYER   = 0x00000002,                     // unit attack player or player's controlled unit and have contested pvpv timer setup, until timer expire, combat end and etc
    UNIT_STAT_DIED            = 0x00000004,                     // Unit::SetFeignDeath
    UNIT_STAT_STUNNED         = 0x00000008,                     // Aura::HandleAuraModStun
    UNIT_STAT_ROOT            = 0x00000010,                     // Aura::HandleAuraModRoot
    UNIT_STAT_ISOLATED        = 0x00000020,                     // area auras do not affect other players, Aura::HandleAuraModSchoolImmunity

    // persistent movement generator state (all time while movement generator applied to unit (independent from top state of movegen)
    UNIT_STAT_IN_FLIGHT       = 0x00000040,                     // player is in flight mode (in fact interrupted at far teleport until next map telport landing)
    UNIT_STAT_DISTRACTED      = 0x00000080,                     // DistractedMovementGenerator active

    // persistent movement generator state with non-persistent mirror states for stop support
    // (can be removed temporary by stop command or another movement generator apply)
    // not use _MOVE versions for generic movegen state, it can be removed temporary for unit stop and etc
    UNIT_STAT_CONFUSED        = 0x00000100,                     // ConfusedMovementGenerator active/onstack
    UNIT_STAT_CONFUSED_MOVE   = 0x00000200,
    UNIT_STAT_ROAMING         = 0x00000400,                     // RandomMovementGenerator/PointMovementGenerator/WaypointMovementGenerator active (now always set)
    UNIT_STAT_ROAMING_MOVE    = 0x00000800,
    UNIT_STAT_CHASE           = 0x00001000,                     // ChaseMovementGenerator active
    UNIT_STAT_CHASE_MOVE      = 0x00002000,
    UNIT_STAT_FOLLOW          = 0x00004000,                     // FollowMovementGenerator active
    UNIT_STAT_FOLLOW_MOVE     = 0x00008000,
    UNIT_STAT_FLEEING         = 0x00010000,                     // FleeMovementGenerator/TimedFleeingMovementGenerator active/onstack
    UNIT_STAT_FLEEING_MOVE    = 0x00020000,

    // masks (only for check)

    // can't move currently
    UNIT_STAT_CAN_NOT_MOVE    = UNIT_STAT_ROOT | UNIT_STAT_STUNNED | UNIT_STAT_DIED,

    // stay by different reasons
    UNIT_STAT_NOT_MOVE        = UNIT_STAT_ROOT | UNIT_STAT_STUNNED | UNIT_STAT_DIED |
                                UNIT_STAT_DISTRACTED,

    // stay or scripted movement for effect( = in player case you can't move by client command)
    UNIT_STAT_NO_FREE_MOVE    = UNIT_STAT_ROOT | UNIT_STAT_STUNNED | UNIT_STAT_DIED |
                                UNIT_STAT_IN_FLIGHT |
                                UNIT_STAT_CONFUSED | UNIT_STAT_FLEEING,

    // not react at move in sight or other
    UNIT_STAT_CAN_NOT_REACT   = UNIT_STAT_STUNNED | UNIT_STAT_DIED |
                                UNIT_STAT_CONFUSED | UNIT_STAT_FLEEING,

    // masks (for check or reset)

    // for real move using movegen check and stop (except unstoppable flight)
    UNIT_STAT_MOVING          = UNIT_STAT_ROAMING_MOVE | UNIT_STAT_CHASE_MOVE | UNIT_STAT_FOLLOW_MOVE | UNIT_STAT_FLEEING_MOVE,

    UNIT_STAT_ALL_STATE       = 0xFFFFFFFF
};

enum UnitMoveType
{
    MOVE_WALK           = 0,
    MOVE_RUN            = 1,
    MOVE_RUN_BACK       = 2,
    MOVE_SWIM           = 3,
    MOVE_SWIM_BACK      = 4,
    MOVE_TURN_RATE      = 5,
    MOVE_FLIGHT         = 6,
    MOVE_FLIGHT_BACK    = 7,
    MOVE_PITCH_RATE     = 8
};

#define MAX_MOVE_TYPE     9

extern float baseMoveSpeed[MAX_MOVE_TYPE];

enum CombatRating
{
    CR_WEAPON_SKILL             = 0,
    CR_DEFENSE_SKILL            = 1,
    CR_DODGE                    = 2,
    CR_PARRY                    = 3,
    CR_BLOCK                    = 4,
    CR_HIT_MELEE                = 5,
    CR_HIT_RANGED               = 6,
    CR_HIT_SPELL                = 7,
    CR_CRIT_MELEE               = 8,
    CR_CRIT_RANGED              = 9,
    CR_CRIT_SPELL               = 10,
    CR_HIT_TAKEN_MELEE          = 11,
    CR_HIT_TAKEN_RANGED         = 12,
    CR_HIT_TAKEN_SPELL          = 13,
    CR_CRIT_TAKEN_MELEE         = 14,
    CR_CRIT_TAKEN_RANGED        = 15,
    CR_CRIT_TAKEN_SPELL         = 16,
    CR_HASTE_MELEE              = 17,
    CR_HASTE_RANGED             = 18,
    CR_HASTE_SPELL              = 19,
    CR_WEAPON_SKILL_MAINHAND    = 20,
    CR_WEAPON_SKILL_OFFHAND     = 21,
    CR_WEAPON_SKILL_RANGED      = 22,
    CR_EXPERTISE                = 23,
    CR_ARMOR_PENETRATION        = 24
};

#define MAX_COMBAT_RATING         25

enum DamageEffectType
{
    DIRECT_DAMAGE           = 0,                            // used for normal weapon damage (not for class abilities or spells)
    SPELL_DIRECT_DAMAGE     = 1,                            // spell/class abilities damage
    DOT                     = 2,
    HEAL                    = 3,
    NODAMAGE                = 4,                            // used also in case when damage applied to health but not applied to spell channelInterruptFlags/etc
    SELF_DAMAGE             = 5
};

/// internal used flags for marking special auras - for example some dummy-auras
enum UnitAuraFlags
{
    UNIT_AURAFLAG_ALIVE_INVISIBLE   = 0x1,                  // aura which makes unit invisible for alive
};

enum UnitVisibility
{
    VISIBILITY_OFF                = 0,                      // absolute, not detectable, GM-like, can see all other
    VISIBILITY_ON                 = 1,
    VISIBILITY_GROUP_STEALTH      = 2,                      // detect chance, seen and can see group members
    VISIBILITY_GROUP_INVISIBILITY = 3,                      // invisibility, can see and can be seen only another invisible unit or invisible detection unit, set only if not stealthed, and in checks not used (mask used instead)
    VISIBILITY_GROUP_NO_DETECT    = 4,                      // state just at stealth apply for update Grid state. Don't remove, otherwise stealth spells will break
    VISIBILITY_RESPAWN            = 5                       // special totally not detectable visibility for force delete object at respawn command
};

// Value masks for UNIT_FIELD_FLAGS
enum UnitFlags
{
    UNIT_FLAG_UNK_0                 = 0x00000001,
    UNIT_FLAG_NON_ATTACKABLE        = 0x00000002,           // not attackable
    UNIT_FLAG_DISABLE_MOVE          = 0x00000004,
    UNIT_FLAG_PVP_ATTACKABLE        = 0x00000008,           // allow apply pvp rules to attackable state in addition to faction dependent state
    UNIT_FLAG_RENAME                = 0x00000010,
    UNIT_FLAG_PREPARATION           = 0x00000020,           // don't take reagents for spells with SPELL_ATTR_EX5_NO_REAGENT_WHILE_PREP
    UNIT_FLAG_UNK_6                 = 0x00000040,
    UNIT_FLAG_NOT_ATTACKABLE_1      = 0x00000080,           // ?? (UNIT_FLAG_PVP_ATTACKABLE | UNIT_FLAG_NOT_ATTACKABLE_1) is NON_PVP_ATTACKABLE
    UNIT_FLAG_OOC_NOT_ATTACKABLE    = 0x00000100,           // 2.0.8 - (OOC Out Of Combat) Can not be attacked when not in combat. Removed if unit for some reason enter combat.
    UNIT_FLAG_UNK_9                 = 0x00000200,           // 3.0.3 - makes you unable to attack everything
    UNIT_FLAG_LOOTING               = 0x00000400,           // loot animation
    UNIT_FLAG_PET_IN_COMBAT         = 0x00000800,           // in combat?, 2.0.8
    UNIT_FLAG_PVP                   = 0x00001000,           // changed in 3.0.3
    UNIT_FLAG_SILENCED              = 0x00002000,           // silenced, 2.1.1
    UNIT_FLAG_UNK_14                = 0x00004000,           // 2.0.8
    UNIT_FLAG_UNK_15                = 0x00008000,
    UNIT_FLAG_UNK_16                = 0x00010000,           // removes attackable icon
    UNIT_FLAG_PACIFIED              = 0x00020000,           // 3.0.3 ok
    UNIT_FLAG_STUNNED               = 0x00040000,           // 3.0.3 ok
    UNIT_FLAG_IN_COMBAT             = 0x00080000,
    UNIT_FLAG_TAXI_FLIGHT           = 0x00100000,           // disable casting at client side spell not allowed by taxi flight (mounted?), probably used with 0x4 flag
    UNIT_FLAG_DISARMED              = 0x00200000,           // 3.0.3, disable melee spells casting..., "Required melee weapon" added to melee spells tooltip.
    UNIT_FLAG_CONFUSED              = 0x00400000,
    UNIT_FLAG_FLEEING               = 0x00800000,
    UNIT_FLAG_PLAYER_CONTROLLED     = 0x01000000,           // used in spell Eyes of the Beast for pet... let attack by controlled creature
    UNIT_FLAG_NOT_SELECTABLE        = 0x02000000,
    UNIT_FLAG_SKINNABLE             = 0x04000000,
    UNIT_FLAG_MOUNT                 = 0x08000000,
    UNIT_FLAG_UNK_28                = 0x10000000,
    UNIT_FLAG_UNK_29                = 0x20000000,           // used in Feing Death spell
    UNIT_FLAG_SHEATHE               = 0x40000000,
    UNIT_FLAG_UNK_31                = 0x80000000            // set skinnable icon and also changes color of portrait
};

// Value masks for UNIT_FIELD_FLAGS_2
enum UnitFlags2
{
    UNIT_FLAG2_FEIGN_DEATH      = 0x00000001,
    UNIT_FLAG2_UNK1             = 0x00000002,               // Hide unit model (show only player equip)
    UNIT_FLAG2_COMPREHEND_LANG  = 0x00000008,
    UNIT_FLAG2_FORCE_MOVE       = 0x00000040,
    UNIT_FLAG2_REGENERATE_POWER = 0x00000800
};

/// Non Player Character flags
enum NPCFlags
{
    UNIT_NPC_FLAG_NONE                  = 0x00000000,
    UNIT_NPC_FLAG_GOSSIP                = 0x00000001,       // 100%
    UNIT_NPC_FLAG_QUESTGIVER            = 0x00000002,       // guessed, probably ok
    UNIT_NPC_FLAG_UNK1                  = 0x00000004,
    UNIT_NPC_FLAG_UNK2                  = 0x00000008,
    UNIT_NPC_FLAG_TRAINER               = 0x00000010,       // 100%
    UNIT_NPC_FLAG_TRAINER_CLASS         = 0x00000020,       // 100%
    UNIT_NPC_FLAG_TRAINER_PROFESSION    = 0x00000040,       // 100%
    UNIT_NPC_FLAG_VENDOR                = 0x00000080,       // 100%
    UNIT_NPC_FLAG_VENDOR_AMMO           = 0x00000100,       // 100%, general goods vendor
    UNIT_NPC_FLAG_VENDOR_FOOD           = 0x00000200,       // 100%
    UNIT_NPC_FLAG_VENDOR_POISON         = 0x00000400,       // guessed
    UNIT_NPC_FLAG_VENDOR_REAGENT        = 0x00000800,       // 100%
    UNIT_NPC_FLAG_REPAIR                = 0x00001000,       // 100%
    UNIT_NPC_FLAG_FLIGHTMASTER          = 0x00002000,       // 100%
    UNIT_NPC_FLAG_SPIRITHEALER          = 0x00004000,       // guessed
    UNIT_NPC_FLAG_SPIRITGUIDE           = 0x00008000,       // guessed
    UNIT_NPC_FLAG_INNKEEPER             = 0x00010000,       // 100%
    UNIT_NPC_FLAG_BANKER                = 0x00020000,       // 100%
    UNIT_NPC_FLAG_PETITIONER            = 0x00040000,       // 100% 0xC0000 = guild petitions, 0x40000 = arena team petitions
    UNIT_NPC_FLAG_TABARDDESIGNER        = 0x00080000,       // 100%
    UNIT_NPC_FLAG_BATTLEMASTER          = 0x00100000,       // 100%
    UNIT_NPC_FLAG_AUCTIONEER            = 0x00200000,       // 100%
    UNIT_NPC_FLAG_STABLEMASTER          = 0x00400000,       // 100%
    UNIT_NPC_FLAG_GUILD_BANKER          = 0x00800000,       // cause client to send 997 opcode
    UNIT_NPC_FLAG_SPELLCLICK            = 0x01000000,       // cause client to send 1015 opcode (spell click), dynamic, set at loading and don't must be set in DB
    UNIT_NPC_FLAG_GUARD                 = 0x10000000        // custom flag for guards
};

// used in most movement packets (send and received)
enum MovementFlags
{
    MOVEFLAG_NONE               = 0x00000000,
    MOVEFLAG_FORWARD            = 0x00000001,
    MOVEFLAG_BACKWARD           = 0x00000002,
    MOVEFLAG_STRAFE_LEFT        = 0x00000004,
    MOVEFLAG_STRAFE_RIGHT       = 0x00000008,
    MOVEFLAG_TURN_LEFT          = 0x00000010,
    MOVEFLAG_TURN_RIGHT         = 0x00000020,
    MOVEFLAG_PITCH_UP           = 0x00000040,
    MOVEFLAG_PITCH_DOWN         = 0x00000080,
    MOVEFLAG_WALK_MODE          = 0x00000100,               // Walking
    MOVEFLAG_ONTRANSPORT        = 0x00000200,
    MOVEFLAG_LEVITATING         = 0x00000400,
    MOVEFLAG_ROOT               = 0x00000800,
    MOVEFLAG_FALLING            = 0x00001000,
    MOVEFLAG_FALLINGFAR         = 0x00002000,
    MOVEFLAG_PENDINGSTOP        = 0x00004000,
    MOVEFLAG_PENDINGSTRAFESTOP  = 0x00008000,
    MOVEFLAG_PENDINGFORWARD     = 0x00010000,
    MOVEFLAG_PENDINGBACKWARD    = 0x00020000,
    MOVEFLAG_PENDINGSTRAFELEFT  = 0x00040000,
    MOVEFLAG_PENDINGSTRAFERIGHT = 0x00080000,
    MOVEFLAG_PENDINGROOT        = 0x00100000,
    MOVEFLAG_SWIMMING           = 0x00200000,               // appears with fly flag also
    MOVEFLAG_ASCENDING          = 0x00400000,               // swim up also
    MOVEFLAG_DESCENDING         = 0x00800000,               // swim down also
    MOVEFLAG_CAN_FLY            = 0x01000000,               // can fly in 3.3?
    MOVEFLAG_FLYING             = 0x02000000,               // Actual flying mode
    MOVEFLAG_SPLINE_ELEVATION   = 0x04000000,               // used for flight paths
    MOVEFLAG_SPLINE_ENABLED     = 0x08000000,               // used for flight paths
    MOVEFLAG_WATERWALKING       = 0x10000000,               // prevent unit from falling through water
    MOVEFLAG_SAFE_FALL          = 0x20000000,               // active rogue safe fall spell (passive)
    MOVEFLAG_HOVER              = 0x40000000
};

// flags that use in movement check for example at spell casting
MovementFlags const movementFlagsMask = MovementFlags(
    MOVEFLAG_FORWARD |MOVEFLAG_BACKWARD  |MOVEFLAG_STRAFE_LEFT |MOVEFLAG_STRAFE_RIGHT|
    MOVEFLAG_PITCH_UP|MOVEFLAG_PITCH_DOWN|MOVEFLAG_ROOT        |
    MOVEFLAG_FALLING |MOVEFLAG_FALLINGFAR|MOVEFLAG_ASCENDING   |
    MOVEFLAG_FLYING  |MOVEFLAG_SPLINE_ELEVATION
);

MovementFlags const movementOrTurningFlagsMask = MovementFlags(
    movementFlagsMask | MOVEFLAG_TURN_LEFT | MOVEFLAG_TURN_RIGHT
);

enum MovementFlags2
{
    MOVEFLAG2_NONE              = 0x0000,
    MOVEFLAG2_UNK1              = 0x0001,
    MOVEFLAG2_UNK2              = 0x0002,
    MOVEFLAG2_UNK3              = 0x0004,
    MOVEFLAG2_FULLSPEEDTURNING  = 0x0008,
    MOVEFLAG2_FULLSPEEDPITCHING = 0x0010,
    MOVEFLAG2_ALLOW_PITCHING    = 0x0020,
    MOVEFLAG2_UNK4              = 0x0040,
    MOVEFLAG2_UNK5              = 0x0080,
    MOVEFLAG2_UNK6              = 0x0100,
    MOVEFLAG2_UNK7              = 0x0200,
    MOVEFLAG2_INTERP_MOVEMENT   = 0x0400,
    MOVEFLAG2_INTERP_TURNING    = 0x0800,
    MOVEFLAG2_INTERP_PITCHING   = 0x1000,
    MOVEFLAG2_UNK8              = 0x2000,
    MOVEFLAG2_UNK9              = 0x4000,
    MOVEFLAG2_UNK10             = 0x8000,
    MOVEFLAG2_INTERP_MASK       = MOVEFLAG2_INTERP_MOVEMENT | MOVEFLAG2_INTERP_TURNING | MOVEFLAG2_INTERP_PITCHING
};

enum SplineFlags
{
    SPLINEFLAG_NONE         = 0x00000000,
    SPLINEFLAG_FORWARD      = 0x00000001,
    SPLINEFLAG_BACKWARD     = 0x00000002,
    SPLINEFLAG_STRAFE_LEFT  = 0x00000004,
    SPLINEFLAG_STRAFE_RIGHT = 0x00000008,
    SPLINEFLAG_LEFT         = 0x00000010,
    SPLINEFLAG_RIGHT        = 0x00000020,
    SPLINEFLAG_PITCH_UP     = 0x00000040,
    SPLINEFLAG_PITCH_DOWN   = 0x00000080,
    SPLINEFLAG_DONE         = 0x00000100,
    SPLINEFLAG_FALLING      = 0x00000200,
    SPLINEFLAG_NO_SPLINE    = 0x00000400,
    SPLINEFLAG_TRAJECTORY   = 0x00000800,
    SPLINEFLAG_WALKMODE     = 0x00001000,
    SPLINEFLAG_FLYING       = 0x00002000,
    SPLINEFLAG_KNOCKBACK    = 0x00004000,
    SPLINEFLAG_FINALPOINT   = 0x00008000,
    SPLINEFLAG_FINALTARGET  = 0x00010000,
    SPLINEFLAG_FINALFACING  = 0x00020000,
    SPLINEFLAG_CATMULLROM   = 0x00040000,
    SPLINEFLAG_UNKNOWN1     = 0x00080000,
    SPLINEFLAG_UNKNOWN2     = 0x00100000,
    SPLINEFLAG_UNKNOWN3     = 0x00200000,
    SPLINEFLAG_UNKNOWN4     = 0x00400000,
    SPLINEFLAG_UNKNOWN5     = 0x00800000,
    SPLINEFLAG_UNKNOWN6     = 0x01000000,
    SPLINEFLAG_UNKNOWN7     = 0x02000000,
    SPLINEFLAG_UNKNOWN8     = 0x04000000,
    SPLINEFLAG_UNKNOWN9     = 0x08000000,
    SPLINEFLAG_UNKNOWN10    = 0x10000000,
    SPLINEFLAG_UNKNOWN11    = 0x20000000,
    SPLINEFLAG_UNKNOWN12    = 0x40000000
};

enum SplineMode
{
    SPLINEMODE_LINEAR       = 0,
    SPLINEMODE_CATMULLROM   = 1,
    SPLINEMODE_BEZIER3      = 2
};

enum SplineType
{
    SPLINETYPE_NORMAL       = 0,
    SPLINETYPE_STOP         = 1,
    SPLINETYPE_FACINGSPOT   = 2,
    SPLINETYPE_FACINGTARGET = 3,
    SPLINETYPE_FACINGANGLE  = 4
};

struct Position
{
    Position() : x(0.0f), y(0.0f), z(0.0f), o(0.0f) {}
    float x, y, z, o;
};

class MovementInfo
{
    public:
        MovementInfo() : moveFlags(MOVEFLAG_NONE), moveFlags2(MOVEFLAG2_NONE), time(0), t_guid(0),
            t_time(0), t_seat(-1), t_time2(0), s_pitch(0.0f), fallTime(0), j_velocity(0.0f), j_sinAngle(0.0f),
            j_cosAngle(0.0f), j_xyspeed(0.0f), u_unk1(0.0f) {}

        MovementInfo(WorldPacket &data);

        // Read/Write methods
        void Read(ByteBuffer &data);
        void Write(ByteBuffer &data);

        // Movement flags manipulations
        void AddMovementFlag(MovementFlags f) { moveFlags |= f; }
        void RemoveMovementFlag(MovementFlags f) { moveFlags &= ~f; }
        bool HasMovementFlag(MovementFlags f) const { return moveFlags & f; }
        MovementFlags GetMovementFlags() const { return MovementFlags(moveFlags); }
        void SetMovementFlags(MovementFlags f) { moveFlags = f; }
        MovementFlags2 GetMovementFlags2() const { return MovementFlags2(moveFlags2); }

        // Position manipulations
        Position const *GetPos() const { return &pos; }
        void SetTransportData(uint64 guid, float x, float y, float z, float o, uint32 time, int8 seat)
        {
            t_guid = guid;
            t_pos.x = x;
            t_pos.y = y;
            t_pos.z = z;
            t_pos.o = o;
            t_time = time;
            t_seat = seat;
        }
        uint64 GetTransportGuid() const { return t_guid; }
        Position const *GetTransportPos() const { return &t_pos; }
        int8 GetTransportSeat() const { return t_seat; }
        uint32 GetTransportTime() const { return t_time; }
        uint32 GetFallTime() const { return fallTime; }
        void ChangePosition(float x, float y, float z, float o) { pos.x = x; pos.y = y; pos.z = z; pos.o = o; }
        void UpdateTime(uint32 _time) { time = _time; }

    private:
        // common
        uint32   moveFlags;                                 // see enum MovementFlags
        uint16   moveFlags2;                                // see enum MovementFlags2
        uint32   time;
        Position pos;
        // transport
        uint64   t_guid;
        Position t_pos;
        uint32   t_time;
        int8     t_seat;
        uint32   t_time2;
        // swimming and flying
        float    s_pitch;
        // last fall time
        uint32   fallTime;
        // jumping
        float    j_velocity, j_sinAngle, j_cosAngle, j_xyspeed;
        // spline
        float    u_unk1;
};

enum DiminishingLevels
{
    DIMINISHING_LEVEL_1             = 0,
    DIMINISHING_LEVEL_2             = 1,
    DIMINISHING_LEVEL_3             = 2,
    DIMINISHING_LEVEL_IMMUNE        = 3
};

struct DiminishingReturn
{
    DiminishingReturn(DiminishingGroup group, uint32 t, uint32 count)
        : DRGroup(group), stack(0), hitTime(t), hitCount(count)
    {}

    DiminishingGroup        DRGroup:16;
    uint16                  stack:16;
    uint32                  hitTime;
    uint32                  hitCount;
};

enum MeleeHitOutcome
{
    MELEE_HIT_EVADE, MELEE_HIT_MISS, MELEE_HIT_DODGE, MELEE_HIT_BLOCK, MELEE_HIT_PARRY,
    MELEE_HIT_GLANCING, MELEE_HIT_CRIT, MELEE_HIT_CRUSHING, MELEE_HIT_NORMAL
};

struct CleanDamage
{
    CleanDamage(uint32 _damage, WeaponAttackType _attackType, MeleeHitOutcome _hitOutCome) :
    damage(_damage), attackType(_attackType), hitOutCome(_hitOutCome) {}

    uint32 damage;
    WeaponAttackType attackType;
    MeleeHitOutcome hitOutCome;
};

// Struct for use in Unit::CalculateMeleeDamage
// Need create structure like in SMSG_ATTACKERSTATEUPDATE opcode
struct CalcDamageInfo
{
    Unit  *attacker;             // Attacker
    Unit  *target;               // Target for damage
    SpellSchoolMask damageSchoolMask;
    uint32 damage;
    uint32 absorb;
    uint32 resist;
    uint32 blocked_amount;
    uint32 HitInfo;
    uint32 TargetState;
// Helper
    WeaponAttackType attackType; //
    uint32 procAttacker;
    uint32 procVictim;
    uint32 procEx;
    uint32 cleanDamage;          // Used only for rage calculation
    MeleeHitOutcome hitOutCome;  // TODO: remove this field (need use TargetState)
};

// Spell damage info structure based on structure sending in SMSG_SPELLNONMELEEDAMAGELOG opcode
struct SpellNonMeleeDamage{
    SpellNonMeleeDamage(Unit *_attacker, Unit *_target, uint32 _SpellID, uint32 _schoolMask)
        : target(_target), attacker(_attacker), SpellID(_SpellID), damage(0), overkill(0), schoolMask(_schoolMask),
        absorb(0), resist(0), physicalLog(false), unused(false), blocked(0), HitInfo(0), cleanDamage(0)
    {}

    Unit   *target;
    Unit   *attacker;
    uint32 SpellID;
    uint32 damage;
    uint32 overkill;
    uint32 schoolMask;
    uint32 absorb;
    uint32 resist;
    bool   physicalLog;
    bool   unused;
    uint32 blocked;
    uint32 HitInfo;
    // Used for help
    uint32 cleanDamage;
};

struct SpellPeriodicAuraLogInfo
{
    SpellPeriodicAuraLogInfo(Aura *_aura, uint32 _damage, uint32 _overDamage, uint32 _absorb, uint32 _resist, float _multiplier, bool _critical = false)
        : aura(_aura), damage(_damage), overDamage(_overDamage), absorb(_absorb), resist(_resist), multiplier(_multiplier), critical(_critical) {}

    Aura   *aura;
    uint32 damage;
    uint32 overDamage;                                      // overkill/overheal
    uint32 absorb;
    uint32 resist;
    float  multiplier;
    bool   critical;
};

uint32 createProcExtendMask(SpellNonMeleeDamage *damageInfo, SpellMissInfo missCondition);

#define MAX_DECLINED_NAME_CASES 5

struct DeclinedName
{
    std::string name[MAX_DECLINED_NAME_CASES];
};

enum CurrentSpellTypes
{
    CURRENT_MELEE_SPELL             = 0,
    CURRENT_GENERIC_SPELL           = 1,
    CURRENT_AUTOREPEAT_SPELL        = 2,
    CURRENT_CHANNELED_SPELL         = 3
};

#define CURRENT_FIRST_NON_MELEE_SPELL 1
#define CURRENT_MAX_SPELL             4


enum ActiveStates
{
    ACT_PASSIVE  = 0x01,                                    // 0x01 - passive
    ACT_DISABLED = 0x81,                                    // 0x80 - castable
    ACT_ENABLED  = 0xC1,                                    // 0x40 | 0x80 - auto cast + castable
    ACT_COMMAND  = 0x07,                                    // 0x01 | 0x02 | 0x04
    ACT_REACTION = 0x06,                                    // 0x02 | 0x04
    ACT_DECIDE   = 0x00                                     // custom
};

enum ReactStates
{
    REACT_PASSIVE    = 0,
    REACT_DEFENSIVE  = 1,
    REACT_AGGRESSIVE = 2
};

enum CommandStates
{
    COMMAND_STAY    = 0,
    COMMAND_FOLLOW  = 1,
    COMMAND_ATTACK  = 2,
    COMMAND_ABANDON = 3
};

#define UNIT_ACTION_BUTTON_ACTION(X) (uint32(X) & 0x00FFFFFF)
#define UNIT_ACTION_BUTTON_TYPE(X)   ((uint32(X) & 0xFF000000) >> 24)
#define MAX_UNIT_ACTION_BUTTON_ACTION_VALUE (0x00FFFFFF+1)
#define MAKE_UNIT_ACTION_BUTTON(A,T) (uint32(A) | (uint32(T) << 24))

struct UnitActionBarEntry
{
    UnitActionBarEntry() : packedData(uint32(ACT_DISABLED) << 24) {}

    uint32 packedData;

    // helper
    ActiveStates GetType() const { return ActiveStates(UNIT_ACTION_BUTTON_TYPE(packedData)); }
    uint32 GetAction() const { return UNIT_ACTION_BUTTON_ACTION(packedData); }
    bool IsActionBarForSpell() const
    {
        ActiveStates Type = GetType();
        return Type == ACT_DISABLED || Type == ACT_ENABLED || Type == ACT_PASSIVE;
    }

    void SetActionAndType(uint32 action, ActiveStates type)
    {
        packedData = MAKE_UNIT_ACTION_BUTTON(action,type);
    }

    void SetType(ActiveStates type)
    {
        packedData = MAKE_UNIT_ACTION_BUTTON(UNIT_ACTION_BUTTON_ACTION(packedData),type);
    }

    void SetAction(uint32 action)
    {
        packedData = (packedData & 0xFF000000) | UNIT_ACTION_BUTTON_ACTION(action);
    }
};

typedef UnitActionBarEntry CharmSpellEntry;

enum ActionBarIndex
{
    ACTION_BAR_INDEX_START = 0,
    ACTION_BAR_INDEX_PET_SPELL_START = 3,
    ACTION_BAR_INDEX_PET_SPELL_END = 7,
    ACTION_BAR_INDEX_END = 10,
};

#define MAX_UNIT_ACTION_BAR_INDEX (ACTION_BAR_INDEX_END-ACTION_BAR_INDEX_START)

struct CharmInfo
{
    public:
        explicit CharmInfo(Unit* unit);
        uint32 GetPetNumber() const { return m_petnumber; }
        void SetPetNumber(uint32 petnumber, bool statwindow);

        void SetCommandState(CommandStates st) { m_CommandState = st; }
        CommandStates GetCommandState() { return m_CommandState; }
        bool HasCommandState(CommandStates state) { return (m_CommandState == state); }
        void SetReactState(ReactStates st) { m_reactState = st; }
        ReactStates GetReactState() { return m_reactState; }
        bool HasReactState(ReactStates state) { return (m_reactState == state); }

        void InitPossessCreateSpells();
        void InitCharmCreateSpells();
        void InitPetActionBar();
        void InitEmptyActionBar();

                                                            //return true if successful
        bool AddSpellToActionBar(uint32 spellid, ActiveStates newstate = ACT_DECIDE);
        bool RemoveSpellFromActionBar(uint32 spell_id);
        void LoadPetActionBar(const std::string& data);
        void BuildActionBar(WorldPacket* data);
        void SetSpellAutocast(uint32 spell_id, bool state);
        void SetActionBar(uint8 index, uint32 spellOrAction,ActiveStates type)
        {
            PetActionBar[index].SetActionAndType(spellOrAction,type);
        }
        UnitActionBarEntry const* GetActionBarEntry(uint8 index) const { return &(PetActionBar[index]); }

        void ToggleCreatureAutocast(uint32 spellid, bool apply);

        CharmSpellEntry* GetCharmSpell(uint8 index) { return &(m_charmspells[index]); }
    private:

        Unit* m_unit;
        UnitActionBarEntry PetActionBar[MAX_UNIT_ACTION_BAR_INDEX];
        CharmSpellEntry m_charmspells[CREATURE_MAX_SPELLS];
        CommandStates   m_CommandState;
        ReactStates     m_reactState;
        uint32          m_petnumber;
};

// for clearing special attacks
#define REACTIVE_TIMER_START 4000

enum ReactiveType
{
    REACTIVE_DEFENSE      = 0,
    REACTIVE_HUNTER_PARRY = 1,
    REACTIVE_OVERPOWER    = 2
};

#define MAX_REACTIVE 3
#define MAX_TOTEM 4

typedef std::set<uint64> GuardianPetList;

// delay time next attack to prevent client attack animation problems
#define ATTACK_DISPLAY_DELAY 200
#define MAX_PLAYER_STEALTH_DETECT_RANGE 45.0f               // max distance for detection targets by player
#define MAX_CREATURE_ATTACK_RADIUS 45.0f                    // max distance for creature aggro (use with CONFIG_FLOAT_RATE_CREATURE_AGGRO)

// Regeneration defines
#define REGEN_TIME_FULL     2000                            // For this time difference is computed regen value
#define REGEN_TIME_PRECISE  500                             // Used in Spell::CheckPower for precise regeneration in spell cast time

struct SpellProcEventEntry;                                 // used only privately

class MANGOS_DLL_SPEC Unit : public WorldObject
{
    public:
        typedef std::set<Unit*> AttackerSet;
        typedef std::pair<uint32, SpellEffectIndex> spellEffectPair;
        typedef std::multimap< spellEffectPair, Aura*> AuraMap;
        typedef std::list<Aura *> AuraList;
        typedef std::list<DiminishingReturn> Diminishing;
        typedef std::set<uint32> ComboPointHolderSet;
        typedef std::map<uint8, uint32> VisibleAuraMap;

        virtual ~Unit ( );

        void AddToWorld();
        void RemoveFromWorld();

        void CleanupsBeforeDelete();                        // used in ~Creature/~Player (or before mass creature delete to remove cross-references to already deleted units)

        DiminishingLevels GetDiminishing(DiminishingGroup  group);
        void IncrDiminishing(DiminishingGroup group);
        void ApplyDiminishingToDuration(DiminishingGroup  group, int32 &duration,Unit* caster, DiminishingLevels Level, int32 limitduration);
        void ApplyDiminishingAura(DiminishingGroup  group, bool apply);
        void ClearDiminishings() { m_Diminishing.clear(); }

        virtual void Update( uint32 time );

        void setAttackTimer(WeaponAttackType type, uint32 time) { m_attackTimer[type] = time; }
        void resetAttackTimer(WeaponAttackType type = BASE_ATTACK);
        uint32 getAttackTimer(WeaponAttackType type) const { return m_attackTimer[type]; }
        bool isAttackReady(WeaponAttackType type = BASE_ATTACK) const { return m_attackTimer[type] == 0; }
        bool haveOffhandWeapon() const;
        bool canReachWithAttack(Unit *pVictim) const;
        uint32 m_extraAttacks;

        void _addAttacker(Unit *pAttacker)                  // must be called only from Unit::Attack(Unit*)
        {
            AttackerSet::const_iterator itr = m_attackers.find(pAttacker);
            if(itr == m_attackers.end())
                m_attackers.insert(pAttacker);
        }
        void _removeAttacker(Unit *pAttacker)               // must be called only from Unit::AttackStop()
        {
            m_attackers.erase(pAttacker);
        }
        Unit * getAttackerForHelper()                       // If someone wants to help, who to give them
        {
            if (getVictim() != NULL)
                return getVictim();

            if (!m_attackers.empty())
                return *(m_attackers.begin());

            return NULL;
        }
        bool Attack(Unit *victim, bool meleeAttack);
        void CastStop(uint32 except_spellid = 0);
        bool AttackStop(bool targetSwitch = false);
        void RemoveAllAttackers();
        AttackerSet const& getAttackers() const { return m_attackers; }
        bool isAttackingPlayer() const;
        Unit* getVictim() const { return m_attacking; }
        void CombatStop(bool includingCast = false);
        void CombatStopWithPets(bool includingCast = false);
        void StopAttackFaction(uint32 faction_id);
        Unit* SelectRandomUnfriendlyTarget(Unit* except = NULL, float radius = ATTACK_DISTANCE) const;
        Unit* SelectRandomFriendlyTarget(Unit* except = NULL, float radius = ATTACK_DISTANCE) const;
        bool hasNegativeAuraWithInterruptFlag(uint32 flag);
        void SendMeleeAttackStop(Unit* victim);
        void SendMeleeAttackStart(Unit* pVictim);

        void addUnitState(uint32 f) { m_state |= f; }
        bool hasUnitState(uint32 f) const { return (m_state & f); }
        void clearUnitState(uint32 f) { m_state &= ~f; }
        bool CanFreeMove() const
        {
            return !hasUnitState(UNIT_STAT_NO_FREE_MOVE) && GetOwnerGUID()==0;
        }

        uint32 getLevel() const { return GetUInt32Value(UNIT_FIELD_LEVEL); }
        virtual uint32 getLevelForTarget(Unit const* /*target*/) const { return getLevel(); }
        void SetLevel(uint32 lvl);
        uint8 getRace() const { return GetByteValue(UNIT_FIELD_BYTES_0, 0); }
        uint32 getRaceMask() const { return 1 << (getRace()-1); }
        uint8 getClass() const { return GetByteValue(UNIT_FIELD_BYTES_0, 1); }
        uint32 getClassMask() const { return 1 << (getClass()-1); }
        uint8 getGender() const { return GetByteValue(UNIT_FIELD_BYTES_0, 2); }

        float GetStat(Stats stat) const { return float(GetUInt32Value(UNIT_FIELD_STAT0+stat)); }
        void SetStat(Stats stat, int32 val) { SetStatInt32Value(UNIT_FIELD_STAT0+stat, val); }
        uint32 GetArmor() const { return GetResistance(SPELL_SCHOOL_NORMAL) ; }
        void SetArmor(int32 val) { SetResistance(SPELL_SCHOOL_NORMAL, val); }

        uint32 GetResistance(SpellSchools school) const { return GetUInt32Value(UNIT_FIELD_RESISTANCES+school); }
        void SetResistance(SpellSchools school, int32 val) { SetStatInt32Value(UNIT_FIELD_RESISTANCES+school,val); }

        uint32 GetHealth()    const { return GetUInt32Value(UNIT_FIELD_HEALTH); }
        uint32 GetMaxHealth() const { return GetUInt32Value(UNIT_FIELD_MAXHEALTH); }
        float GetHealthPercent() const { return (GetHealth()*100.0f) / GetMaxHealth(); }
        void SetHealth(   uint32 val);
        void SetMaxHealth(uint32 val);
        void SetHealthPercent(float percent);
        int32 ModifyHealth(int32 val);

        Powers getPowerType() const { return Powers(GetByteValue(UNIT_FIELD_BYTES_0, 3)); }
        void setPowerType(Powers power);
        uint32 GetPower(   Powers power) const { return GetUInt32Value(UNIT_FIELD_POWER1   +power); }
        uint32 GetMaxPower(Powers power) const { return GetUInt32Value(UNIT_FIELD_MAXPOWER1+power); }
        void SetPower(   Powers power, uint32 val);
        void SetMaxPower(Powers power, uint32 val);
        int32 ModifyPower(Powers power, int32 val);
        void ApplyPowerMod(Powers power, uint32 val, bool apply);
        void ApplyMaxPowerMod(Powers power, uint32 val, bool apply);

        uint32 GetAttackTime(WeaponAttackType att) const { return (uint32)(GetFloatValue(UNIT_FIELD_BASEATTACKTIME+att)/m_modAttackSpeedPct[att]); }
        void SetAttackTime(WeaponAttackType att, uint32 val) { SetFloatValue(UNIT_FIELD_BASEATTACKTIME+att,val*m_modAttackSpeedPct[att]); }
        void ApplyAttackTimePercentMod(WeaponAttackType att,float val, bool apply);
        void ApplyCastTimePercentMod(float val, bool apply);

        SheathState GetSheath() const { return SheathState(GetByteValue(UNIT_FIELD_BYTES_2, 0)); }
        virtual void SetSheath( SheathState sheathed ) { SetByteValue(UNIT_FIELD_BYTES_2, 0, sheathed); }

        // faction template id
        uint32 getFaction() const { return GetUInt32Value(UNIT_FIELD_FACTIONTEMPLATE); }
        void setFaction(uint32 faction) { SetUInt32Value(UNIT_FIELD_FACTIONTEMPLATE, faction ); }
        FactionTemplateEntry const* getFactionTemplateEntry() const;
        bool IsHostileTo(Unit const* unit) const;
        bool IsHostileToPlayers() const;
        bool IsFriendlyTo(Unit const* unit) const;
        bool IsNeutralToAll() const;
        bool IsContestedGuard() const
        {
            if(FactionTemplateEntry const* entry = getFactionTemplateEntry())
                return entry->IsContestedGuardFaction();

            return false;
        }
        bool IsPvP() const { return HasByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_PVP); }
        void SetPvP(bool state);
        bool IsFFAPvP() const { return HasByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_FFA_PVP); }
        void SetFFAPvP(bool state);
        uint32 GetCreatureType() const;
        uint32 GetCreatureTypeMask() const
        {
            uint32 creatureType = GetCreatureType();
            return (creatureType >= 1) ? (1 << (creatureType - 1)) : 0;
        }

        uint8 getStandState() const { return GetByteValue(UNIT_FIELD_BYTES_1, 0); }
        bool IsSitState() const;
        bool IsStandState() const;
        void SetStandState(uint8 state);

        void  SetStandFlags(uint8 flags) { SetByteFlag(UNIT_FIELD_BYTES_1, 2,flags); }
        void  RemoveStandFlags(uint8 flags) { RemoveByteFlag(UNIT_FIELD_BYTES_1, 2,flags); }

        bool IsMounted() const { return HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_MOUNT ); }
        uint32 GetMountID() const { return GetUInt32Value(UNIT_FIELD_MOUNTDISPLAYID); }
        void Mount(uint32 mount, uint32 spellId = 0);
        void Unmount();

        uint16 GetMaxSkillValueForLevel(Unit const* target = NULL) const { return (target ? getLevelForTarget(target) : getLevel()) * 5; }
        void DealDamageMods(Unit *pVictim, uint32 &damage, uint32* absorb);
        uint32 DealDamage(Unit *pVictim, uint32 damage, CleanDamage const* cleanDamage, DamageEffectType damagetype, SpellSchoolMask damageSchoolMask, SpellEntry const *spellProto, bool durabilityLoss);
        int32 DealHeal(Unit *pVictim, uint32 addhealth, SpellEntry const *spellProto, bool critical = false);

        void ProcDamageAndSpell(Unit *pVictim, uint32 procAttacker, uint32 procVictim, uint32 procEx, uint32 amount, WeaponAttackType attType = BASE_ATTACK, SpellEntry const *procSpell = NULL);
        void ProcDamageAndSpellFor( bool isVictim, Unit * pTarget, uint32 procFlag, uint32 procExtra, WeaponAttackType attType, SpellEntry const * procSpell, uint32 damage );

        void HandleEmoteCommand(uint32 anim_id);
        void AttackerStateUpdate (Unit *pVictim, WeaponAttackType attType = BASE_ATTACK, bool extra = false );

        float MeleeMissChanceCalc(const Unit *pVictim, WeaponAttackType attType) const;

        void CalculateMeleeDamage(Unit *pVictim, uint32 damage, CalcDamageInfo *damageInfo, WeaponAttackType attackType = BASE_ATTACK);
        void DealMeleeDamage(CalcDamageInfo *damageInfo, bool durabilityLoss);

        void CalculateSpellDamage(SpellNonMeleeDamage *damageInfo, int32 damage, SpellEntry const *spellInfo, WeaponAttackType attackType = BASE_ATTACK);
        void DealSpellDamage(SpellNonMeleeDamage *damageInfo, bool durabilityLoss);

        // player or player's pet resilience (-1%)
        float GetMeleeCritChanceReduction() const { return GetCombatRatingReduction(CR_CRIT_TAKEN_MELEE); }
        float GetRangedCritChanceReduction() const { return GetCombatRatingReduction(CR_CRIT_TAKEN_RANGED); }
        float GetSpellCritChanceReduction() const { return GetCombatRatingReduction(CR_CRIT_TAKEN_SPELL); }

        // player or player's pet resilience (-1%)
        uint32 GetMeleeCritDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_MELEE, 2.2f, 33.0f, damage); }
        uint32 GetRangedCritDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_RANGED, 2.2f, 33.0f, damage); }
        uint32 GetSpellCritDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_SPELL, 2.2f, 33.0f, damage); }

        // player or player's pet resilience (-1%), cap 100%
        uint32 GetMeleeDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_MELEE, 1.0f, 100.0f, damage); }
        uint32 GetRangedDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_MELEE, 1.0f, 100.0f, damage); }
        uint32 GetSpellDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_MELEE, 1.0f, 100.0f, damage); }

        float  MeleeSpellMissChance(Unit *pVictim, WeaponAttackType attType, int32 skillDiff, SpellEntry const *spell);
        SpellMissInfo MeleeSpellHitResult(Unit *pVictim, SpellEntry const *spell);
        SpellMissInfo MagicSpellHitResult(Unit *pVictim, SpellEntry const *spell);
        SpellMissInfo SpellHitResult(Unit *pVictim, SpellEntry const *spell, bool canReflect = false);

        float GetUnitDodgeChance()    const;
        float GetUnitParryChance()    const;
        float GetUnitBlockChance()    const;
        float GetUnitCriticalChance(WeaponAttackType attackType, const Unit *pVictim) const;

        virtual uint32 GetShieldBlockValue() const =0;
        uint32 GetUnitMeleeSkill(Unit const* target = NULL) const { return (target ? getLevelForTarget(target) : getLevel()) * 5; }
        uint32 GetDefenseSkillValue(Unit const* target = NULL) const;
        uint32 GetWeaponSkillValue(WeaponAttackType attType, Unit const* target = NULL) const;
        float GetWeaponProcChance() const;
        float GetPPMProcChance(uint32 WeaponSpeed, float PPM) const;

        MeleeHitOutcome RollMeleeOutcomeAgainst (const Unit *pVictim, WeaponAttackType attType) const;
        MeleeHitOutcome RollMeleeOutcomeAgainst (const Unit *pVictim, WeaponAttackType attType, int32 crit_chance, int32 miss_chance, int32 dodge_chance, int32 parry_chance, int32 block_chance) const;

        bool isVendor()       const { return HasFlag( UNIT_NPC_FLAGS, UNIT_NPC_FLAG_VENDOR ); }
        bool isTrainer()      const { return HasFlag( UNIT_NPC_FLAGS, UNIT_NPC_FLAG_TRAINER ); }
        bool isQuestGiver()   const { return HasFlag( UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER ); }
        bool isGossip()       const { return HasFlag( UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP ); }
        bool isTaxi()         const { return HasFlag( UNIT_NPC_FLAGS, UNIT_NPC_FLAG_FLIGHTMASTER ); }
        bool isGuildMaster()  const { return HasFlag( UNIT_NPC_FLAGS, UNIT_NPC_FLAG_PETITIONER ); }
        bool isBattleMaster() const { return HasFlag( UNIT_NPC_FLAGS, UNIT_NPC_FLAG_BATTLEMASTER ); }
        bool isBanker()       const { return HasFlag( UNIT_NPC_FLAGS, UNIT_NPC_FLAG_BANKER ); }
        bool isInnkeeper()    const { return HasFlag( UNIT_NPC_FLAGS, UNIT_NPC_FLAG_INNKEEPER ); }
        bool isSpiritHealer() const { return HasFlag( UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPIRITHEALER ); }
        bool isSpiritGuide()  const { return HasFlag( UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPIRITGUIDE ); }
        bool isTabardDesigner()const { return HasFlag( UNIT_NPC_FLAGS, UNIT_NPC_FLAG_TABARDDESIGNER ); }
        bool isAuctioner()    const { return HasFlag( UNIT_NPC_FLAGS, UNIT_NPC_FLAG_AUCTIONEER ); }
        bool isArmorer()      const { return HasFlag( UNIT_NPC_FLAGS, UNIT_NPC_FLAG_REPAIR ); }
        bool isServiceProvider() const
        {
            return HasFlag( UNIT_NPC_FLAGS,
                UNIT_NPC_FLAG_VENDOR | UNIT_NPC_FLAG_TRAINER | UNIT_NPC_FLAG_FLIGHTMASTER |
                UNIT_NPC_FLAG_PETITIONER | UNIT_NPC_FLAG_BATTLEMASTER | UNIT_NPC_FLAG_BANKER |
                UNIT_NPC_FLAG_INNKEEPER | UNIT_NPC_FLAG_GUARD | UNIT_NPC_FLAG_SPIRITHEALER |
                UNIT_NPC_FLAG_SPIRITGUIDE | UNIT_NPC_FLAG_TABARDDESIGNER | UNIT_NPC_FLAG_AUCTIONEER );
        }
        bool isSpiritService() const { return HasFlag( UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPIRITHEALER | UNIT_NPC_FLAG_SPIRITGUIDE ); }

        //Need fix or use this
        bool isGuard() const  { return HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GUARD); }

        bool isInFlight()  const { return hasUnitState(UNIT_STAT_IN_FLIGHT); }

        bool isInCombat()  const { return HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT); }
        void SetInCombatState(bool PvP, Unit* enemy = NULL);
        void SetInCombatWith(Unit* enemy);
        void ClearInCombat();
        uint32 GetCombatTimer() const { return m_CombatTimer; }

        bool HasAuraType(AuraType auraType) const;
        bool HasAura(uint32 spellId, SpellEffectIndex effIndex) const
        {
            return m_Auras.find(spellEffectPair(spellId, effIndex)) != m_Auras.end();
        }
        bool HasAura(uint32 spellId) const;

        bool virtual HasSpell(uint32 /*spellID*/) const { return false; }

        bool HasStealthAura()      const { return HasAuraType(SPELL_AURA_MOD_STEALTH); }
        bool HasInvisibilityAura() const { return HasAuraType(SPELL_AURA_MOD_INVISIBILITY); }
        bool isFeared()  const { return HasAuraType(SPELL_AURA_MOD_FEAR); }
        bool isInRoots() const { return HasAuraType(SPELL_AURA_MOD_ROOT); }
        bool IsPolymorphed() const;

        bool isFrozen() const;

        void RemoveSpellbyDamageTaken(AuraType auraType, uint32 damage);

        bool isTargetableForAttack(bool inversAlive = false) const;
        virtual bool IsInWater() const;
        virtual bool IsUnderWater() const;
        bool isInAccessablePlaceFor(Creature const* c) const;

        void SendHealSpellLog(Unit *pVictim, uint32 SpellID, uint32 Damage, uint32 OverHeal, bool critical = false);
        void SendEnergizeSpellLog(Unit *pVictim, uint32 SpellID, uint32 Damage,Powers powertype);
        void EnergizeBySpell(Unit *pVictim, uint32 SpellID, uint32 Damage, Powers powertype);
        uint32 SpellNonMeleeDamageLog(Unit *pVictim, uint32 spellID, uint32 damage);
        void CastSpell(Unit* Victim, uint32 spellId, bool triggered, Item *castItem = NULL, Aura* triggeredByAura = NULL, uint64 originalCaster = 0);
        void CastSpell(Unit* Victim,SpellEntry const *spellInfo, bool triggered, Item *castItem= NULL, Aura* triggeredByAura = NULL, uint64 originalCaster = 0);
        void CastCustomSpell(Unit* Victim, uint32 spellId, int32 const* bp0, int32 const* bp1, int32 const* bp2, bool triggered, Item *castItem= NULL, Aura* triggeredByAura = NULL, uint64 originalCaster = 0);
        void CastCustomSpell(Unit* Victim,SpellEntry const *spellInfo, int32 const* bp0, int32 const* bp1, int32 const* bp2, bool triggered, Item *castItem= NULL, Aura* triggeredByAura = NULL, uint64 originalCaster = 0);
        void CastSpell(float x, float y, float z, uint32 spellId, bool triggered, Item *castItem = NULL, Aura* triggeredByAura = NULL, uint64 originalCaster = 0);
        void CastSpell(float x, float y, float z, SpellEntry const *spellInfo, bool triggered, Item *castItem = NULL, Aura* triggeredByAura = NULL, uint64 originalCaster = 0);

        bool IsDamageToThreatSpell(SpellEntry const * spellInfo) const;

        void DeMorph();

        void SendAttackStateUpdate(CalcDamageInfo *damageInfo);
        void SendAttackStateUpdate(uint32 HitInfo, Unit *target, uint8 SwingType, SpellSchoolMask damageSchoolMask, uint32 Damage, uint32 AbsorbDamage, uint32 Resist, VictimState TargetState, uint32 BlockedAmount);
        void SendSpellNonMeleeDamageLog(SpellNonMeleeDamage *log);
        void SendSpellNonMeleeDamageLog(Unit *target,uint32 SpellID, uint32 Damage, SpellSchoolMask damageSchoolMask, uint32 AbsorbedDamage, uint32 Resist, bool PhysicalDamage, uint32 Blocked, bool CriticalHit = false);
        void SendPeriodicAuraLog(SpellPeriodicAuraLogInfo *pInfo);
        void SendSpellMiss(Unit *target, uint32 spellID, SpellMissInfo missInfo);

        void NearTeleportTo(float x, float y, float z, float orientation, bool casting = false);

        void MonsterMove(float x, float y, float z, uint32 transitTime);
        void MonsterMoveWithSpeed(float x, float y, float z, uint32 transitTime = 0);

        // recommend use MonsterMove/MonsterMoveWithSpeed for most case that correctly work with movegens
        void SendMonsterMove(float x, float y, float z, SplineType type, SplineFlags flags, uint32 Time, Player* player = NULL);
        void SendMonsterMoveByPath(Path const& path, uint32 start, uint32 end, SplineFlags flags);
        void SendMonsterMoveWithSpeed(float x, float y, float z, uint32 transitTime = 0, Player* player = NULL);

        void SendHighestThreatUpdate(HostileReference* pHostileReference);
        void SendThreatClear();
        void SendThreatRemove(HostileReference* pHostileReference);
        void SendThreatUpdate();

        void BuildHeartBeatMsg( WorldPacket *data ) const;

        virtual void MoveOutOfRange(Player &) {  };

        bool isAlive() const { return (m_deathState == ALIVE); };
        bool isDead() const { return ( m_deathState == DEAD || m_deathState == CORPSE ); };
        DeathState getDeathState() { return m_deathState; };
        virtual void setDeathState(DeathState s);           // overwritten in Creature/Player/Pet

        uint64 GetOwnerGUID() const { return  GetUInt64Value(UNIT_FIELD_SUMMONEDBY); }
        void SetOwnerGUID(uint64 owner) { SetUInt64Value(UNIT_FIELD_SUMMONEDBY, owner); }
        uint64 GetCreatorGUID() const { return GetUInt64Value(UNIT_FIELD_CREATEDBY); }
        void SetCreatorGUID(uint64 creator) { SetUInt64Value(UNIT_FIELD_CREATEDBY, creator); }
        uint64 GetPetGUID() const { return GetUInt64Value(UNIT_FIELD_SUMMON); }
        void SetPetGUID(uint64 pet) { SetUInt64Value(UNIT_FIELD_SUMMON, pet); }
        uint64 GetCharmerGUID() const { return GetUInt64Value(UNIT_FIELD_CHARMEDBY); }
        void SetCharmerGUID(uint64 owner) { SetUInt64Value(UNIT_FIELD_CHARMEDBY, owner); }
        uint64 GetCharmGUID() const { return GetUInt64Value(UNIT_FIELD_CHARM); }
        void SetCharmGUID(uint64 charm) { SetUInt64Value(UNIT_FIELD_CHARM, charm); }
        uint64 GetTargetGUID() const { return GetUInt64Value(UNIT_FIELD_TARGET); }
        void SetTargetGUID(uint64 targetGuid) { SetUInt64Value(UNIT_FIELD_TARGET, targetGuid); }
        uint64 GetChannelObjectGUID() const { return GetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT); }
        void SetChannelObjectGUID(uint64 targetGuid) { SetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT, targetGuid); }

        uint64 GetCharmerOrOwnerGUID() const { return GetCharmerGUID() ? GetCharmerGUID() : GetOwnerGUID(); }
        uint64 GetCharmerOrOwnerOrOwnGUID() const
        {
            if(uint64 guid = GetCharmerOrOwnerGUID())
                return guid;
            return GetGUID();
        }
        bool isCharmedOwnedByPlayerOrPlayer() const { return IS_PLAYER_GUID(GetCharmerOrOwnerOrOwnGUID()); }

        Player* GetSpellModOwner();

        Unit* GetOwner() const;
        Pet* GetPet() const;
        Unit* GetCharmer() const;
        Unit* GetCharm() const;
        void Uncharm();
        Unit* GetCharmerOrOwner() const { return GetCharmerGUID() ? GetCharmer() : GetOwner(); }
        Unit* GetCharmerOrOwnerOrSelf()
        {
            if(Unit* u = GetCharmerOrOwner())
                return u;

            return this;
        }
        bool IsCharmerOrOwnerPlayerOrPlayerItself() const;
        Player* GetCharmerOrOwnerPlayerOrPlayerItself();
        float GetCombatDistance( const Unit* target ) const;

        void SetPet(Pet* pet);
        void SetCharm(Unit* pet);

        void AddGuardian(Pet* pet);
        void RemoveGuardian(Pet* pet);
        void RemoveGuardians();
        Pet* FindGuardianWithEntry(uint32 entry);

        bool isCharmed() const { return GetCharmerGUID() != 0; }

        CharmInfo* GetCharmInfo() { return m_charmInfo; }
        CharmInfo* InitCharmInfo(Unit* charm);

        Pet* CreateTamedPetFrom(Creature* creatureTarget,uint32 spell_id = 0);

        Totem* GetTotem(uint8 slot) const;

        template<typename Func>
        void CallForAllControlledUnits(Func const& func, bool withTotems, bool withGuardians, bool withCharms);
        template<typename Func>
        bool CheckAllControlledUnits(Func const& func, bool withTotems, bool withGuardians, bool withCharms) const;

        bool AddAura(Aura *aur);

        // removing specific aura stack
        void RemoveAura(Aura* aura, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
        void RemoveAura(AuraMap::iterator &i, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
        void RemoveAura(uint32 spellId, SpellEffectIndex effindex, Aura* except = NULL);

        // removing specific aura stacks by diff reasons and selections
        void RemoveAurasDueToSpell(uint32 spellId, Aura* except = NULL);
        void RemoveAurasDueToItemSpell(Item* castItem,uint32 spellId);
        void RemoveAurasByCasterSpell(uint32 spellId, uint64 casterGUID);
        void RemoveAurasByCasterSpell(uint32 spellId, SpellEffectIndex effindex, uint64 casterGUID);
        void RemoveAurasDueToSpellBySteal(uint32 spellId, uint64 casterGUID, Unit *stealer);
        void RemoveAurasDueToSpellByCancel(uint32 spellId);

        // removing unknown aura stacks by diff reasons and selections
        void RemoveNotOwnSingleTargetAuras(uint32 newPhase = 0x0);
        void RemoveAurasAtMechanicImmunity(uint32 mechMask, uint32 exceptSpellId, bool non_positive = false);
        void RemoveSpellsCausingAura(AuraType auraType);
        void RemoveRankAurasDueToSpell(uint32 spellId);
        bool RemoveNoStackAurasDueToAura(Aura *Aur);
        void RemoveAurasWithInterruptFlags(uint32 flags);
        void RemoveAurasWithDispelType( DispelType type );
        void RemoveAllAuras(AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
        void RemoveArenaAuras(bool onleave = false);
        void RemoveAllAurasOnDeath();

        // removing specific aura FROM stack
        void RemoveSingleAuraFromStack(uint32 spellId, SpellEffectIndex effindex, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
        void RemoveSingleAuraFromStack(AuraMap::iterator &i, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);

        // removing specific aura FROM stack by diff reasons and selections
        void RemoveSingleSpellAurasFromStack(uint32 spellId, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
        void RemoveSingleSpellAurasByCasterSpell(uint32 spellId, uint64 casterGUID, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
        void RemoveSingleAuraByCasterSpell(uint32 spellId, SpellEffectIndex effindex, uint64 casterGUID, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
        void RemoveSingleAuraDueToSpellByDispel(uint32 spellId, uint64 casterGUID, Unit *dispeler);

        void DelayAura(uint32 spellId, SpellEffectIndex effindex, int32 delaytime);

        float GetResistanceBuffMods(SpellSchools school, bool positive) const { return GetFloatValue(positive ? UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE+school : UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE+school ); }
        void SetResistanceBuffMods(SpellSchools school, bool positive, float val) { SetFloatValue(positive ? UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE+school : UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE+school,val); }
        void ApplyResistanceBuffModsMod(SpellSchools school, bool positive, float val, bool apply) { ApplyModSignedFloatValue(positive ? UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE+school : UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE+school, val, apply); }
        void ApplyResistanceBuffModsPercentMod(SpellSchools school, bool positive, float val, bool apply) { ApplyPercentModFloatValue(positive ? UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE+school : UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE+school, val, apply); }
        void InitStatBuffMods()
        {
            for(int i = STAT_STRENGTH; i < MAX_STATS; ++i) SetFloatValue(UNIT_FIELD_POSSTAT0+i, 0);
            for(int i = STAT_STRENGTH; i < MAX_STATS; ++i) SetFloatValue(UNIT_FIELD_NEGSTAT0+i, 0);
        }
        void ApplyStatBuffMod(Stats stat, float val, bool apply) { ApplyModSignedFloatValue((val > 0 ? UNIT_FIELD_POSSTAT0+stat : UNIT_FIELD_NEGSTAT0+stat), val, apply); }
        void ApplyStatPercentBuffMod(Stats stat, float val, bool apply)
        {
            ApplyPercentModFloatValue(UNIT_FIELD_POSSTAT0+stat, val, apply);
            ApplyPercentModFloatValue(UNIT_FIELD_NEGSTAT0+stat, val, apply);
        }
        void SetCreateStat(Stats stat, float val) { m_createStats[stat] = val; }
        void SetCreateHealth(uint32 val) { SetUInt32Value(UNIT_FIELD_BASE_HEALTH, val); }
        uint32 GetCreateHealth() const { return GetUInt32Value(UNIT_FIELD_BASE_HEALTH); }
        void SetCreateMana(uint32 val) { SetUInt32Value(UNIT_FIELD_BASE_MANA, val); }
        uint32 GetCreateMana() const { return GetUInt32Value(UNIT_FIELD_BASE_MANA); }
        uint32 GetCreatePowers(Powers power) const;
        float GetPosStat(Stats stat) const { return GetFloatValue(UNIT_FIELD_POSSTAT0+stat); }
        float GetNegStat(Stats stat) const { return GetFloatValue(UNIT_FIELD_NEGSTAT0+stat); }
        float GetCreateStat(Stats stat) const { return m_createStats[stat]; }

        void SetCurrentCastedSpell(Spell * pSpell);
        virtual void ProhibitSpellSchool(SpellSchoolMask /*idSchoolMask*/, uint32 /*unTimeMs*/ ) { }
        void InterruptSpell(CurrentSpellTypes spellType, bool withDelayed = true);
        void FinishSpell(CurrentSpellTypes spellType, bool ok = true);

        // set withDelayed to true to account delayed spells as casted
        // delayed+channeled spells are always accounted as casted
        // we can skip channeled or delayed checks using flags
        bool IsNonMeleeSpellCasted(bool withDelayed, bool skipChanneled = false, bool skipAutorepeat = false) const;

        // set withDelayed to true to interrupt delayed spells too
        // delayed+channeled spells are always interrupted
        void InterruptNonMeleeSpells(bool withDelayed, uint32 spellid = 0);

        Spell* GetCurrentSpell(CurrentSpellTypes spellType) const { return m_currentSpells[spellType]; }
        Spell* FindCurrentSpellBySpellId(uint32 spell_id) const;

        bool CheckAndIncreaseCastCounter();
        void DecreaseCastCounter() { if (m_castCounter) --m_castCounter; }

        uint32 m_addDmgOnce;
        uint64 m_TotemSlot[MAX_TOTEM];
        uint64 m_ObjectSlot[4];
        uint32 m_detectInvisibilityMask;
        uint32 m_invisibilityMask;

        uint32 m_ShapeShiftFormSpellId;
        ShapeshiftForm m_form;
        bool IsInFeralForm() const { return m_form == FORM_CAT || m_form == FORM_BEAR || m_form == FORM_DIREBEAR; }

        float m_modMeleeHitChance;
        float m_modRangedHitChance;
        float m_modSpellHitChance;
        int32 m_baseSpellCritChance;

        float m_threatModifier[MAX_SPELL_SCHOOL];
        float m_modAttackSpeedPct[3];

        // Event handler
        EventProcessor m_Events;

        // stat system
        bool HandleStatModifier(UnitMods unitMod, UnitModifierType modifierType, float amount, bool apply);
        void SetModifierValue(UnitMods unitMod, UnitModifierType modifierType, float value) { m_auraModifiersGroup[unitMod][modifierType] = value; }
        float GetModifierValue(UnitMods unitMod, UnitModifierType modifierType) const;
        float GetTotalStatValue(Stats stat) const;
        float GetTotalAuraModValue(UnitMods unitMod) const;
        SpellSchools GetSpellSchoolByAuraGroup(UnitMods unitMod) const;
        Stats GetStatByAuraGroup(UnitMods unitMod) const;
        Powers GetPowerTypeByAuraGroup(UnitMods unitMod) const;
        bool CanModifyStats() const { return m_canModifyStats; }
        void SetCanModifyStats(bool modifyStats) { m_canModifyStats = modifyStats; }
        virtual bool UpdateStats(Stats stat) = 0;
        virtual bool UpdateAllStats() = 0;
        virtual void UpdateResistances(uint32 school) = 0;
        virtual void UpdateArmor() = 0;
        virtual void UpdateMaxHealth() = 0;
        virtual void UpdateMaxPower(Powers power) = 0;
        virtual void UpdateAttackPowerAndDamage(bool ranged = false) = 0;
        virtual void UpdateDamagePhysical(WeaponAttackType attType) = 0;
        float GetTotalAttackPowerValue(WeaponAttackType attType) const;
        float GetWeaponDamageRange(WeaponAttackType attType ,WeaponDamageRange type) const;
        void SetBaseWeaponDamage(WeaponAttackType attType ,WeaponDamageRange damageRange, float value) { m_weaponDamage[attType][damageRange] = value; }

        void SetInFront(Unit const* target);
        void SetFacingTo(float ori);
        void SetFacingToObject(WorldObject* pObject) { SetFacingTo(GetAngle(pObject)); }

        // Visibility system
        UnitVisibility GetVisibility() const { return m_Visibility; }
        void SetVisibility(UnitVisibility x);

        // common function for visibility checks for player/creatures with detection code
        bool isVisibleForOrDetect(Unit const* u, WorldObject const* viewPoint, bool detect, bool inVisibleList = false, bool is3dDistance = true) const;
        bool canDetectInvisibilityOf(Unit const* u) const;
        void SetPhaseMask(uint32 newPhaseMask, bool update);// overwrite WorldObject::SetPhaseMask

        // virtual functions for all world objects types
        bool isVisibleForInState(Player const* u, WorldObject const* viewPoint, bool inVisibleList) const;
        // function for low level grid visibility checks in player/creature cases
        virtual bool IsVisibleInGridForPlayer(Player* pl) const = 0;
        bool isInvisibleForAlive() const;

        AuraList      & GetSingleCastAuras()       { return m_scAuras; }
        AuraList const& GetSingleCastAuras() const { return m_scAuras; }
        SpellImmuneList m_spellImmune[MAX_SPELL_IMMUNITY];

        // Threat related methods
        bool CanHaveThreatList() const;
        void AddThreat(Unit* pVictim, float threat = 0.0f, bool crit = false, SpellSchoolMask schoolMask = SPELL_SCHOOL_MASK_NONE, SpellEntry const *threatSpell = NULL);
        float ApplyTotalThreatModifier(float threat, SpellSchoolMask schoolMask = SPELL_SCHOOL_MASK_NORMAL);
        void DeleteThreatList();
        bool SelectHostileTarget();
        void TauntApply(Unit* pVictim);
        void TauntFadeOut(Unit *taunter);
        ThreatManager& getThreatManager() { return m_ThreatManager; }
        ThreatManager const& getThreatManager() const { return m_ThreatManager; }
        void addHatedBy(HostileReference* pHostileReference) { m_HostileRefManager.insertFirst(pHostileReference); };
        void removeHatedBy(HostileReference* /*pHostileReference*/ ) { /* nothing to do yet */ }
        HostileRefManager& getHostileRefManager() { return m_HostileRefManager; }

        uint32 GetVisibleAura(uint8 slot)
        {
            VisibleAuraMap::const_iterator itr = m_visibleAuras.find(slot);
            if(itr != m_visibleAuras.end())
                return itr->second;
            return 0;
        }
        void SetVisibleAura(uint8 slot, uint32 spellid)
        {
            if(spellid == 0)
                m_visibleAuras.erase(slot);
            else
                m_visibleAuras[slot] = spellid;
        }
        VisibleAuraMap const *GetVisibleAuras() { return &m_visibleAuras; }
        uint8 GetVisibleAurasCount() { return m_visibleAuras.size(); }

        Aura* GetAura(uint32 spellId, SpellEffectIndex effindex);
        Aura* GetAura(AuraType type, uint32 family, uint64 familyFlag, uint32 familyFlag2 = 0, uint64 casterGUID = 0);

        AuraMap      & GetAuras()       { return m_Auras; }
        AuraMap const& GetAuras() const { return m_Auras; }
        AuraList const& GetAurasByType(AuraType type) const { return m_modAuras[type]; }
        void ApplyAuraProcTriggerDamage(Aura* aura, bool apply);

        int32 GetTotalAuraModifier(AuraType auratype) const;
        float GetTotalAuraMultiplier(AuraType auratype) const;
        int32 GetMaxPositiveAuraModifier(AuraType auratype) const;
        int32 GetMaxNegativeAuraModifier(AuraType auratype) const;

        int32 GetTotalAuraModifierByMiscMask(AuraType auratype, uint32 misc_mask) const;
        float GetTotalAuraMultiplierByMiscMask(AuraType auratype, uint32 misc_mask) const;
        int32 GetMaxPositiveAuraModifierByMiscMask(AuraType auratype, uint32 misc_mask) const;
        int32 GetMaxNegativeAuraModifierByMiscMask(AuraType auratype, uint32 misc_mask) const;

        int32 GetTotalAuraModifierByMiscValue(AuraType auratype, int32 misc_value) const;
        float GetTotalAuraMultiplierByMiscValue(AuraType auratype, int32 misc_value) const;
        int32 GetMaxPositiveAuraModifierByMiscValue(AuraType auratype, int32 misc_value) const;
        int32 GetMaxNegativeAuraModifierByMiscValue(AuraType auratype, int32 misc_value) const;

        // misc have plain value but we check it fit to provided values mask (mask & (1 << (misc-1)))
        float GetTotalAuraMultiplierByMiscValueForMask(AuraType auratype, uint32 mask) const;

        Aura* GetDummyAura(uint32 spell_id) const;

        uint32 m_AuraFlags;

        uint32 GetDisplayId() { return GetUInt32Value(UNIT_FIELD_DISPLAYID); }
        void SetDisplayId(uint32 modelId);
        uint32 GetNativeDisplayId() { return GetUInt32Value(UNIT_FIELD_NATIVEDISPLAYID); }
        void SetNativeDisplayId(uint32 modelId) { SetUInt32Value(UNIT_FIELD_NATIVEDISPLAYID, modelId); }
        void setTransForm(uint32 spellid) { m_transform = spellid;}
        uint32 getTransForm() const { return m_transform;}

        DynamicObject* GetDynObject(uint32 spellId, SpellEffectIndex effIndex);
        DynamicObject* GetDynObject(uint32 spellId);
        void AddDynObject(DynamicObject* dynObj);
        void RemoveDynObject(uint32 spellid);
        void RemoveDynObjectWithGUID(uint64 guid) { m_dynObjGUIDs.remove(guid); }
        void RemoveAllDynObjects();

        GameObject* GetGameObject(uint32 spellId) const;
        void AddGameObject(GameObject* gameObj);
        void RemoveGameObject(GameObject* gameObj, bool del);
        void RemoveGameObject(uint32 spellid, bool del);
        void RemoveAllGameObjects();

        uint32 CalculateDamage(WeaponAttackType attType, bool normalized);
        float GetAPMultiplier(WeaponAttackType attType, bool normalized);
        void ModifyAuraState(AuraState flag, bool apply);
        bool HasAuraState(AuraState flag) const { return HasFlag(UNIT_FIELD_AURASTATE, 1<<(flag-1)); }
        bool HasAuraStateForCaster(AuraState flag, uint64 caster) const;
        void UnsummonAllTotems();
        Unit* SelectMagnetTarget(Unit *victim, SpellEntry const *spellInfo = NULL);
        int32 SpellBaseDamageBonus(SpellSchoolMask schoolMask);
        int32 SpellBaseHealingBonus(SpellSchoolMask schoolMask);
        int32 SpellBaseDamageBonusForVictim(SpellSchoolMask schoolMask, Unit *pVictim);
        int32 SpellBaseHealingBonusForVictim(SpellSchoolMask schoolMask, Unit *pVictim);
        uint32 SpellDamageBonus(Unit *pVictim, SpellEntry const *spellProto, uint32 damage, DamageEffectType damagetype, uint32 stack = 1);
        int32 SpellHealingBonus(Unit *pVictim, SpellEntry const *spellProto, int32 healamount, DamageEffectType damagetype, uint32 stack = 1);
        bool   isSpellBlocked(Unit *pVictim, SpellEntry const *spellProto, WeaponAttackType attackType = BASE_ATTACK);
        bool   isSpellCrit(Unit *pVictim, SpellEntry const *spellProto, SpellSchoolMask schoolMask, WeaponAttackType attackType = BASE_ATTACK);
        uint32 SpellCriticalDamageBonus(SpellEntry const *spellProto, uint32 damage, Unit *pVictim);
        uint32 SpellCriticalHealingBonus(SpellEntry const *spellProto, uint32 damage, Unit *pVictim);

        void SetLastManaUse()
        {
            if (GetTypeId() == TYPEID_PLAYER && !IsUnderLastManaUseEffect())
                RemoveFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_REGENERATE_POWER);

            m_lastManaUseTimer = 5000;
        }
        bool IsUnderLastManaUseEffect() const { return m_lastManaUseTimer; }

        uint32 GetRegenTimer() const { return m_regenTimer; }

        void SetContestedPvP(Player *attackedPlayer = NULL);

        uint32 MeleeDamageBonus(Unit *pVictim, uint32 damage, WeaponAttackType attType, SpellEntry const *spellProto = NULL, DamageEffectType damagetype = DIRECT_DAMAGE, uint32 stack =1);
        uint32 GetCastingTimeForBonus( SpellEntry const *spellProto, DamageEffectType damagetype, uint32 CastingTime );

        void ApplySpellImmune(uint32 spellId, uint32 op, uint32 type, bool apply);
        void ApplySpellDispelImmunity(const SpellEntry * spellProto, DispelType type, bool apply);
        virtual bool IsImmunedToSpell(SpellEntry const* spellInfo);
                                                            // redefined in Creature
        bool IsImmunedToDamage(SpellSchoolMask meleeSchoolMask);
        virtual bool IsImmunedToSpellEffect(SpellEntry const* spellInfo, SpellEffectIndex index) const;
                                                            // redefined in Creature

        uint32 CalcArmorReducedDamage(Unit* pVictim, const uint32 damage);
        void CalcAbsorbResist(Unit *pVictim, SpellSchoolMask schoolMask, DamageEffectType damagetype, const uint32 damage, uint32 *absorb, uint32 *resist, bool canReflect = false);

        void  UpdateWalkMode(Unit* source, bool self = true);
        void  UpdateSpeed(UnitMoveType mtype, bool forced);
        float GetSpeed( UnitMoveType mtype ) const;
        float GetSpeedRate( UnitMoveType mtype ) const { return m_speed_rate[mtype]; }
        void SetSpeedRate(UnitMoveType mtype, float rate, bool forced = false);

        void SetHover(bool on);
        bool isHover() const { return HasAuraType(SPELL_AURA_HOVER); }

        void KnockBackFrom(Unit* target, float horizontalSpeed, float verticalSpeed);

        void _RemoveAllAuraMods();
        void _ApplyAllAuraMods();

        int32 CalculateSpellDamage(SpellEntry const* spellProto, SpellEffectIndex effect_index, int32 basePoints, Unit const* target);

        uint32 CalcNotIgnoreAbsorbDamage( uint32 damage, SpellSchoolMask damageSchoolMask, SpellEntry const* spellInfo = NULL);
        uint32 CalcNotIgnoreDamageRedunction( uint32 damage, SpellSchoolMask damageSchoolMask);
        int32 CalculateSpellDuration(SpellEntry const* spellProto, SpellEffectIndex effect_index, Unit const* target);
        float CalculateLevelPenalty(SpellEntry const* spellProto) const;

        void addFollower(FollowerReference* pRef) { m_FollowingRefManager.insertFirst(pRef); }
        void removeFollower(FollowerReference* /*pRef*/ ) { /* nothing to do yet */ }
        static Unit* GetUnit(WorldObject const& object, uint64 guid);

        MotionMaster* GetMotionMaster() { return &i_motionMaster; }

        bool IsStopped() const { return !(hasUnitState(UNIT_STAT_MOVING)); }
        void StopMoving();

        void SetFeared(bool apply, uint64 const& casterGUID = 0, uint32 spellID = 0, uint32 time = 0);
        void SetConfused(bool apply, uint64 const& casterGUID = 0, uint32 spellID = 0);
        void SetFeignDeath(bool apply, uint64 const& casterGUID = 0, uint32 spellID = 0);

        void AddComboPointHolder(uint32 lowguid) { m_ComboPointHolders.insert(lowguid); }
        void RemoveComboPointHolder(uint32 lowguid) { m_ComboPointHolders.erase(lowguid); }
        void ClearComboPointHolders();

        ///----------Pet responses methods-----------------
        void SendPetCastFail(uint32 spellid, SpellCastResult msg);
        void SendPetActionFeedback (uint8 msg);
        void SendPetTalk (uint32 pettalk);
        void SendPetAIReaction(uint64 guid);
        ///----------End of Pet responses methods----------

        void propagateSpeedChange() { GetMotionMaster()->propagateSpeedChange(); }

        // reactive attacks
        void ClearAllReactives();
        void StartReactiveTimer( ReactiveType reactive ) { m_reactiveTimer[reactive] = REACTIVE_TIMER_START;}
        void UpdateReactives(uint32 p_time);

        // group updates
        void UpdateAuraForGroup(uint8 slot);

        // pet auras
        typedef std::set<PetAura const*> PetAuraSet;
        PetAuraSet m_petAuras;
        void AddPetAura(PetAura const* petSpell);
        void RemovePetAura(PetAura const* petSpell);

        // Movement info
        MovementInfo m_movementInfo;

    protected:
        explicit Unit ();

        void _UpdateSpells(uint32 time);

        void _UpdateAutoRepeatSpell();
        bool m_AutoRepeatFirstCast;

        uint32 m_attackTimer[MAX_ATTACK];

        float m_createStats[MAX_STATS];

        AttackerSet m_attackers;
        Unit* m_attacking;

        DeathState m_deathState;

        AuraMap m_Auras;
        AuraMap::iterator m_AurasUpdateIterator;            // != end() in Unit::m_Auras update and point to next element
        AuraList m_deletedAuras;                            // auras removed while in ApplyModifier and waiting deleted

        AuraList m_scAuras;                                 // casted by unit single per-caster auras

        typedef std::list<uint64> DynObjectGUIDs;
        DynObjectGUIDs m_dynObjGUIDs;

        typedef std::list<GameObject*> GameObjectList;
        GameObjectList m_gameObj;
        bool m_isSorted;
        uint32 m_transform;

        AuraList m_modAuras[TOTAL_AURAS];
        float m_auraModifiersGroup[UNIT_MOD_END][MODIFIER_TYPE_END];
        float m_weaponDamage[MAX_ATTACK][2];
        bool m_canModifyStats;
        //std::list< spellEffectPair > AuraSpells[TOTAL_AURAS];  // TODO: use this if ok for mem
        VisibleAuraMap m_visibleAuras;

        float m_speed_rate[MAX_MOVE_TYPE];

        CharmInfo *m_charmInfo;

        virtual SpellSchoolMask GetMeleeDamageSchoolMask() const;

        MotionMaster i_motionMaster;

        uint32 m_reactiveTimer[MAX_REACTIVE];
        uint32 m_regenTimer;
        uint32 m_lastManaUseTimer;

    private:
        void CleanupDeletedAuras();

        bool IsTriggeredAtSpellProcEvent(Unit *pVictim, Aura* aura, SpellEntry const* procSpell, uint32 procFlag, uint32 procExtra, WeaponAttackType attType, bool isVictim, bool active, SpellProcEventEntry const*& spellProcEvent );
        bool HandleDummyAuraProc(   Unit *pVictim, uint32 damage, Aura* triggredByAura, SpellEntry const *procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        bool HandleHasteAuraProc(   Unit *pVictim, uint32 damage, Aura* triggredByAura, SpellEntry const *procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        bool HandleSpellCritChanceAuraProc(   Unit *pVictim, uint32 damage, Aura* triggredByAura, SpellEntry const *procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        bool HandleProcTriggerSpell(Unit *pVictim, uint32 damage, Aura* triggredByAura, SpellEntry const *procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        bool HandleOverrideClassScriptAuraProc(Unit *pVictim, uint32 damage, Aura* triggredByAura, SpellEntry const *procSpell, uint32 cooldown);
        bool HandleMendingAuraProc(Aura* triggeredByAura);

        // player or player's pet
        float GetCombatRatingReduction(CombatRating cr) const;
        uint32 GetCombatRatingDamageReduction(CombatRating cr, float rate, float cap, uint32 damage) const;

        Unit* _GetTotem(uint8 slot) const;                  // for templated function without include need

        uint32 m_state;                                     // Even derived shouldn't modify
        uint32 m_CombatTimer;

        Spell* m_currentSpells[CURRENT_MAX_SPELL];
        uint32 m_castCounter;                               // count casts chain of triggered spells for prevent infinity cast crashes

        UnitVisibility m_Visibility;

        Diminishing m_Diminishing;
        // Manage all Units threatening us
        ThreatManager m_ThreatManager;
        // Manage all Units that are threatened by us
        HostileRefManager m_HostileRefManager;

        FollowerRefManager m_FollowingRefManager;

        ComboPointHolderSet m_ComboPointHolders;

        GuardianPetList m_guardianPets;
};

template<typename Func>
void Unit::CallForAllControlledUnits(Func const& func, bool withTotems, bool withGuardians, bool withCharms)
{
    if(Pet* pet = GetPet())
        func(pet);

    if (withGuardians)
    {
        for(GuardianPetList::const_iterator itr = m_guardianPets.begin(); itr != m_guardianPets.end(); ++itr)
            if(Unit* guardian = Unit::GetUnit(*this,*itr))
                func(guardian);
    }

    if (withTotems)
    {
        for (int8 i = 0; i < MAX_TOTEM; ++i)
            if (Unit *totem = _GetTotem(i))
                func(totem);
    }

    if (withCharms)
        if(Unit* charm = GetCharm())
            func(charm);
}


template<typename Func>
bool Unit::CheckAllControlledUnits(Func const& func, bool withTotems, bool withGuardians, bool withCharms) const
{
    if (Pet* pet = GetPet())
        if (func(pet))
            return true;

    if (withGuardians)
    {
        for(GuardianPetList::const_iterator itr = m_guardianPets.begin(); itr != m_guardianPets.end(); ++itr)
            if (Unit* guardian = Unit::GetUnit(*this,*itr))
                if (func(guardian))
                    return true;

    }

    if (withTotems)
    {
        for (int8 i = 0; i < MAX_TOTEM; ++i)
            if (Unit *totem = _GetTotem(i))
                if (func(totem))
                    return true;
    }

    if (withCharms)
        if(Unit* charm = GetCharm())
            if (func(charm))
                return true;

    return false;
}
#endif
