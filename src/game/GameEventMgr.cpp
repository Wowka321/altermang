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

#include "GameEventMgr.h"
#include "World.h"
#include "ObjectMgr.h"
#include "ObjectGuid.h"
#include "PoolManager.h"
#include "ProgressBar.h"
#include "Language.h"
#include "Log.h"
#include "MapManager.h"
#include "BattleGroundMgr.h"
#include "Policies/SingletonImp.h"

INSTANTIATE_SINGLETON_1(GameEventMgr);

bool GameEventMgr::CheckOneGameEvent(uint16 entry) const
{
    // Get the event information
    time_t currenttime = time(NULL);
    if( mGameEvent[entry].start < currenttime && currenttime < mGameEvent[entry].end &&
        ((currenttime - mGameEvent[entry].start) % (mGameEvent[entry].occurence * MINUTE)) < (mGameEvent[entry].length * MINUTE) )
        return true;
    else
        return false;
}

uint32 GameEventMgr::NextCheck(uint16 entry) const
{
    time_t currenttime = time(NULL);

    // outdated event: we return max
    if (currenttime > mGameEvent[entry].end)
        return max_ge_check_delay;

    // never started event, we return delay before start
    if (mGameEvent[entry].start > currenttime)
        return uint32(mGameEvent[entry].start - currenttime);

    uint32 delay;
    // in event, we return the end of it
    if ((((currenttime - mGameEvent[entry].start) % (mGameEvent[entry].occurence * 60)) < (mGameEvent[entry].length * 60)))
        // we return the delay before it ends
        delay = (mGameEvent[entry].length * MINUTE) - ((currenttime - mGameEvent[entry].start) % (mGameEvent[entry].occurence * MINUTE));
    else                                                    // not in window, we return the delay before next start
        delay = (mGameEvent[entry].occurence * MINUTE) - ((currenttime - mGameEvent[entry].start) % (mGameEvent[entry].occurence * MINUTE));
    // In case the end is before next check
    if (mGameEvent[entry].end  < time_t(currenttime + delay))
        return uint32(mGameEvent[entry].end - currenttime);
    else
        return delay;
}

void GameEventMgr::StartEvent( uint16 event_id, bool overwrite )
{
    AddActiveEvent(event_id);
    ApplyNewEvent(event_id);
    if(overwrite)
    {
        mGameEvent[event_id].start = time(NULL);
        if(mGameEvent[event_id].end <= mGameEvent[event_id].start)
            mGameEvent[event_id].end = mGameEvent[event_id].start+mGameEvent[event_id].length;
    }
}

void GameEventMgr::StopEvent( uint16 event_id, bool overwrite )
{
    RemoveActiveEvent(event_id);
    UnApplyEvent(event_id);
    if(overwrite)
    {
        mGameEvent[event_id].start = time(NULL) - mGameEvent[event_id].length * MINUTE;
        if(mGameEvent[event_id].end <= mGameEvent[event_id].start)
            mGameEvent[event_id].end = mGameEvent[event_id].start+mGameEvent[event_id].length;
    }
}

void GameEventMgr::LoadFromDB()
{
    {
        QueryResult *result = WorldDatabase.Query("SELECT MAX(entry) FROM game_event");
        if( !result )
        {
            sLog.outString(">> Table game_event is empty.");
            sLog.outString();
            return;
        }

        Field *fields = result->Fetch();

        uint32 max_event_id = fields[0].GetUInt16();
        delete result;

        mGameEvent.resize(max_event_id+1);
    }

    QueryResult *result = WorldDatabase.Query("SELECT entry,UNIX_TIMESTAMP(start_time),UNIX_TIMESTAMP(end_time),occurence,length,holiday,description FROM game_event");
    if( !result )
    {
        mGameEvent.clear();
        sLog.outString(">> Table game_event is empty!");
        sLog.outString();
        return;
    }

    uint32 count = 0;

    {
        barGoLink bar( (int)result->GetRowCount() );
        do
        {
            ++count;
            Field *fields = result->Fetch();

            bar.step();

            uint16 event_id = fields[0].GetUInt16();
            if(event_id==0)
            {
                sLog.outErrorDb("`game_event` game event id (%i) is reserved and can't be used.",event_id);
                continue;
            }

            GameEventData& pGameEvent = mGameEvent[event_id];
            uint64 starttime        = fields[1].GetUInt64();
            pGameEvent.start        = time_t(starttime);
            uint64 endtime          = fields[2].GetUInt64();
            pGameEvent.end          = time_t(endtime);
            pGameEvent.occurence    = fields[3].GetUInt32();
            pGameEvent.length       = fields[4].GetUInt32();
            pGameEvent.holiday_id   = HolidayIds(fields[5].GetUInt32());


            if(pGameEvent.length==0)                            // length>0 is validity check
            {
                sLog.outErrorDb("`game_event` game event id (%i) have length 0 and can't be used.",event_id);
                continue;
            }

            if(pGameEvent.holiday_id != HOLIDAY_NONE)
            {
                if(!sHolidaysStore.LookupEntry(pGameEvent.holiday_id))
                {
                    sLog.outErrorDb("`game_event` game event id (%i) have not existed holiday id %u.",event_id,pGameEvent.holiday_id);
                    pGameEvent.holiday_id = HOLIDAY_NONE;
                }
            }

            pGameEvent.description  = fields[6].GetCppString();

        } while( result->NextRow() );
        delete result;

        sLog.outString();
        sLog.outString( ">> Loaded %u game events", count );
    }

    std::map<uint16,int16> pool2event;                      // for check unique spawn event associated with pool 
    std::map<uint32,int16> creature2event;                  // for check unique spawn event associated with creature 
    std::map<uint32,int16> go2event;                        // for check unique spawn event associated with gameobject

    // list only positive event top pools, filled at creature/gameobject loading
    mGameEventSpawnPoolIds.resize(mGameEvent.size());

    mGameEventCreatureGuids.resize(mGameEvent.size()*2-1);
    //                                   1              2
    result = WorldDatabase.Query("SELECT creature.guid, game_event_creature.event "
        "FROM creature JOIN game_event_creature ON creature.guid = game_event_creature.guid");

    count = 0;
    if( !result )
    {
        barGoLink bar(1);
        bar.step();

        sLog.outString();
        sLog.outString(">> Loaded %u creatures in game events", count );
    }
    else
    {

        barGoLink bar( (int)result->GetRowCount() );
        do
        {
            Field *fields = result->Fetch();

            bar.step();

            uint32 guid    = fields[0].GetUInt32();
            int16 event_id = fields[1].GetInt16();

            if (event_id == 0)
            {
                sLog.outErrorDb("`game_event_creature` game event id (%i) not allowed",event_id);
                continue;
            }

            int32 internal_event_id = mGameEvent.size() + event_id - 1;

            if(internal_event_id < 0 || (size_t)internal_event_id >= mGameEventCreatureGuids.size())
            {
                sLog.outErrorDb("`game_event_creature` game event id (%i) is out of range compared to max event id in `game_event`",event_id);
                continue;
            }

            ++count;

            // spawn objects at event can be grouped in pools and then affected pools have stricter requirements for this case
            if (event_id > 0)                   
            {
                creature2event[guid] = event_id;

                // not list explicitly creatures from pools in event creature list
                if (uint16 topPoolId =  sPoolMgr.IsPartOfTopPool<Creature>(guid))
                {
                    int16& eventRef = pool2event[topPoolId];
                    if (eventRef != 0)
                    {
                        if (eventRef != event_id)
                            sLog.outErrorDb("`game_event_creature` have creature (GUID: %u) for event %i from pool or subpool of pool (ID: %u) but pool have already content from event %i. Pool don't must have content for different events!", guid, event_id, topPoolId, eventRef);
                    }
                    else
                    {
                        eventRef = event_id;
                        mGameEventSpawnPoolIds[event_id].push_back(topPoolId);
                        sPoolMgr.RemoveAutoSpawnForPool(topPoolId);
                    }

                    continue;
                }
            }

            GuidList& crelist = mGameEventCreatureGuids[internal_event_id];
            crelist.push_back(guid);

        } while( result->NextRow() );
        delete result;

        sLog.outString();
        sLog.outString( ">> Loaded %u creatures in game events", count );
    }

    mGameEventGameobjectGuids.resize(mGameEvent.size()*2-1);
    //                                   1                2
    result = WorldDatabase.Query("SELECT gameobject.guid, game_event_gameobject.event "
        "FROM gameobject JOIN game_event_gameobject ON gameobject.guid=game_event_gameobject.guid");

    count = 0;
    if( !result )
    {
        barGoLink bar(1);
        bar.step();

        sLog.outString();
        sLog.outString(">> Loaded %u gameobjects in game events", count );
    }
    else
    {

        barGoLink bar( (int)result->GetRowCount() );
        do
        {
            Field *fields = result->Fetch();

            bar.step();

            uint32 guid    = fields[0].GetUInt32();
            int16 event_id = fields[1].GetInt16();

            if (event_id == 0)
            {
                sLog.outErrorDb("`game_event_gameobject` game event id (%i) not allowed",event_id);
                continue;
            }

            int32 internal_event_id = mGameEvent.size() + event_id - 1;

            if(internal_event_id < 0 || (size_t)internal_event_id >= mGameEventGameobjectGuids.size())
            {
                sLog.outErrorDb("`game_event_gameobject` game event id (%i) is out of range compared to max event id in `game_event`",event_id);
                continue;
            }

            ++count;

            // spawn objects at event can be grouped in pools and then affected pools have stricter requirements for this case
            if (event_id > 0)                   
            {
                go2event[guid] = event_id;

                // not list explicitly gameobjects from pools in event gameobject list
                if (uint16 topPoolId =  sPoolMgr.IsPartOfTopPool<GameObject>(guid))
                {
                    int16& eventRef = pool2event[topPoolId];
                    if (eventRef != 0)
                    {
                        if (eventRef != event_id)
                            sLog.outErrorDb("`game_event_gameobject` have gameobject (GUID: %u) for event %i from pool or subpool of pool (ID: %u) but pool have already content from event %i. Pool don't must have content for different events!", guid, event_id, topPoolId, eventRef);
                    }
                    else
                    {
                        eventRef = event_id;
                        mGameEventSpawnPoolIds[event_id].push_back(topPoolId);
                        sPoolMgr.RemoveAutoSpawnForPool(topPoolId);
                    }

                    continue;
                }
            }

            GuidList& golist = mGameEventGameobjectGuids[internal_event_id];
            golist.push_back(guid);

        } while( result->NextRow() );
        delete result;

        sLog.outString();
        sLog.outString( ">> Loaded %u gameobjects in game events", count );
    }

    // now recheck that all eventPools linked with events after our skip pools with parents
    for(std::map<uint16,int16>::const_iterator itr = pool2event.begin(); itr != pool2event.end();  ++itr)
    {
        uint16 pool_id = itr->first;
        int16 event_id = itr->second;

        sPoolMgr.CheckEventLinkAndReport(pool_id, event_id, creature2event, go2event);
    }

    mGameEventModelEquip.resize(mGameEvent.size());
    //                                   0              1                             2
    result = WorldDatabase.Query("SELECT creature.guid, game_event_model_equip.event, game_event_model_equip.modelid,"
    //   3
        "game_event_model_equip.equipment_id "
        "FROM creature JOIN game_event_model_equip ON creature.guid=game_event_model_equip.guid");

    count = 0;
    if( !result )
    {
        barGoLink bar(1);
        bar.step();

        sLog.outString();
        sLog.outString(">> Loaded %u model/equipment changes in game events", count );
    }
    else
    {

        barGoLink bar( (int)result->GetRowCount() );
        do
        {
            Field *fields = result->Fetch();

            bar.step();
            uint32 guid     = fields[0].GetUInt32();
            uint16 event_id = fields[1].GetUInt16();

            if(event_id >= mGameEventModelEquip.size())
            {
                sLog.outErrorDb("`game_event_model_equip` game event id (%u) is out of range compared to max event id in `game_event`",event_id);
                continue;
            }

            ++count;
            ModelEquipList& equiplist = mGameEventModelEquip[event_id];
            ModelEquip newModelEquipSet;
            newModelEquipSet.modelid = fields[2].GetUInt32();
            newModelEquipSet.equipment_id = fields[3].GetUInt32();
            newModelEquipSet.equipement_id_prev = 0;
            newModelEquipSet.modelid_prev = 0;

            if(newModelEquipSet.equipment_id > 0)
            {
                if(!sObjectMgr.GetEquipmentInfo(newModelEquipSet.equipment_id))
                {
                    sLog.outErrorDb("Table `game_event_model_equip` have creature (Guid: %u) with equipment_id %u not found in table `creature_equip_template`, set to no equipment.", guid, newModelEquipSet.equipment_id);
                    continue;
                }
            }

            equiplist.push_back(std::pair<uint32, ModelEquip>(guid, newModelEquipSet));

        } while( result->NextRow() );
        delete result;

        sLog.outString();
        sLog.outString( ">> Loaded %u model/equipment changes in game events", count );
    }

    mGameEventQuests.resize(mGameEvent.size());
    //                                   0   1      2
    result = WorldDatabase.Query("SELECT id, quest, event FROM game_event_creature_quest");

    count = 0;
    if( !result )
    {
        barGoLink bar(1);
        bar.step();

        sLog.outString();
        sLog.outString(">> Loaded %u quests additions in game events", count );
    }
    else
    {

        barGoLink bar( (int)result->GetRowCount() );
        do
        {
            Field *fields = result->Fetch();

            bar.step();
            uint32 id       = fields[0].GetUInt32();
            uint32 quest    = fields[1].GetUInt32();
            uint16 event_id = fields[2].GetUInt16();

            if(event_id >= mGameEventQuests.size())
            {
                sLog.outErrorDb("`game_event_creature_quest` game event id (%u) is out of range compared to max event id in `game_event`",event_id);
                continue;
            }

            ++count;
            QuestRelList& questlist = mGameEventQuests[event_id];
            questlist.push_back(QuestRelation(id, quest));

        } while( result->NextRow() );
        delete result;

        sLog.outString();
        sLog.outString( ">> Loaded %u quests additions in game events", count );
    }
}

uint32 GameEventMgr::Initialize()                           // return the next event delay in ms
{
    m_ActiveEvents.clear();
    uint32 delay = Update();
    BASIC_LOG("Game Event system initialized." );
    m_IsGameEventsInit = true;
    return delay;
}

uint32 GameEventMgr::Update()                               // return the next event delay in ms
{
    uint32 nextEventDelay = max_ge_check_delay;             // 1 day
    uint32 calcDelay;
    for (uint16 itr = 1; itr < mGameEvent.size(); ++itr)
    {
        //sLog.outErrorDb("Checking event %u",itr);
        if (CheckOneGameEvent(itr))
        {
            //DEBUG_LOG("GameEvent %u is active",itr->first);
            if (!IsActiveEvent(itr))
                StartEvent(itr);
        }
        else
        {
            //DEBUG_LOG("GameEvent %u is not active",itr->first);
            if (IsActiveEvent(itr))
                StopEvent(itr);
            else
            {
                if (!m_IsGameEventsInit)
                {
                    int16 event_nid = (-1) * (itr);
                    // spawn all negative ones for this event
                    GameEventSpawn(event_nid);

                    // disable any event specific quest (for cases where creature is spawned, but event not active).
                    UpdateEventQuests(itr, false);
                    UpdateWorldStates(itr, false);
                }
            }
        }
        calcDelay = NextCheck(itr);
        if (calcDelay < nextEventDelay)
            nextEventDelay = calcDelay;
    }
    BASIC_LOG("Next game event check in %u seconds.", nextEventDelay + 1);
    return (nextEventDelay + 1) * IN_MILLISECONDS;           // Add 1 second to be sure event has started/stopped at next call
}

void GameEventMgr::UnApplyEvent(uint16 event_id)
{
    sLog.outString("GameEvent %u \"%s\" removed.", event_id, mGameEvent[event_id].description.c_str());
    // un-spawn positive event tagged objects
    GameEventUnspawn(event_id);
    // spawn negative event tagget objects
    int16 event_nid = (-1) * event_id;
    GameEventSpawn(event_nid);
    // restore equipment or model
    ChangeEquipOrModel(event_id, false);
    // Remove quests that are events only to non event npc
    UpdateEventQuests(event_id, false);
    UpdateWorldStates(event_id, false);
}

void GameEventMgr::ApplyNewEvent(uint16 event_id)
{
    if (sWorld.getConfig(CONFIG_BOOL_EVENT_ANNOUNCE))
        sWorld.SendWorldText(LANG_EVENTMESSAGE, mGameEvent[event_id].description.c_str());

    sLog.outString("GameEvent %u \"%s\" started.", event_id, mGameEvent[event_id].description.c_str());
    // spawn positive event tagget objects
    GameEventSpawn(event_id);
    // un-spawn negative event tagged objects
    int16 event_nid = (-1) * event_id;
    GameEventUnspawn(event_nid);
    // Change equipement or model
    ChangeEquipOrModel(event_id, true);
    // Add quests that are events only to non event npc
    UpdateEventQuests(event_id, true);
    UpdateWorldStates(event_id, true);
}

void GameEventMgr::GameEventSpawn(int16 event_id)
{
    int32 internal_event_id = mGameEvent.size() + event_id - 1;

    if (internal_event_id < 0 || (size_t)internal_event_id >= mGameEventCreatureGuids.size())
    {
        sLog.outError("GameEventMgr::GameEventSpawn attempt access to out of range mGameEventCreatureGuids element %i (size: " SIZEFMTD ")",internal_event_id,mGameEventCreatureGuids.size());
        return;
    }

    for (GuidList::iterator itr = mGameEventCreatureGuids[internal_event_id].begin();itr != mGameEventCreatureGuids[internal_event_id].end();++itr)
    {
        // Add to correct cell
        CreatureData const* data = sObjectMgr.GetCreatureData(*itr);
        if (data)
        {
            // negative event id for pool element meaning allow be used in next pool spawn 
            if (event_id < 0)
            {
                if (uint16 pool_id = sPoolMgr.IsPartOfAPool<Creature>(*itr))
                {
                    // will have chance at next pool update
                    sPoolMgr.SetExcludeObject<Creature>(pool_id, *itr, false);
                    sPoolMgr.UpdatePool<Creature>(pool_id);
                    continue;
                }
            }

            sObjectMgr.AddCreatureToGrid(*itr, data);

            // Spawn if necessary (loaded grids only)
            Map* map = const_cast<Map*>(sMapMgr.CreateBaseMap(data->mapid));
            // We use spawn coords to spawn
            if(!map->Instanceable() && map->IsLoaded(data->posX,data->posY))
            {
                Creature* pCreature = new Creature;
                //DEBUG_LOG("Spawning creature %u",*itr);
                if (!pCreature->LoadFromDB(*itr, map))
                {
                    delete pCreature;
                }
                else
                {
                    map->Add(pCreature);
                }
            }
        }
    }

    if (internal_event_id < 0 || (size_t)internal_event_id >= mGameEventGameobjectGuids.size())
    {
        sLog.outError("GameEventMgr::GameEventSpawn attempt access to out of range mGameEventGameobjectGuids element %i (size: " SIZEFMTD ")",internal_event_id,mGameEventGameobjectGuids.size());
        return;
    }

    for (GuidList::iterator itr = mGameEventGameobjectGuids[internal_event_id].begin();itr != mGameEventGameobjectGuids[internal_event_id].end();++itr)
    {
        // Add to correct cell
        GameObjectData const* data = sObjectMgr.GetGOData(*itr);
        if (data)
        {
            // negative event id for pool element meaning allow be used in next pool spawn 
            if (event_id < 0)
            {
                if (uint16 pool_id = sPoolMgr.IsPartOfAPool<GameObject>(*itr))
                {
                    // will have chance at next pool update
                    sPoolMgr.SetExcludeObject<GameObject>(pool_id, *itr, false);
                    sPoolMgr.UpdatePool<GameObject>(pool_id);
                    continue;
                }
            }

            sObjectMgr.AddGameobjectToGrid(*itr, data);

            // Spawn if necessary (loaded grids only)
            // this base map checked as non-instanced and then only existed
            Map* map = const_cast<Map*>(sMapMgr.CreateBaseMap(data->mapid));
            // We use current coords to unspawn, not spawn coords since creature can have changed grid
            if(!map->Instanceable() && map->IsLoaded(data->posX, data->posY))
            {
                GameObject* pGameobject = new GameObject;
                //DEBUG_LOG("Spawning gameobject %u", *itr);
                if (!pGameobject->LoadFromDB(*itr, map))
                {
                    delete pGameobject;
                }
                else
                {
                    if(pGameobject->isSpawnedByDefault())
                        map->Add(pGameobject);
                }
            }
        }
    }

    if (event_id > 0)
    {
        if((size_t)event_id >= mGameEventSpawnPoolIds.size())
        {
            sLog.outError("GameEventMgr::GameEventSpawn attempt access to out of range mGameEventSpawnPoolIds element %i (size: " SIZEFMTD ")", event_id, mGameEventSpawnPoolIds.size());
            return;
        }

        for (IdList::iterator itr = mGameEventSpawnPoolIds[event_id].begin();itr != mGameEventSpawnPoolIds[event_id].end();++itr)
            sPoolMgr.SpawnPool(*itr, true);
    }
}

void GameEventMgr::GameEventUnspawn(int16 event_id)
{
    int32 internal_event_id = mGameEvent.size() + event_id - 1;

    if (internal_event_id < 0 || (size_t)internal_event_id >= mGameEventCreatureGuids.size())
    {
        sLog.outError("GameEventMgr::GameEventUnspawn attempt access to out of range mGameEventCreatureGuids element %i (size: " SIZEFMTD ")",internal_event_id,mGameEventCreatureGuids.size());
        return;
    }

    for (GuidList::iterator itr = mGameEventCreatureGuids[internal_event_id].begin();itr != mGameEventCreatureGuids[internal_event_id].end();++itr)
    {
        // Remove the creature from grid
        if( CreatureData const* data = sObjectMgr.GetCreatureData(*itr) )
        {
            // negative event id for pool element meaning unspawn in pool and exclude for next spawns
            if (event_id < 0)
            {
                if (uint16 poolid = sPoolMgr.IsPartOfAPool<Creature>(*itr))
                {
                    sPoolMgr.SetExcludeObject<Creature>(poolid, *itr, true);
                    sPoolMgr.UpdatePool<Creature>(poolid, *itr);
                    continue;
                }
            }

            sObjectMgr.RemoveCreatureFromGrid(*itr, data);

            if (Creature* pCreature = ObjectAccessor::GetCreatureInWorld(ObjectGuid(HIGHGUID_UNIT, data->id, *itr)))
                pCreature->AddObjectToRemoveList();
        }
    }

    if (internal_event_id < 0 || (size_t)internal_event_id >= mGameEventGameobjectGuids.size())
    {
        sLog.outError("GameEventMgr::GameEventUnspawn attempt access to out of range mGameEventGameobjectGuids element %i (size: " SIZEFMTD ")",internal_event_id,mGameEventGameobjectGuids.size());
        return;
    }

    for (GuidList::iterator itr = mGameEventGameobjectGuids[internal_event_id].begin();itr != mGameEventGameobjectGuids[internal_event_id].end();++itr)
    {
        // Remove the gameobject from grid
        if(GameObjectData const* data = sObjectMgr.GetGOData(*itr))
        {
            // negative event id for pool element meaning unspawn in pool and exclude for next spawns
            if (event_id < 0)
            {
                if (uint16 poolid = sPoolMgr.IsPartOfAPool<GameObject>(*itr))
                {
                    sPoolMgr.SetExcludeObject<GameObject>(poolid, *itr, true);
                    sPoolMgr.UpdatePool<GameObject>(poolid, *itr);
                    continue;
                }
            }

            sObjectMgr.RemoveGameobjectFromGrid(*itr, data);

            if( GameObject* pGameobject = ObjectAccessor::GetGameObjectInWorld(ObjectGuid(HIGHGUID_GAMEOBJECT, data->id, *itr)) )
                pGameobject->AddObjectToRemoveList();
        }
    }

    if (event_id > 0)
    {
        if ((size_t)event_id >= mGameEventSpawnPoolIds.size())
        {
            sLog.outError("GameEventMgr::GameEventUnspawn attempt access to out of range mGameEventSpawnPoolIds element %i (size: " SIZEFMTD ")", event_id, mGameEventSpawnPoolIds.size());
            return;
        }

        for (IdList::iterator itr = mGameEventSpawnPoolIds[event_id].begin();itr != mGameEventSpawnPoolIds[event_id].end();++itr)
        {
            sPoolMgr.DespawnPool(*itr);
        }
    }
}

void GameEventMgr::ChangeEquipOrModel(int16 event_id, bool activate)
{
    for(ModelEquipList::iterator itr = mGameEventModelEquip[event_id].begin();itr != mGameEventModelEquip[event_id].end();++itr)
    {
        // Remove the creature from grid
        CreatureData const* data = sObjectMgr.GetCreatureData(itr->first);
        if(!data)
            continue;

        // Update if spawned
        if (Creature* pCreature = ObjectAccessor::GetCreatureInWorld(ObjectGuid(HIGHGUID_UNIT, data->id, itr->first)))
        {
            if (activate)
            {
                itr->second.equipement_id_prev = pCreature->GetCurrentEquipmentId();
                itr->second.modelid_prev = pCreature->GetDisplayId();
                pCreature->LoadEquipment(itr->second.equipment_id, true);
                if (itr->second.modelid >0 && itr->second.modelid_prev != itr->second.modelid)
                {
                    CreatureModelInfo const *minfo = sObjectMgr.GetCreatureModelInfo(itr->second.modelid);
                    if (minfo)
                    {
                        pCreature->SetDisplayId(itr->second.modelid);
                        pCreature->SetNativeDisplayId(itr->second.modelid);
                    }
                }
            }
            else
            {
                pCreature->LoadEquipment(itr->second.equipement_id_prev, true);
                if (itr->second.modelid_prev >0 && itr->second.modelid_prev != itr->second.modelid)
                {
                    CreatureModelInfo const *minfo = sObjectMgr.GetCreatureModelInfo(itr->second.modelid_prev);
                    if (minfo)
                    {
                        pCreature->SetDisplayId(itr->second.modelid_prev);
                        pCreature->SetNativeDisplayId(itr->second.modelid_prev);
                    }
                }
            }
        }
        else                                                // If not spawned
        {
            CreatureData const* data2 = sObjectMgr.GetCreatureData(itr->first);
            if (data2 && activate)
            {
                CreatureInfo const *cinfo = ObjectMgr::GetCreatureTemplate(data2->id);
                uint32 display_id = sObjectMgr.ChooseDisplayId(0,cinfo,data2);
                CreatureModelInfo const *minfo = sObjectMgr.GetCreatureModelRandomGender(display_id);
                if (minfo)
                    display_id = minfo->modelid;

                if (data2->equipmentId == 0)
                    itr->second.equipement_id_prev = cinfo->equipmentId;
                else if (data2->equipmentId != -1)
                    itr->second.equipement_id_prev = data->equipmentId;
                itr->second.modelid_prev = display_id;
            }
        }
        // now last step: put in data
                                                            // just to have write access to it
        CreatureData& data2 = sObjectMgr.NewOrExistCreatureData(itr->first);
        if (activate)
        {
            data2.displayid = itr->second.modelid;
            data2.equipmentId = itr->second.equipment_id;
        }
        else
        {
            data2.displayid = itr->second.modelid_prev;
            data2.equipmentId = itr->second.equipement_id_prev;
        }
    }
}

void GameEventMgr::UpdateEventQuests(uint16 event_id, bool Activate)
{
    QuestRelList::iterator itr;
    for (itr = mGameEventQuests[event_id].begin();itr != mGameEventQuests[event_id].end();++itr)
    {
        QuestRelations &CreatureQuestMap = sObjectMgr.mCreatureQuestRelations;
        if (Activate)                                       // Add the pair(id,quest) to the multimap
            CreatureQuestMap.insert(QuestRelations::value_type(itr->first, itr->second));
        else
        {                                                   // Remove the pair(id,quest) from the multimap
            QuestRelations::iterator qitr = CreatureQuestMap.find(itr->first);
            if (qitr == CreatureQuestMap.end())
                continue;
            QuestRelations::iterator lastElement = CreatureQuestMap.upper_bound(itr->first);
            for ( ;qitr != lastElement;++qitr)
            {
                if (qitr->second == itr->second)
                {
                    CreatureQuestMap.erase(qitr);           // iterator is now no more valid
                    break;                                  // but we can exit loop since the element is found
                }
            }
        }
    }
}

void GameEventMgr::UpdateWorldStates(uint16 event_id, bool Activate)
{
    GameEventData const& event = mGameEvent[event_id];
    if (event.holiday_id != HOLIDAY_NONE)
    {
        BattleGroundTypeId bgTypeId = BattleGroundMgr::WeekendHolidayIdToBGType(event.holiday_id);
        if (bgTypeId != BATTLEGROUND_TYPE_NONE)
        {
            BattlemasterListEntry const * bl = sBattlemasterListStore.LookupEntry(bgTypeId);
            if (bl && bl->HolidayWorldStateId)
            {
                WorldPacket data;
                sBattleGroundMgr.BuildUpdateWorldStatePacket(&data, bl->HolidayWorldStateId, Activate ? 1 : 0);
                sWorld.SendGlobalMessage(&data);
            }
        }
    }
}

// Get the Game Event ID for Creature by guid
template <>
int16 GameEventMgr::GetGameEventId<Creature>(uint32 guid_or_poolid)
{
    for (uint16 i = 0; i < mGameEventCreatureGuids.size(); i++) // 0 <= i <= 2*(S := mGameEvent.size()) - 2
        for (GuidList::const_iterator itr = mGameEventCreatureGuids[i].begin(); itr != mGameEventCreatureGuids[i].end(); itr++)
            if (*itr == guid_or_poolid)
                    return i + 1 - mGameEvent.size();       // -S *1 + 1 <= . <= 1*S - 1
    return 0;
}

// Get the Game Event ID for GameObject by guid
template <>
int16 GameEventMgr::GetGameEventId<GameObject>(uint32 guid_or_poolid)
{
    for (uint16 i = 0; i < mGameEventGameobjectGuids.size(); i++)
        for (GuidList::const_iterator itr = mGameEventGameobjectGuids[i].begin(); itr != mGameEventGameobjectGuids[i].end(); itr++)
            if (*itr == guid_or_poolid)
                return i + 1 - mGameEvent.size();       // -S *1 + 1 <= . <= 1*S - 1
    return 0;
}

// Get the Game Event ID for Pool by pool ID
template <>
int16 GameEventMgr::GetGameEventId<Pool>(uint32 guid_or_poolid)
{
    for (uint16 i = 0; i < mGameEventSpawnPoolIds.size(); i++)
        for (IdList::const_iterator itr = mGameEventSpawnPoolIds[i].begin(); itr != mGameEventSpawnPoolIds[i].end(); itr++)
            if (*itr == guid_or_poolid)
                return i;
    return 0;
}

GameEventMgr::GameEventMgr()
{
    m_IsGameEventsInit = false;
}

MANGOS_DLL_SPEC bool IsHolidayActive( HolidayIds id )
{
    if (id == HOLIDAY_NONE)
        return false;

    GameEventMgr::GameEventDataMap const& events = sGameEventMgr.GetEventMap();
    GameEventMgr::ActiveEvents const& ae = sGameEventMgr.GetActiveEventList();

    for(GameEventMgr::ActiveEvents::const_iterator itr = ae.begin(); itr != ae.end(); ++itr)
        if (events[*itr].holiday_id == id)
            return true;

    return false;
}
