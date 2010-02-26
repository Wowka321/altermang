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

#ifndef MANGOS_GRIDNOTIFIERS_H
#define MANGOS_GRIDNOTIFIERS_H

#include "ObjectGridLoader.h"
#include "UpdateData.h"
#include <iostream>

#include "Corpse.h"
#include "Object.h"
#include "DynamicObject.h"
#include "GameObject.h"
#include "Player.h"
#include "Unit.h"
#include "CreatureAI.h"

class Player;
//class Map;

namespace MaNGOS
{

    struct MANGOS_DLL_DECL PlayerNotifier
    {
        explicit PlayerNotifier(Player &pl) : i_player(pl) {}
        void Visit(PlayerMapType &);
        template<class SKIP> void Visit(GridRefManager<SKIP> &) {}
        Player &i_player;
    };

    struct MANGOS_DLL_DECL VisibleNotifier
    {
        Player &i_player;
        UpdateData i_data;
        UpdateDataMapType i_data_updates;
        Player::ClientGUIDs i_clientGUIDs;
        std::set<WorldObject*> i_visibleNow;

        explicit VisibleNotifier(Player &player) : i_player(player),i_clientGUIDs(player.m_clientGUIDs) {}
        template<class T> void Visit(GridRefManager<T> &m);
        void Visit(PlayerMapType &);
        void Notify(void);
    };

    struct MANGOS_DLL_DECL VisibleChangesNotifier
    {
        WorldObject &i_object;

        explicit VisibleChangesNotifier(WorldObject &object) : i_object(object) {}
        template<class T> void Visit(GridRefManager<T> &) {}
        void Visit(PlayerMapType &);
    };

    struct MANGOS_DLL_DECL GridUpdater
    {
        GridType &i_grid;
        uint32 i_timeDiff;
        GridUpdater(GridType &grid, uint32 diff) : i_grid(grid), i_timeDiff(diff) {}

        template<class T> void updateObjects(GridRefManager<T> &m)
        {
            for(typename GridRefManager<T>::iterator iter = m.begin(); iter != m.end(); ++iter)
                iter->getSource()->Update(i_timeDiff);
        }

        void Visit(PlayerMapType &m) { updateObjects<Player>(m); }
        void Visit(CreatureMapType &m){ updateObjects<Creature>(m); }
        void Visit(GameObjectMapType &m) { updateObjects<GameObject>(m); }
        void Visit(DynamicObjectMapType &m) { updateObjects<DynamicObject>(m); }
        void Visit(CorpseMapType &m) { updateObjects<Corpse>(m); }
    };

    struct MANGOS_DLL_DECL MessageDeliverer
    {
        Player &i_player;
        WorldPacket *i_message;
        bool i_toSelf;
        MessageDeliverer(Player &pl, WorldPacket *msg, bool to_self) : i_player(pl), i_message(msg), i_toSelf(to_self) {}
        void Visit(PlayerMapType &m);
        template<class SKIP> void Visit(GridRefManager<SKIP> &) {}
    };

    struct MANGOS_DLL_DECL ObjectMessageDeliverer
    {
        uint32 i_phaseMask;
        WorldPacket *i_message;
        explicit ObjectMessageDeliverer(WorldObject& obj, WorldPacket *msg)
            : i_phaseMask(obj.GetPhaseMask()), i_message(msg) {}
        void Visit(PlayerMapType &m);
        template<class SKIP> void Visit(GridRefManager<SKIP> &) {}
    };

    struct MANGOS_DLL_DECL MessageDistDeliverer
    {
        Player &i_player;
        WorldPacket *i_message;
        bool i_toSelf;
        bool i_ownTeamOnly;
        float i_dist;

        MessageDistDeliverer(Player &pl, WorldPacket *msg, float dist, bool to_self, bool ownTeamOnly)
            : i_player(pl), i_message(msg), i_toSelf(to_self), i_ownTeamOnly(ownTeamOnly), i_dist(dist) {}
        void Visit(PlayerMapType &m);
        template<class SKIP> void Visit(GridRefManager<SKIP> &) {}
    };

    struct MANGOS_DLL_DECL ObjectMessageDistDeliverer
    {
        WorldObject &i_object;
        WorldPacket *i_message;
        float i_dist;
        ObjectMessageDistDeliverer(WorldObject &obj, WorldPacket *msg, float dist) : i_object(obj), i_message(msg), i_dist(dist) {}
        void Visit(PlayerMapType &m);
        template<class SKIP> void Visit(GridRefManager<SKIP> &) {}
    };

    struct MANGOS_DLL_DECL ObjectUpdater
    {
        uint32 i_timeDiff;
        explicit ObjectUpdater(const uint32 &diff) : i_timeDiff(diff) {}
        template<class T> void Visit(GridRefManager<T> &m);
        void Visit(PlayerMapType &) {}
        void Visit(CorpseMapType &) {}
        void Visit(CreatureMapType &);
    };

    struct MANGOS_DLL_DECL PlayerRelocationNotifier
    {
        Player &i_player;
        PlayerRelocationNotifier(Player &pl) : i_player(pl) {}
        template<class T> void Visit(GridRefManager<T> &) {}
        void Visit(PlayerMapType &);
        void Visit(CreatureMapType &);
    };

    struct MANGOS_DLL_DECL CreatureRelocationNotifier
    {
        Creature &i_creature;
        CreatureRelocationNotifier(Creature &c) : i_creature(c) {}
        template<class T> void Visit(GridRefManager<T> &) {}
        #ifdef WIN32
        template<> void Visit(PlayerMapType &);
        #endif
    };

    struct MANGOS_DLL_DECL DynamicObjectUpdater
    {
        DynamicObject &i_dynobject;
        Unit* i_check;
        DynamicObjectUpdater(DynamicObject &dynobject, Unit* caster) : i_dynobject(dynobject)
        {
            i_check = caster;
            Unit* owner = i_check->GetOwner();
            if(owner)
                i_check = owner;
        }

        template<class T> inline void Visit(GridRefManager<T>  &) {}
        #ifdef WIN32
        template<> inline void Visit<Player>(PlayerMapType &);
        template<> inline void Visit<Creature>(CreatureMapType &);
        #endif

        void VisitHelper(Unit* target);
    };

    // SEARCHERS & LIST SEARCHERS & WORKERS

    // WorldObject searchers & workers

    template<class Check>
        struct MANGOS_DLL_DECL WorldObjectSearcher
    {
        uint32 i_phaseMask;
        WorldObject* &i_object;
        Check &i_check;

        WorldObjectSearcher(WorldObject const* searcher, WorldObject* & result, Check& check)
            : i_phaseMask(searcher->GetPhaseMask()), i_object(result),i_check(check) {}

        void Visit(GameObjectMapType &m);
        void Visit(PlayerMapType &m);
        void Visit(CreatureMapType &m);
        void Visit(CorpseMapType &m);
        void Visit(DynamicObjectMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) {}
    };

    template<class Check>
        struct MANGOS_DLL_DECL WorldObjectListSearcher
    {
        uint32 i_phaseMask;
        std::list<WorldObject*> &i_objects;
        Check& i_check;

        WorldObjectListSearcher(WorldObject const* searcher, std::list<WorldObject*> &objects, Check & check)
            : i_phaseMask(searcher->GetPhaseMask()), i_objects(objects),i_check(check) {}

        void Visit(PlayerMapType &m);
        void Visit(CreatureMapType &m);
        void Visit(CorpseMapType &m);
        void Visit(GameObjectMapType &m);
        void Visit(DynamicObjectMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) {}
    };

    template<class Do>
        struct MANGOS_DLL_DECL WorldObjectWorker
    {
        uint32 i_phaseMask;
        Do const& i_do;

        WorldObjectWorker(WorldObject const* searcher, Do const& _do)
            : i_phaseMask(searcher->GetPhaseMask()), i_do(_do) {}

        void Visit(GameObjectMapType &m)
        {
            for(GameObjectMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
                if(itr->getSource()->InSamePhase(i_phaseMask))
                    i_do(itr->getSource());
        }

        void Visit(PlayerMapType &m)
        {
            for(PlayerMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
                if(itr->getSource()->InSamePhase(i_phaseMask))
                    i_do(itr->getSource());
        }
        void Visit(CreatureMapType &m)
        {
            for(CreatureMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
                if(itr->getSource()->InSamePhase(i_phaseMask))
                    i_do(itr->getSource());
        }

        void Visit(CorpseMapType &m)
        {
            for(CorpseMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
                if(itr->getSource()->InSamePhase(i_phaseMask))
                    i_do(itr->getSource());
        }

        void Visit(DynamicObjectMapType &m)
        {
            for(DynamicObjectMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
                if(itr->getSource()->InSamePhase(i_phaseMask))
                    i_do(itr->getSource());
        }

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) {}
    };

    // Gameobject searchers

    template<class Check>
        struct MANGOS_DLL_DECL GameObjectSearcher
    {
        uint32 i_phaseMask;
        GameObject* &i_object;
        Check &i_check;

        GameObjectSearcher(WorldObject const* searcher, GameObject* & result, Check& check)
            : i_phaseMask(searcher->GetPhaseMask()), i_object(result),i_check(check) {}

        void Visit(GameObjectMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) {}
    };

    // Last accepted by Check GO if any (Check can change requirements at each call)
    template<class Check>
        struct MANGOS_DLL_DECL GameObjectLastSearcher
    {
        uint32 i_phaseMask;
        GameObject* &i_object;
        Check& i_check;

        GameObjectLastSearcher(WorldObject const* searcher, GameObject* & result, Check& check)
            : i_phaseMask(searcher->GetPhaseMask()), i_object(result), i_check(check) {}

        void Visit(GameObjectMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) {}
    };

    template<class Check>
        struct MANGOS_DLL_DECL GameObjectListSearcher
    {
        uint32 i_phaseMask;
        std::list<GameObject*> &i_objects;
        Check& i_check;

        GameObjectListSearcher(WorldObject const* searcher, std::list<GameObject*> &objects, Check & check)
            : i_phaseMask(searcher->GetPhaseMask()), i_objects(objects), i_check(check) {}

        void Visit(GameObjectMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) {}
    };

    // Unit searchers

    // First accepted by Check Unit if any
    template<class Check>
        struct MANGOS_DLL_DECL UnitSearcher
    {
        uint32 i_phaseMask;
        Unit* &i_object;
        Check & i_check;

        UnitSearcher(WorldObject const* searcher, Unit* & result, Check & check)
            : i_phaseMask(searcher->GetPhaseMask()), i_object(result),i_check(check) {}

        void Visit(CreatureMapType &m);
        void Visit(PlayerMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) {}
    };

    // Last accepted by Check Unit if any (Check can change requirements at each call)
    template<class Check>
        struct MANGOS_DLL_DECL UnitLastSearcher
    {
        uint32 i_phaseMask;
        Unit* &i_object;
        Check & i_check;

        UnitLastSearcher(WorldObject const* searcher, Unit* & result, Check & check)
            : i_phaseMask(searcher->GetPhaseMask()), i_object(result),i_check(check) {}

        void Visit(CreatureMapType &m);
        void Visit(PlayerMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) {}
    };

    // All accepted by Check units if any
    template<class Check>
        struct MANGOS_DLL_DECL UnitListSearcher
    {
        uint32 i_phaseMask;
        std::list<Unit*> &i_objects;
        Check& i_check;

        UnitListSearcher(WorldObject const* searcher, std::list<Unit*> &objects, Check & check)
            : i_phaseMask(searcher->GetPhaseMask()), i_objects(objects),i_check(check) {}

        void Visit(PlayerMapType &m);
        void Visit(CreatureMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) {}
    };

    // Creature searchers

    template<class Check>
        struct MANGOS_DLL_DECL CreatureSearcher
    {
        uint32 i_phaseMask;
        Creature* &i_object;
        Check & i_check;

        CreatureSearcher(WorldObject const* searcher, Creature* & result, Check & check)
            : i_phaseMask(searcher->GetPhaseMask()), i_object(result),i_check(check) {}

        void Visit(CreatureMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) {}
    };

    // Last accepted by Check Creature if any (Check can change requirements at each call)
    template<class Check>
        struct MANGOS_DLL_DECL CreatureLastSearcher
    {
        uint32 i_phaseMask;
        Creature* &i_object;
        Check & i_check;

        CreatureLastSearcher(WorldObject const* searcher, Creature* & result, Check & check)
            : i_phaseMask(searcher->GetPhaseMask()), i_object(result),i_check(check) {}

        void Visit(CreatureMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) {}
    };

    template<class Check>
        struct MANGOS_DLL_DECL CreatureListSearcher
    {
        uint32 i_phaseMask;
        std::list<Creature*> &i_objects;
        Check& i_check;

        CreatureListSearcher(WorldObject const* searcher, std::list<Creature*> &objects, Check & check)
            : i_phaseMask(searcher->GetPhaseMask()), i_objects(objects),i_check(check) {}

        void Visit(CreatureMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) {}
    };

    template<class Do>
    struct MANGOS_DLL_DECL CreatureWorker
    {
        uint32 i_phaseMask;
        Do& i_do;

        CreatureWorker(WorldObject const* searcher, Do& _do)
            : i_phaseMask(searcher->GetPhaseMask()), i_do(_do) {}

        void Visit(CreatureMapType &m)
        {
            for(CreatureMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
                if(itr->getSource()->InSamePhase(i_phaseMask))
                    i_do(itr->getSource());
        }

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) {}
    };

    // Player searchers

    template<class Check>
    struct MANGOS_DLL_DECL PlayerSearcher
    {
        uint32 i_phaseMask;
        Player* &i_object;
        Check & i_check;

        PlayerSearcher(WorldObject const* searcher, Player* & result, Check & check)
            : i_phaseMask(searcher->GetPhaseMask()), i_object(result),i_check(check) {}

        void Visit(PlayerMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) {}
    };

    template<class Do>
    struct MANGOS_DLL_DECL PlayerWorker
    {
        uint32 i_phaseMask;
        Do& i_do;

        PlayerWorker(WorldObject const* searcher, Do& _do)
            : i_phaseMask(searcher->GetPhaseMask()), i_do(_do) {}

        void Visit(PlayerMapType &m)
        {
            for(PlayerMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
                if(itr->getSource()->InSamePhase(i_phaseMask))
                    i_do(itr->getSource());
        }

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) {}
    };

    template<class Do>
    struct MANGOS_DLL_DECL PlayerDistWorker
    {
        WorldObject const* i_searcher;
        float i_dist;
        Do& i_do;

        PlayerDistWorker(WorldObject const* searcher, float _dist, Do& _do)
            : i_searcher(searcher), i_dist(_dist), i_do(_do) {}

        void Visit(PlayerMapType &m)
        {
            for(PlayerMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
                if (itr->getSource()->InSamePhase(i_searcher) && itr->getSource()->IsWithinDist(i_searcher,i_dist))
                    i_do(itr->getSource());
        }

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) {}
    };

    // CHECKS && DO classes

    // WorldObject check classes
    class RaiseDeadObjectCheck
    {
        public:
            RaiseDeadObjectCheck(Unit* funit, float range) : i_funit(funit), i_range(range) {}
            bool operator()(Creature* u)
            {
                if (i_funit->GetTypeId()!=TYPEID_PLAYER || !((Player*)i_funit)->isHonorOrXPTarget(u) ||
                    u->getDeathState() != CORPSE || u->isDeadByDefault() || u->isInFlight() ||
                    ( u->GetCreatureTypeMask() & (1 << (CREATURE_TYPE_HUMANOID-1)) )==0 ||
                    (u->GetDisplayId() != u->GetNativeDisplayId()))
                    return false;

                return i_funit->IsWithinDistInMap(u, i_range);
            }
            template<class NOT_INTERESTED> bool operator()(NOT_INTERESTED*) { return false; }
        private:
            Unit* const i_funit;
            float i_range;
    };

    class ExplodeCorpseObjectCheck
    {
        public:
            ExplodeCorpseObjectCheck(Unit* funit, float range) : i_funit(funit), i_range(range) {}
            bool operator()(Player* u)
            {
                if (u->getDeathState()!=CORPSE || u->isInFlight() ||
                    u->HasAuraType(SPELL_AURA_GHOST) || (u->GetDisplayId() != u->GetNativeDisplayId()))
                    return false;

                return i_funit->IsWithinDistInMap(u, i_range);
            }
            bool operator()(Creature* u)
            {
                if (u->getDeathState()!=CORPSE || u->isInFlight() || u->isDeadByDefault() ||
                    (u->GetDisplayId() != u->GetNativeDisplayId()) ||
                    (u->GetCreatureTypeMask() & CREATURE_TYPEMASK_MECHANICAL_OR_ELEMENTAL)!=0)
                    return false;

                return i_funit->IsWithinDistInMap(u, i_range);
            }
            template<class NOT_INTERESTED> bool operator()(NOT_INTERESTED*) { return false; }
        private:
            Unit* const i_funit;
            float i_range;
    };

    class CannibalizeObjectCheck
    {
        public:
            CannibalizeObjectCheck(Unit* funit, float range) : i_funit(funit), i_range(range) {}
            bool operator()(Player* u)
            {
                if( i_funit->IsFriendlyTo(u) || u->isAlive() || u->isInFlight() )
                    return false;

                return i_funit->IsWithinDistInMap(u, i_range);
            }
            bool operator()(Corpse* u);
            bool operator()(Creature* u)
            {
                if (i_funit->IsFriendlyTo(u) || u->isAlive() || u->isInFlight() ||
                    (u->GetCreatureTypeMask() & CREATURE_TYPEMASK_HUMANOID_OR_UNDEAD)==0)
                    return false;

                return i_funit->IsWithinDistInMap(u, i_range);
            }
            template<class NOT_INTERESTED> bool operator()(NOT_INTERESTED*) { return false; }
        private:
            Unit* const i_funit;
            float i_range;
    };

    // WorldObject do classes

    class RespawnDo
    {
        public:
            RespawnDo() {}
            void operator()(Creature* u) const { u->Respawn(); }
            void operator()(GameObject* u) const { u->Respawn(); }
            void operator()(WorldObject*) const {}
            void operator()(Corpse*) const {}
    };

    // GameObject checks

    class GameObjectFocusCheck
    {
        public:
            GameObjectFocusCheck(Unit const* unit,uint32 focusId) : i_unit(unit), i_focusId(focusId) {}
            bool operator()(GameObject* go) const
            {
                if(go->GetGOInfo()->type != GAMEOBJECT_TYPE_SPELL_FOCUS)
                    return false;

                if(go->GetGOInfo()->spellFocus.focusId != i_focusId)
                    return false;

                float dist = (float)go->GetGOInfo()->spellFocus.dist;

                return go->IsWithinDistInMap(i_unit, dist);
            }
        private:
            Unit const* i_unit;
            uint32 i_focusId;
    };

    // Find the nearest Fishing hole and return true only if source object is in range of hole
    class NearestGameObjectFishingHole
    {
        public:
            NearestGameObjectFishingHole(WorldObject const& obj, float range) : i_obj(obj), i_range(range) {}
            bool operator()(GameObject* go)
            {
                if(go->GetGOInfo()->type == GAMEOBJECT_TYPE_FISHINGHOLE && go->isSpawned() && i_obj.IsWithinDistInMap(go, i_range) && i_obj.IsWithinDistInMap(go, (float)go->GetGOInfo()->fishinghole.radius))
                {
                    i_range = i_obj.GetDistance(go);
                    return true;
                }
                return false;
            }
            float GetLastRange() const { return i_range; }
        private:
            WorldObject const& i_obj;
            float  i_range;

            // prevent clone
            NearestGameObjectFishingHole(NearestGameObjectFishingHole const&);
    };

    // Success at unit in range, range update for next check (this can be use with GameobjectLastSearcher to find nearest GO)
    class NearestGameObjectEntryInObjectRangeCheck
    {
        public:
            NearestGameObjectEntryInObjectRangeCheck(WorldObject const& obj,uint32 entry, float range) : i_obj(obj), i_entry(entry), i_range(range) {}
            bool operator()(GameObject* go)
            {
                if(go->GetEntry() == i_entry && i_obj.IsWithinDistInMap(go, i_range))
                {
                    i_range = i_obj.GetDistance(go);        // use found GO range as new range limit for next check
                    return true;
                }
                return false;
            }
            float GetLastRange() const { return i_range; }
        private:
            WorldObject const& i_obj;
            uint32 i_entry;
            float  i_range;

            // prevent clone this object
            NearestGameObjectEntryInObjectRangeCheck(NearestGameObjectEntryInObjectRangeCheck const&);
    };

    class GameObjectWithDbGUIDCheck
    {
        public:
            GameObjectWithDbGUIDCheck(WorldObject const& obj,uint32 db_guid) : i_obj(obj), i_db_guid(db_guid) {}
            bool operator()(GameObject const* go) const
            {
                return go->GetDBTableGUIDLow() == i_db_guid;
            }
        private:
            WorldObject const& i_obj;
            uint32 i_db_guid;
    };

    // Unit checks

    class MostHPMissingInRange
    {
        public:
            MostHPMissingInRange(Unit const* obj, float range, uint32 hp) : i_obj(obj), i_range(range), i_hp(hp) {}
            bool operator()(Unit* u)
            {
                if(u->isAlive() && u->isInCombat() && !i_obj->IsHostileTo(u) && i_obj->IsWithinDistInMap(u, i_range) && u->GetMaxHealth() - u->GetHealth() > i_hp)
                {
                    i_hp = u->GetMaxHealth() - u->GetHealth();
                    return true;
                }
                return false;
            }
        private:
            Unit const* i_obj;
            float i_range;
            uint32 i_hp;
    };

    class FriendlyCCedInRange
    {
        public:
            FriendlyCCedInRange(Unit const* obj, float range) : i_obj(obj), i_range(range) {}
            bool operator()(Unit* u)
            {
                if(u->isAlive() && u->isInCombat() && !i_obj->IsHostileTo(u) && i_obj->IsWithinDistInMap(u, i_range) &&
                    (u->isCharmed() || u->isFrozen() || u->hasUnitState(UNIT_STAT_CAN_NOT_REACT)))
                {
                    return true;
                }
                return false;
            }
        private:
            Unit const* i_obj;
            float i_range;
    };

    class FriendlyMissingBuffInRange
    {
        public:
            FriendlyMissingBuffInRange(Unit const* obj, float range, uint32 spellid) : i_obj(obj), i_range(range), i_spell(spellid) {}
            bool operator()(Unit* u)
            {
                if(u->isAlive() && u->isInCombat() && !i_obj->IsHostileTo(u) && i_obj->IsWithinDistInMap(u, i_range) &&
                    !(u->HasAura(i_spell, EFFECT_INDEX_0) || u->HasAura(i_spell, EFFECT_INDEX_1) || u->HasAura(i_spell, EFFECT_INDEX_2)))
                {
                    return true;
                }
                return false;
            }
        private:
            Unit const* i_obj;
            float i_range;
            uint32 i_spell;
    };

    class AnyUnfriendlyUnitInObjectRangeCheck
    {
        public:
            AnyUnfriendlyUnitInObjectRangeCheck(WorldObject const* obj, Unit const* funit, float range) : i_obj(obj), i_funit(funit), i_range(range) {}
            bool operator()(Unit* u)
            {
                if(u->isAlive() && i_obj->IsWithinDistInMap(u, i_range) && !i_funit->IsFriendlyTo(u))
                    return true;
                else
                    return false;
            }
        private:
            WorldObject const* i_obj;
            Unit const* i_funit;
            float i_range;
    };

    class AnyUnfriendlyVisibleUnitInObjectRangeCheck
    {
        public:
            AnyUnfriendlyVisibleUnitInObjectRangeCheck(WorldObject const* obj, Unit const* funit, float range)
                : i_obj(obj), i_funit(funit), i_range(range) {}

            bool operator()(Unit* u)
            {
                return u->isAlive()
                    && i_obj->IsWithinDistInMap(u, i_range)
                    && !i_funit->IsFriendlyTo(u)
                    && u->isVisibleForOrDetect(i_funit,i_funit,false);
            }
        private:
            WorldObject const* i_obj;
            Unit const* i_funit;
            float i_range;
    };

    class AnyFriendlyUnitInObjectRangeCheck
    {
        public:
            AnyFriendlyUnitInObjectRangeCheck(WorldObject const* obj, Unit const* funit, float range) : i_obj(obj), i_funit(funit), i_range(range) {}
            bool operator()(Unit* u)
            {
                if(u->isAlive() && i_obj->IsWithinDistInMap(u, i_range) && i_funit->IsFriendlyTo(u))
                    return true;
                else
                    return false;
            }
        private:
            WorldObject const* i_obj;
            Unit const* i_funit;
            float i_range;
    };

    class AnyUnitInObjectRangeCheck
    {
        public:
            AnyUnitInObjectRangeCheck(WorldObject const* obj, float range) : i_obj(obj), i_range(range) {}
            bool operator()(Unit* u)
            {
                if(u->isAlive() && i_obj->IsWithinDistInMap(u, i_range))
                    return true;

                return false;
            }
        private:
            WorldObject const* i_obj;
            float i_range;
    };

    // Success at unit in range, range update for next check (this can be use with UnitLastSearcher to find nearest unit)
    class NearestAttackableUnitInObjectRangeCheck
    {
        public:
            NearestAttackableUnitInObjectRangeCheck(WorldObject const* obj, Unit const* funit, float range) : i_obj(obj), i_funit(funit), i_range(range) {}
            bool operator()(Unit* u)
            {
                if( u->isTargetableForAttack() && i_obj->IsWithinDistInMap(u, i_range) &&
                    !i_funit->IsFriendlyTo(u) && u->isVisibleForOrDetect(i_funit,i_funit,false)  )
                {
                    i_range = i_obj->GetDistance(u);        // use found unit range as new range limit for next check
                    return true;
                }

                return false;
            }
        private:
            WorldObject const* i_obj;
            Unit const* i_funit;
            float i_range;

            // prevent clone this object
            NearestAttackableUnitInObjectRangeCheck(NearestAttackableUnitInObjectRangeCheck const&);
    };

    class AnyAoETargetUnitInObjectRangeCheck
    {
        public:
            AnyAoETargetUnitInObjectRangeCheck(WorldObject const* obj, Unit const* funit, float range, bool hitHidden = true)
                : i_obj(obj), i_funit(funit), i_range(range), i_hitHidden(hitHidden)
            {
                Unit const* check = i_funit;
                Unit const* owner = i_funit->GetOwner();
                if(owner)
                    check = owner;
                i_targetForPlayer = ( check->GetTypeId()==TYPEID_PLAYER );
            }
            bool operator()(Unit* u)
            {
                // Check contains checks for: live, non-selectable, non-attackable flags, flight check and GM check, ignore totems
                if (!u->isTargetableForAttack())
                    return false;
                if(u->GetTypeId()==TYPEID_UNIT && ((Creature*)u)->isTotem())
                    return false;
                if (!i_hitHidden && !u->isVisibleForOrDetect(i_funit, i_funit, false))
                    return false;

                if(( i_targetForPlayer ? !i_funit->IsFriendlyTo(u) : i_funit->IsHostileTo(u) )&& i_obj->IsWithinDistInMap(u, i_range))
                    return true;

                return false;
            }
        private:
            bool i_targetForPlayer;
            WorldObject const* i_obj;
            Unit const* i_funit;
            float i_range;
            bool i_hitHidden;
    };

    // do attack at call of help to friendly crearture
    class CallOfHelpCreatureInRangeDo
    {
        public:
            CallOfHelpCreatureInRangeDo(Unit* funit, Unit* enemy, float range)
                : i_funit(funit), i_enemy(enemy), i_range(range)
            {}
            void operator()(Creature* u)
            {
                if (u == i_funit)
                    return;

                if (!u->CanAssistTo(i_funit, i_enemy, false))
                    return;

                // too far
                if (!i_funit->IsWithinDistInMap(u, i_range))
                    return;

                // only if see assisted creature
                if (!i_funit->IsWithinLOSInMap(u))
                    return;

                if (u->AI())
                    u->AI()->AttackStart(i_enemy);
            }
        private:
            Unit* const i_funit;
            Unit* const i_enemy;
            float i_range;
    };

    struct AnyDeadUnitCheck
    {
        bool operator()(Unit* u) { return !u->isAlive(); }
    };

    struct AnyStealthedCheck
    {
        bool operator()(Unit* u) { return u->GetVisibility()==VISIBILITY_GROUP_STEALTH; }
    };

    // Creature checks

    class InAttackDistanceFromAnyHostileCreatureCheck
    {
        public:
            explicit InAttackDistanceFromAnyHostileCreatureCheck(Unit* funit) : i_funit(funit) {}
            bool operator()(Creature* u)
            {
                if(u->isAlive() && u->IsHostileTo(i_funit) && i_funit->IsWithinDistInMap(u, u->GetAttackDistance(i_funit)))
                    return true;

                return false;
            }
        private:
            Unit* const i_funit;
    };

    class AnyAssistCreatureInRangeCheck
    {
        public:
            AnyAssistCreatureInRangeCheck(Unit* funit, Unit* enemy, float range)
                : i_funit(funit), i_enemy(enemy), i_range(range)
            {
            }
            bool operator()(Creature* u)
            {
                if(u == i_funit)
                    return false;

                if ( !u->CanAssistTo(i_funit, i_enemy) )
                    return false;

                // too far
                if( !i_funit->IsWithinDistInMap(u, i_range) )
                    return false;

                // only if see assisted creature
                if( !i_funit->IsWithinLOSInMap(u) )
                    return false;

                return true;
            }
        private:
            Unit* const i_funit;
            Unit* const i_enemy;
            float i_range;
    };

    class NearestAssistCreatureInCreatureRangeCheck
    {
        public:
            NearestAssistCreatureInCreatureRangeCheck(Creature* obj, Unit* enemy, float range)
                : i_obj(obj), i_enemy(enemy), i_range(range) {}

            bool operator()(Creature* u)
            {
                if(u == i_obj)
                    return false;
                if(!u->CanAssistTo(i_obj,i_enemy))
                    return false;

                if(!i_obj->IsWithinDistInMap(u, i_range))
                    return false;

                if(!i_obj->IsWithinLOSInMap(u))
                    return false;

                i_range = i_obj->GetDistance(u);            // use found unit range as new range limit for next check
                return true;
            }
            float GetLastRange() const { return i_range; }
        private:
            Creature* const i_obj;
            Unit* const i_enemy;
            float  i_range;

            // prevent clone this object
            NearestAssistCreatureInCreatureRangeCheck(NearestAssistCreatureInCreatureRangeCheck const&);
    };

    // Success at unit in range, range update for next check (this can be use with CreatureLastSearcher to find nearest creature)
    class NearestCreatureEntryWithLiveStateInObjectRangeCheck
    {
        public:
            NearestCreatureEntryWithLiveStateInObjectRangeCheck(WorldObject const& obj,uint32 entry, bool alive, float range)
                : i_obj(obj), i_entry(entry), i_alive(alive), i_range(range) {}

            bool operator()(Creature* u)
            {
                if(u->GetEntry() == i_entry && u->isAlive()==i_alive && i_obj.IsWithinDistInMap(u, i_range))
                {
                    i_range = i_obj.GetDistance(u);         // use found unit range as new range limit for next check
                    return true;
                }
                return false;
            }
            float GetLastRange() const { return i_range; }
        private:
            WorldObject const& i_obj;
            uint32 i_entry;
            bool   i_alive;
            float  i_range;

            // prevent clone this object
            NearestCreatureEntryWithLiveStateInObjectRangeCheck(NearestCreatureEntryWithLiveStateInObjectRangeCheck const&);
    };

    class AnyPlayerInObjectRangeCheck
    {
    public:
        AnyPlayerInObjectRangeCheck(WorldObject const* obj, float range) : i_obj(obj), i_range(range) {}
        bool operator()(Player* u)
        {
            if(u->isAlive() && i_obj->IsWithinDistInMap(u, i_range))
                return true;

            return false;
        }
    private:
        WorldObject const* i_obj;
        float i_range;
    };

    // Player checks and do

    // Prepare using Builder localized packets with caching and send to player
    template<class Builder>
    class LocalizedPacketDo
    {
        public:
            explicit LocalizedPacketDo(Builder& builder) : i_builder(builder) {}

            ~LocalizedPacketDo()
            {
                for(size_t i = 0; i < i_data_cache.size(); ++i)
                    delete i_data_cache[i];
            }
            void operator()( Player* p );

        private:
            Builder& i_builder;
            std::vector<WorldPacket*> i_data_cache;         // 0 = default, i => i-1 locale index
    };

    // Prepare using Builder localized packets with caching and send to player
    template<class Builder>
    class LocalizedPacketListDo
    {
        public:
            typedef std::vector<WorldPacket*> WorldPacketList;
            explicit LocalizedPacketListDo(Builder& builder) : i_builder(builder) {}

            ~LocalizedPacketListDo()
            {
                for(size_t i = 0; i < i_data_cache.size(); ++i)
                    for(size_t j = 0; j < i_data_cache[i].size(); ++j)
                        delete i_data_cache[i][j];
            }
            void operator()( Player* p );

        private:
            Builder& i_builder;
            std::vector<WorldPacketList> i_data_cache;
                                                            // 0 = default, i => i-1 locale index
    };

    #ifndef WIN32
    template<> void PlayerRelocationNotifier::Visit<Creature>(CreatureMapType &);
    template<> void PlayerRelocationNotifier::Visit<Player>(PlayerMapType &);
    template<> void CreatureRelocationNotifier::Visit<Player>(PlayerMapType &);
    template<> void CreatureRelocationNotifier::Visit<Creature>(CreatureMapType &);
    template<> inline void DynamicObjectUpdater::Visit<Creature>(CreatureMapType &);
    template<> inline void DynamicObjectUpdater::Visit<Player>(PlayerMapType &);
    #endif
}
#endif
