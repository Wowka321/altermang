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

#include "MapManager.h"
#include "InstanceSaveMgr.h"
#include "Policies/SingletonImp.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Transports.h"
#include "GridDefines.h"
#include "MapInstanced.h"
#include "DestinationHolderImp.h"
#include "World.h"
#include "CellImpl.h"
#include "Corpse.h"
#include "ObjectMgr.h"

#define CLASS_LOCK MaNGOS::ClassLevelLockable<MapManager, ACE_Thread_Mutex>
INSTANTIATE_SINGLETON_2(MapManager, CLASS_LOCK);
INSTANTIATE_CLASS_MUTEX(MapManager, ACE_Thread_Mutex);

extern GridState* si_GridStates[];                          // debugging code, should be deleted some day

MapManager::MapManager() : i_gridCleanUpDelay(sWorld.getConfig(CONFIG_UINT32_INTERVAL_GRIDCLEAN))
{
    i_timer.SetInterval(sWorld.getConfig(CONFIG_UINT32_INTERVAL_MAPUPDATE));
}

MapManager::~MapManager()
{
    for(MapMapType::iterator iter=i_maps.begin(); iter != i_maps.end(); ++iter)
        delete iter->second;

    for(TransportSet::iterator i = m_Transports.begin(); i != m_Transports.end(); ++i)
        delete *i;

    Map::DeleteStateMachine();
}

void
MapManager::Initialize()
{
    Map::InitStateMachine();

    // debugging code, should be deleted some day
    {
        for(int i=0;i<MAX_GRID_STATE; i++)
        {
            i_GridStates[i] = si_GridStates[i];
        }
        i_GridStateErrorCount = 0;
    }

    InitMaxInstanceId();
}

void MapManager::InitializeVisibilityDistanceInfo()
{
    for(MapMapType::iterator iter=i_maps.begin(); iter != i_maps.end(); ++iter)
        (*iter).second->InitVisibilityDistance();
}

// debugging code, should be deleted some day
void MapManager::checkAndCorrectGridStatesArray()
{
    bool ok = true;
    for(int i=0;i<MAX_GRID_STATE; i++)
    {
        if(i_GridStates[i] != si_GridStates[i])
        {
            sLog.outError("MapManager::checkGridStates(), GridState: si_GridStates is currupt !!!");
            ok = false;
            si_GridStates[i] = i_GridStates[i];
        }
        #ifdef MANGOS_DEBUG
        // inner class checking only when compiled with debug
        if(!si_GridStates[i]->checkMagic())
        {
            ok = false;
            si_GridStates[i]->setMagic();
        }
        #endif
    }
    if(!ok)
        ++i_GridStateErrorCount;
    if(i_GridStateErrorCount > 2)
        ASSERT(false);                                      // force a crash. Too many errors
}

Map*
MapManager::_createBaseMap(uint32 id)
{
    Map *m = _findMap(id);

    if( m == NULL )
    {
        Guard guard(*this);

        const MapEntry* entry = sMapStore.LookupEntry(id);
        if (entry && entry->Instanceable())
        {
            m = new MapInstanced(id, i_gridCleanUpDelay);
        }
        else
        {
            m = new Map(id, i_gridCleanUpDelay, 0, REGULAR_DIFFICULTY);
        }
        i_maps[id] = m;
    }

    ASSERT(m != NULL);
    return m;
}

Map* MapManager::CreateMap(uint32 id, const WorldObject* obj)
{
    ASSERT(obj);
    //if(!obj->IsInWorld()) sLog.outError("GetMap: called for map %d with object (typeid %d, guid %d, mapid %d, instanceid %d) who is not in world!", id, obj->GetTypeId(), obj->GetGUIDLow(), obj->GetMapId(), obj->GetInstanceId());
    Map *m = _createBaseMap(id);

    if (m && (obj->GetTypeId() == TYPEID_PLAYER) && m->Instanceable()) m = ((MapInstanced*)m)->CreateInstance(id, (Player*)obj);

    return m;
}

Map* MapManager::CreateBgMap(uint32 mapid, BattleGround* bg)
{
    Map *m = _createBaseMap(mapid);
    ((MapInstanced*)m)->CreateBattleGroundMap(sMapMgr.GenerateInstanceId(), bg);
    return m;
}

Map* MapManager::FindMap(uint32 mapid, uint32 instanceId) const
{
    Map *map = _findMap(mapid);
    if(!map)
        return NULL;

    if(!map->Instanceable())
        return instanceId == 0 ? map : NULL;

    return ((MapInstanced*)map)->FindMap(instanceId);
}

/*
    checks that do not require a map to be created
    will send transfer error messages on fail
*/
bool MapManager::CanPlayerEnter(uint32 mapid, Player* player)
{
    const MapEntry *entry = sMapStore.LookupEntry(mapid);
    if(!entry) return false;
    const char *mapName = entry->name[player->GetSession()->GetSessionDbcLocale()];

    if(entry->map_type == MAP_INSTANCE || entry->map_type == MAP_RAID)
    {
        if (entry->map_type == MAP_RAID)
        {
            // GMs can avoid raid limitations
            if(!player->isGameMaster() && !sWorld.getConfig(CONFIG_BOOL_INSTANCE_IGNORE_RAID))
            {
                // can only enter in a raid group
                Group* group = player->GetGroup();
                if (!group || !group->isRaidGroup())
                {
                    // probably there must be special opcode, because client has this string constant in GlobalStrings.lua
                    // TODO: this is not a good place to send the message
                    player->GetSession()->SendAreaTriggerMessage("You must be in a raid group to enter %s instance", mapName);
                    sLog.outDebug("MAP: Player '%s' must be in a raid group to enter instance of '%s'", player->GetName(), mapName);
                    return false;
                }
            }
        }

        //The player has a heroic mode and tries to enter into instance which has no a heroic mode
        MapDifficulty const* mapDiff = GetMapDifficultyData(entry->MapID,player->GetDifficulty(entry->map_type == MAP_RAID));
        if (!mapDiff)
        {
            bool isRegularTargetMap = player->GetDifficulty(entry->IsRaid()) == REGULAR_DIFFICULTY;

            //Send aborted message
            // FIX ME: what about absent normal/heroic mode with specific players limit...
            player->SendTransferAborted(mapid, TRANSFER_ABORT_DIFFICULTY, isRegularTargetMap ? DUNGEON_DIFFICULTY_NORMAL : DUNGEON_DIFFICULTY_HEROIC);
            return false;
        }

        if (!player->isAlive())
        {
            if(Corpse *corpse = player->GetCorpse())
            {
                // let enter in ghost mode in instance that connected to inner instance with corpse
                uint32 instance_map = corpse->GetMapId();
                do
                {
                    if(instance_map==mapid)
                        break;

                    InstanceTemplate const* instance = ObjectMgr::GetInstanceTemplate(instance_map);
                    instance_map = instance ? instance->parent : 0;
                }
                while (instance_map);

                if (!instance_map)
                {
                    player->GetSession()->SendAreaTriggerMessage("You cannot enter %s while in a ghost mode", mapName);
                    sLog.outDebug("MAP: Player '%s' doesn't has a corpse in instance '%s' and can't enter", player->GetName(), mapName);
                    return false;
                }
                sLog.outDebug("MAP: Player '%s' has corpse in instance '%s' and can enter", player->GetName(), mapName);
            }
            else
            {
                sLog.outDebug("Map::CanEnter - player '%s' is dead but doesn't have a corpse!", player->GetName());
            }
        }

        // TODO: move this to a map dependent location
        /*if(i_data && i_data->IsEncounterInProgress())
        {
            sLog.outDebug("MAP: Player '%s' can't enter instance '%s' while an encounter is in progress.", player->GetName(), GetMapName());
            player->SendTransferAborted(GetId(), TRANSFER_ABORT_ZONE_IN_COMBAT);
            return(false);
        }*/
        return true;
    }
    else
        return true;
}

void MapManager::DeleteInstance(uint32 mapid, uint32 instanceId)
{
    Map *m = _createBaseMap(mapid);
    if (m && m->Instanceable())
        ((MapInstanced*)m)->DestroyInstance(instanceId);
}

void MapManager::RemoveBonesFromMap(uint32 mapid, uint64 guid, float x, float y)
{
    bool remove_result = _createBaseMap(mapid)->RemoveBones(guid, x, y);

    if (!remove_result)
    {
        sLog.outDebug("Bones %u not found in world. Delete from DB also.", GUID_LOPART(guid));
    }
}

void
MapManager::Update(uint32 diff)
{
    i_timer.Update(diff);
    if( !i_timer.Passed() )
        return;

    for(MapMapType::iterator iter=i_maps.begin(); iter != i_maps.end(); ++iter)
    {
        checkAndCorrectGridStatesArray();                   // debugging code, should be deleted some day
        iter->second->Update((uint32)i_timer.GetCurrent());
    }

    for (TransportSet::iterator iter = m_Transports.begin(); iter != m_Transports.end(); ++iter)
        (*iter)->Update(i_timer.GetCurrent());

    i_timer.SetCurrent(0);
}

void MapManager::RemoveAllObjectsInRemoveList()
{
    for(MapMapType::iterator iter=i_maps.begin(); iter != i_maps.end(); ++iter)
        iter->second->RemoveAllObjectsInRemoveList();
}

bool MapManager::ExistMapAndVMap(uint32 mapid, float x,float y)
{
    GridPair p = MaNGOS::ComputeGridPair(x,y);

    int gx=63-p.x_coord;
    int gy=63-p.y_coord;

    return Map::ExistMap(mapid,gx,gy) && Map::ExistVMap(mapid,gx,gy);
}

bool MapManager::IsValidMAP(uint32 mapid)
{
    MapEntry const* mEntry = sMapStore.LookupEntry(mapid);
    return mEntry && (!mEntry->IsDungeon() || ObjectMgr::GetInstanceTemplate(mapid));
    // TODO: add check for battleground template
}

void MapManager::UnloadAll()
{
    for(MapMapType::iterator iter=i_maps.begin(); iter != i_maps.end(); ++iter)
        iter->second->UnloadAll(true);

    while(!i_maps.empty())
    {
        delete i_maps.begin()->second;
        i_maps.erase(i_maps.begin());
    }
}

void MapManager::InitMaxInstanceId()
{
    i_MaxInstanceId = 0;

    QueryResult *result = CharacterDatabase.Query( "SELECT MAX(id) FROM instance" );
    if( result )
    {
        i_MaxInstanceId = result->Fetch()[0].GetUInt32();
        delete result;
    }
}

uint32 MapManager::GetNumInstances()
{
    uint32 ret = 0;
    for(MapMapType::iterator itr = i_maps.begin(); itr != i_maps.end(); ++itr)
    {
        Map *map = itr->second;
        if(!map->Instanceable()) continue;
        MapInstanced::InstancedMaps &maps = ((MapInstanced *)map)->GetInstancedMaps();
        for(MapInstanced::InstancedMaps::iterator mitr = maps.begin(); mitr != maps.end(); ++mitr)
            if(mitr->second->IsDungeon()) ret++;
    }
    return ret;
}

uint32 MapManager::GetNumPlayersInInstances()
{
    uint32 ret = 0;
    for(MapMapType::iterator itr = i_maps.begin(); itr != i_maps.end(); ++itr)
    {
        Map *map = itr->second;
        if(!map->Instanceable()) continue;
        MapInstanced::InstancedMaps &maps = ((MapInstanced *)map)->GetInstancedMaps();
        for(MapInstanced::InstancedMaps::iterator mitr = maps.begin(); mitr != maps.end(); ++mitr)
            if(mitr->second->IsDungeon())
                ret += ((InstanceMap*)mitr->second)->GetPlayers().getSize();
    }
    return ret;
}
