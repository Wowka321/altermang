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
#include "Database/DatabaseEnv.h"
#include "DBCStores.h"
#include "ObjectMgr.h"
#include "ObjectDefines.h"
#include "Player.h"
#include "Item.h"
#include "GameObject.h"
#include "Opcodes.h"
#include "Chat.h"
#include "ObjectAccessor.h"
#include "MapManager.h"
#include "Language.h"
#include "World.h"
#include "GameEventMgr.h"
#include "SpellMgr.h"
#include "PoolManager.h"
#include "AccountMgr.h"
#include "GMTicketMgr.h"
#include "WaypointManager.h"
#include "Util.h"
#include <cctype>
#include <iostream>
#include <fstream>
#include <map>
#include "GlobalEvents.h"

#include "TargetedMovementGenerator.h"                      // for HandleNpcUnFollowCommand

static uint32 ReputationRankStrIndex[MAX_REPUTATION_RANK] =
{
    LANG_REP_HATED,    LANG_REP_HOSTILE, LANG_REP_UNFRIENDLY, LANG_REP_NEUTRAL,
    LANG_REP_FRIENDLY, LANG_REP_HONORED, LANG_REP_REVERED,    LANG_REP_EXALTED
};

//mute player for some times
bool ChatHandler::HandleMuteCommand(const char* args)
{
    char* nameStr;
    char* delayStr;
    extractOptFirstArg((char*)args,&nameStr,&delayStr);
    if(!delayStr)
        return false;

    Player* target;
    uint64 target_guid;
    std::string target_name;
    if(!extractPlayerTarget(nameStr,&target,&target_guid,&target_name))
        return false;

    uint32 account_id = target ? target->GetSession()->GetAccountId() : sObjectMgr.GetPlayerAccountIdByGUID(target_guid);

    // find only player from same account if any
    if(!target)
    {
        if(WorldSession* session = sWorld.FindSession(account_id))
            target = session->GetPlayer();
    }

    uint32 notspeaktime = (uint32) atoi(delayStr);

    // must have strong lesser security level
    if(HasLowerSecurity (target,target_guid,true))
        return false;

    time_t mutetime = time(NULL) + notspeaktime*60;

    if (target)
        target->GetSession()->m_muteTime = mutetime;

    loginDatabase.PExecute("UPDATE account SET mutetime = " UI64FMTD " WHERE id = '%u'",uint64(mutetime), account_id );

    if(target)
        ChatHandler(target).PSendSysMessage(LANG_YOUR_CHAT_DISABLED, notspeaktime);

    std::string nameLink = playerLink(target_name);

    PSendSysMessage(LANG_YOU_DISABLE_CHAT, nameLink.c_str(), notspeaktime);
    return true;
}

//unmute player
bool ChatHandler::HandleUnmuteCommand(const char* args)
{
    Player* target;
    uint64 target_guid;
    std::string target_name;
    if(!extractPlayerTarget((char*)args,&target,&target_guid,&target_name))
        return false;

    uint32 account_id = target ? target->GetSession()->GetAccountId() : sObjectMgr.GetPlayerAccountIdByGUID(target_guid);

    // find only player from same account if any
    if(!target)
    {
        if(WorldSession* session = sWorld.FindSession(account_id))
            target = session->GetPlayer();
    }

    // must have strong lesser security level
    if(HasLowerSecurity (target,target_guid,true))
        return false;

    if (target)
    {
        if(target->CanSpeak())
        {
            SendSysMessage(LANG_CHAT_ALREADY_ENABLED);
            SetSentErrorMessage(true);
            return false;
        }

        target->GetSession()->m_muteTime = 0;
    }

    loginDatabase.PExecute("UPDATE account SET mutetime = '0' WHERE id = '%u'", account_id );

    if(target)
        ChatHandler(target).PSendSysMessage(LANG_YOUR_CHAT_ENABLED);

    std::string nameLink = playerLink(target_name);

    PSendSysMessage(LANG_YOU_ENABLE_CHAT, nameLink.c_str());
    return true;
}

bool ChatHandler::HandleGoTriggerCommand(const char* args)
{
    Player* _player = m_session->GetPlayer();

    if (!*args)
        return false;

    char *atId = strtok((char*)args, " ");
    if (!atId)
        return false;

    int32 i_atId = atoi(atId);

    if(!i_atId)
        return false;

    AreaTriggerEntry const* at = sAreaTriggerStore.LookupEntry(i_atId);
    if (!at)
    {
        PSendSysMessage(LANG_COMMAND_GOAREATRNOTFOUND,i_atId);
        SetSentErrorMessage(true);
        return false;
    }

    if(!MapManager::IsValidMapCoord(at->mapid,at->x,at->y,at->z))
    {
        PSendSysMessage(LANG_INVALID_TARGET_COORD,at->x,at->y,at->mapid);
        SetSentErrorMessage(true);
        return false;
    }

    // stop flight if need
    if(_player->isInFlight())
    {
        _player->GetMotionMaster()->MovementExpired();
        _player->m_taxi.ClearTaxiDestinations();
    }
    // save only in non-flight case
    else
        _player->SaveRecallPosition();

    _player->TeleportTo(at->mapid, at->x, at->y, at->z, _player->GetOrientation());
    return true;
}

bool ChatHandler::HandleGoGraveyardCommand(const char* args)
{
    Player* _player = m_session->GetPlayer();

    if (!*args)
        return false;

    char *gyId = strtok((char*)args, " ");
    if (!gyId)
        return false;

    int32 i_gyId = atoi(gyId);

    if(!i_gyId)
        return false;

    WorldSafeLocsEntry const* gy = sWorldSafeLocsStore.LookupEntry(i_gyId);
    if (!gy)
    {
        PSendSysMessage(LANG_COMMAND_GRAVEYARDNOEXIST,i_gyId);
        SetSentErrorMessage(true);
        return false;
    }

    if(!MapManager::IsValidMapCoord(gy->map_id,gy->x,gy->y,gy->z))
    {
        PSendSysMessage(LANG_INVALID_TARGET_COORD,gy->x,gy->y,gy->map_id);
        SetSentErrorMessage(true);
        return false;
    }

    // stop flight if need
    if(_player->isInFlight())
    {
        _player->GetMotionMaster()->MovementExpired();
        _player->m_taxi.ClearTaxiDestinations();
    }
    // save only in non-flight case
    else
        _player->SaveRecallPosition();

    _player->TeleportTo(gy->map_id, gy->x, gy->y, gy->z, _player->GetOrientation());
    return true;
}

/** \brief Teleport the GM to the specified creature
*
* .gocreature <GUID>      --> TP using creature.guid
* .gocreature azuregos    --> TP player to the mob with this name
*                             Warning: If there is more than one mob with this name
*                                      you will be teleported to the first one that is found.
* .gocreature id 6109     --> TP player to the mob, that has this creature_template.entry
*                             Warning: If there is more than one mob with this "id"
*                                      you will be teleported to the first one that is found.
*/
//teleport to creature
bool ChatHandler::HandleGoCreatureCommand(const char* args)
{
    if(!*args)
        return false;
    Player* _player = m_session->GetPlayer();

    // "id" or number or [name] Shift-click form |color|Hcreature_entry:creature_id|h[name]|h|r
    char* pParam1 = extractKeyFromLink((char*)args,"Hcreature");
    if (!pParam1)
        return false;

    std::ostringstream whereClause;

    // User wants to teleport to the NPC's template entry
    if( strcmp(pParam1, "id") == 0 )
    {
        //sLog.outError("DEBUG: ID found");

        // Get the "creature_template.entry"
        // number or [name] Shift-click form |color|Hcreature_entry:creature_id|h[name]|h|r
        char* tail = strtok(NULL,"");
        if(!tail)
            return false;
        char* cId = extractKeyFromLink(tail,"Hcreature_entry");
        if(!cId)
            return false;

        int32 tEntry = atoi(cId);
        //sLog.outError("DEBUG: ID value: %d", tEntry);
        if(!tEntry)
            return false;

        whereClause << "WHERE id = '" << tEntry << "'";
    }
    else
    {
        //sLog.outError("DEBUG: ID *not found*");

        int32 guid = atoi(pParam1);

        // Number is invalid - maybe the user specified the mob's name
        if(!guid)
        {
            std::string name = pParam1;
            WorldDatabase.escape_string(name);
            whereClause << ", creature_template WHERE creature.id = creature_template.entry AND creature_template.name "_LIKE_" '" << name << "'";
        }
        else
        {
            whereClause <<  "WHERE guid = '" << guid << "'";
        }
    }
    //sLog.outError("DEBUG: %s", whereClause.c_str());

    QueryResult *result = WorldDatabase.PQuery("SELECT position_x,position_y,position_z,orientation,map FROM creature %s", whereClause.str().c_str() );
    if (!result)
    {
        SendSysMessage(LANG_COMMAND_GOCREATNOTFOUND);
        SetSentErrorMessage(true);
        return false;
    }
    if( result->GetRowCount() > 1 )
    {
        SendSysMessage(LANG_COMMAND_GOCREATMULTIPLE);
    }

    Field *fields = result->Fetch();
    float x = fields[0].GetFloat();
    float y = fields[1].GetFloat();
    float z = fields[2].GetFloat();
    float ort = fields[3].GetFloat();
    int mapid = fields[4].GetUInt16();

    delete result;

    if(!MapManager::IsValidMapCoord(mapid,x,y,z,ort))
    {
        PSendSysMessage(LANG_INVALID_TARGET_COORD,x,y,mapid);
        SetSentErrorMessage(true);
        return false;
    }

    // stop flight if need
    if(_player->isInFlight())
    {
        _player->GetMotionMaster()->MovementExpired();
        _player->m_taxi.ClearTaxiDestinations();
    }
    // save only in non-flight case
    else
        _player->SaveRecallPosition();

    _player->TeleportTo(mapid, x, y, z, ort);
    return true;
}

//teleport to gameobject
bool ChatHandler::HandleGoObjectCommand(const char* args)
{
    if(!*args)
        return false;

    Player* _player = m_session->GetPlayer();

    // number or [name] Shift-click form |color|Hgameobject:go_guid|h[name]|h|r
    char* cId = extractKeyFromLink((char*)args,"Hgameobject");
    if(!cId)
        return false;

    int32 guid = atoi(cId);
    if(!guid)
        return false;

    float x, y, z, ort;
    int mapid;

    // by DB guid
    if (GameObjectData const* go_data = sObjectMgr.GetGOData(guid))
    {
        x = go_data->posX;
        y = go_data->posY;
        z = go_data->posZ;
        ort = go_data->orientation;
        mapid = go_data->mapid;
    }
    else
    {
        SendSysMessage(LANG_COMMAND_GOOBJNOTFOUND);
        SetSentErrorMessage(true);
        return false;
    }

    if(!MapManager::IsValidMapCoord(mapid,x,y,z,ort))
    {
        PSendSysMessage(LANG_INVALID_TARGET_COORD,x,y,mapid);
        SetSentErrorMessage(true);
        return false;
    }

    // stop flight if need
    if(_player->isInFlight())
    {
        _player->GetMotionMaster()->MovementExpired();
        _player->m_taxi.ClearTaxiDestinations();
    }
    // save only in non-flight case
    else
        _player->SaveRecallPosition();

    _player->TeleportTo(mapid, x, y, z, ort);
    return true;
}

bool ChatHandler::HandleGameObjectTargetCommand(const char* args)
{
    Player* pl = m_session->GetPlayer();
    QueryResult *result;
    GameEventMgr::ActiveEvents const& activeEventsList = sGameEventMgr.GetActiveEventList();
    if(*args)
    {
        // number or [name] Shift-click form |color|Hgameobject_entry:go_id|h[name]|h|r
        char* cId = extractKeyFromLink((char*)args,"Hgameobject_entry");
        if(!cId)
            return false;

        uint32 id = atol(cId);

        if(id)
            result = WorldDatabase.PQuery("SELECT guid, id, position_x, position_y, position_z, orientation, map, (POW(position_x - '%f', 2) + POW(position_y - '%f', 2) + POW(position_z - '%f', 2)) AS order_ FROM gameobject WHERE map = '%i' AND id = '%u' ORDER BY order_ ASC LIMIT 1",
                pl->GetPositionX(), pl->GetPositionY(), pl->GetPositionZ(), pl->GetMapId(),id);
        else
        {
            std::string name = cId;
            WorldDatabase.escape_string(name);
            result = WorldDatabase.PQuery(
                "SELECT guid, id, position_x, position_y, position_z, orientation, map, (POW(position_x - %f, 2) + POW(position_y - %f, 2) + POW(position_z - %f, 2)) AS order_ "
                "FROM gameobject,gameobject_template WHERE gameobject_template.entry = gameobject.id AND map = %i AND name "_LIKE_" "_CONCAT3_("'%%'","'%s'","'%%'")" ORDER BY order_ ASC LIMIT 1",
                pl->GetPositionX(), pl->GetPositionY(), pl->GetPositionZ(), pl->GetMapId(),name.c_str());
        }
    }
    else
    {
        std::ostringstream eventFilter;
        eventFilter << " AND (event IS NULL ";
        bool initString = true;

        for (GameEventMgr::ActiveEvents::const_iterator itr = activeEventsList.begin(); itr != activeEventsList.end(); ++itr)
        {
            if (initString)
            {
                eventFilter  <<  "OR event IN (" <<*itr;
                initString =false;
            }
            else
                eventFilter << "," << *itr;
        }

        if (!initString)
            eventFilter << "))";
        else
            eventFilter << ")";

        result = WorldDatabase.PQuery("SELECT gameobject.guid, id, position_x, position_y, position_z, orientation, map, "
            "(POW(position_x - %f, 2) + POW(position_y - %f, 2) + POW(position_z - %f, 2)) AS order_ FROM gameobject "
            "LEFT OUTER JOIN game_event_gameobject on gameobject.guid=game_event_gameobject.guid WHERE map = '%i' %s ORDER BY order_ ASC LIMIT 10",
            m_session->GetPlayer()->GetPositionX(), m_session->GetPlayer()->GetPositionY(), m_session->GetPlayer()->GetPositionZ(), m_session->GetPlayer()->GetMapId(),eventFilter.str().c_str());
    }

    if (!result)
    {
        SendSysMessage(LANG_COMMAND_TARGETOBJNOTFOUND);
        return true;
    }

    bool found = false;
    float x, y, z, o;
    uint32 lowguid, id;
    uint16 mapid, pool_id;

    do
    {
        Field *fields = result->Fetch();
        lowguid = fields[0].GetUInt32();
        id =      fields[1].GetUInt32();
        x =       fields[2].GetFloat();
        y =       fields[3].GetFloat();
        z =       fields[4].GetFloat();
        o =       fields[5].GetFloat();
        mapid =   fields[6].GetUInt16();
        pool_id = sPoolMgr.IsPartOfAPool<GameObject>(lowguid);
        if (!pool_id || sPoolMgr.IsSpawnedObject<GameObject>(lowguid))
            found = true;
    } while( result->NextRow() && (!found) );

    delete result;

    if (!found)
    {
        PSendSysMessage(LANG_GAMEOBJECT_NOT_EXIST,id);
        return false;
    }

    GameObjectInfo const* goI = ObjectMgr::GetGameObjectInfo(id);

    if (!goI)
    {
        PSendSysMessage(LANG_GAMEOBJECT_NOT_EXIST,id);
        return false;
    }

    GameObject* target = m_session->GetPlayer()->GetMap()->GetGameObject(MAKE_NEW_GUID(lowguid,id,HIGHGUID_GAMEOBJECT));

    PSendSysMessage(LANG_GAMEOBJECT_DETAIL, lowguid, goI->name, lowguid, id, x, y, z, mapid, o);

    if(target)
    {
        time_t curRespawnDelay = target->GetRespawnTimeEx()-time(NULL);
        if(curRespawnDelay < 0)
            curRespawnDelay = 0;

        std::string curRespawnDelayStr = secsToTimeString(curRespawnDelay,true);
        std::string defRespawnDelayStr = secsToTimeString(target->GetRespawnDelay(),true);

        PSendSysMessage(LANG_COMMAND_RAWPAWNTIMES, defRespawnDelayStr.c_str(),curRespawnDelayStr.c_str());
    }
    return true;
}

//delete object by selection or guid
bool ChatHandler::HandleGameObjectDeleteCommand(const char* args)
{
    // number or [name] Shift-click form |color|Hgameobject:go_guid|h[name]|h|r
    char* cId = extractKeyFromLink((char*)args,"Hgameobject");
    if(!cId)
        return false;

    uint32 lowguid = atoi(cId);
    if(!lowguid)
        return false;

    GameObject* obj = NULL;

    // by DB guid
    if (GameObjectData const* go_data = sObjectMgr.GetGOData(lowguid))
        obj = GetObjectGlobalyWithGuidOrNearWithDbGuid(lowguid,go_data->id);

    if(!obj)
    {
        PSendSysMessage(LANG_COMMAND_OBJNOTFOUND, lowguid);
        SetSentErrorMessage(true);
        return false;
    }

    uint64 owner_guid = obj->GetOwnerGUID();
    if(owner_guid)
    {
        Unit* owner = ObjectAccessor::GetUnit(*m_session->GetPlayer(),owner_guid);
        if(!owner || !IS_PLAYER_GUID(owner_guid))
        {
            PSendSysMessage(LANG_COMMAND_DELOBJREFERCREATURE, GUID_LOPART(owner_guid), obj->GetGUIDLow());
            SetSentErrorMessage(true);
            return false;
        }

        owner->RemoveGameObject(obj,false);
    }

    obj->SetRespawnTime(0);                                 // not save respawn time
    obj->Delete();
    obj->DeleteFromDB();

    PSendSysMessage(LANG_COMMAND_DELOBJMESSAGE, obj->GetGUIDLow());

    return true;
}

//turn selected object
bool ChatHandler::HandleGameObjectTurnCommand(const char* args)
{
    // number or [name] Shift-click form |color|Hgameobject:go_id|h[name]|h|r
    char* cId = extractKeyFromLink((char*)args,"Hgameobject");
    if(!cId)
        return false;

    uint32 lowguid = atoi(cId);
    if(!lowguid)
        return false;

    GameObject* obj = NULL;

    // by DB guid
    if (GameObjectData const* go_data = sObjectMgr.GetGOData(lowguid))
        obj = GetObjectGlobalyWithGuidOrNearWithDbGuid(lowguid,go_data->id);

    if(!obj)
    {
        PSendSysMessage(LANG_COMMAND_OBJNOTFOUND, lowguid);
        SetSentErrorMessage(true);
        return false;
    }

    char* po = strtok(NULL, " ");
    float o;

    if (po)
    {
        o = (float)atof(po);
    }
    else
    {
        Player *chr = m_session->GetPlayer();
        o = chr->GetOrientation();
    }

    Map* map = obj->GetMap();
    map->Remove(obj,false);

    obj->Relocate(obj->GetPositionX(), obj->GetPositionY(), obj->GetPositionZ(), o);
    obj->UpdateRotationFields();

    map->Add(obj);

    obj->SaveToDB();
    obj->Refresh();

    PSendSysMessage(LANG_COMMAND_TURNOBJMESSAGE, obj->GetGUIDLow(), obj->GetGOInfo()->name, obj->GetGUIDLow(), o);

    return true;
}

//move selected object
bool ChatHandler::HandleGameObjectMoveCommand(const char* args)
{
    // number or [name] Shift-click form |color|Hgameobject:go_guid|h[name]|h|r
    char* cId = extractKeyFromLink((char*)args,"Hgameobject");
    if(!cId)
        return false;

    uint32 lowguid = atoi(cId);
    if(!lowguid)
        return false;

    GameObject* obj = NULL;

    // by DB guid
    if (GameObjectData const* go_data = sObjectMgr.GetGOData(lowguid))
        obj = GetObjectGlobalyWithGuidOrNearWithDbGuid(lowguid,go_data->id);

    if(!obj)
    {
        PSendSysMessage(LANG_COMMAND_OBJNOTFOUND, lowguid);
        SetSentErrorMessage(true);
        return false;
    }

    char* px = strtok(NULL, " ");
    char* py = strtok(NULL, " ");
    char* pz = strtok(NULL, " ");

    if (!px)
    {
        Player *chr = m_session->GetPlayer();

        Map* map = obj->GetMap();
        map->Remove(obj,false);

        obj->Relocate(chr->GetPositionX(), chr->GetPositionY(), chr->GetPositionZ(), obj->GetOrientation());

        map->Add(obj);
    }
    else
    {
        if(!py || !pz)
            return false;

        float x = (float)atof(px);
        float y = (float)atof(py);
        float z = (float)atof(pz);

        if(!MapManager::IsValidMapCoord(obj->GetMapId(),x,y,z))
        {
            PSendSysMessage(LANG_INVALID_TARGET_COORD,x,y,obj->GetMapId());
            SetSentErrorMessage(true);
            return false;
        }

        Map* map = obj->GetMap();
        map->Remove(obj,false);

        obj->Relocate(x, y, z, obj->GetOrientation());

        map->Add(obj);
    }

    obj->SaveToDB();
    obj->Refresh();

    PSendSysMessage(LANG_COMMAND_MOVEOBJMESSAGE, obj->GetGUIDLow(), obj->GetGOInfo()->name, obj->GetGUIDLow());

    return true;
}

//spawn go
bool ChatHandler::HandleGameObjectAddCommand(const char* args)
{
    if (!*args)
        return false;

    // number or [name] Shift-click form |color|Hgameobject_entry:go_id|h[name]|h|r
    char* cId = extractKeyFromLink((char*)args,"Hgameobject_entry");
    if(!cId)
        return false;

    uint32 id = atol(cId);
    if(!id)
        return false;

    char* spawntimeSecs = strtok(NULL, " ");

    const GameObjectInfo *gInfo = ObjectMgr::GetGameObjectInfo(id);

    if (!gInfo)
    {
        PSendSysMessage(LANG_GAMEOBJECT_NOT_EXIST,id);
        SetSentErrorMessage(true);
        return false;
    }

    if (gInfo->displayId && !sGameObjectDisplayInfoStore.LookupEntry(gInfo->displayId))
    {
        // report to DB errors log as in loading case
        sLog.outErrorDb("Gameobject (Entry %u GoType: %u) have invalid displayId (%u), not spawned.",id, gInfo->type, gInfo->displayId);
        PSendSysMessage(LANG_GAMEOBJECT_HAVE_INVALID_DATA,id);
        SetSentErrorMessage(true);
        return false;
    }

    Player *chr = m_session->GetPlayer();
    float x = float(chr->GetPositionX());
    float y = float(chr->GetPositionY());
    float z = float(chr->GetPositionZ());
    float o = float(chr->GetOrientation());
    Map *map = chr->GetMap();

    GameObject* pGameObj = new GameObject;
    uint32 db_lowGUID = sObjectMgr.GenerateLowGuid(HIGHGUID_GAMEOBJECT);

    if(!pGameObj->Create(db_lowGUID, gInfo->id, map, chr->GetPhaseMaskForSpawn(), x, y, z, o, 0.0f, 0.0f, 0.0f, 0.0f, 0, GO_STATE_READY))
    {
        delete pGameObj;
        return false;
    }

    if( spawntimeSecs )
    {
        uint32 value = atoi((char*)spawntimeSecs);
        pGameObj->SetRespawnTime(value);
        //sLog.outDebug("*** spawntimeSecs: %d", value);
    }

    // fill the gameobject data and save to the db
    pGameObj->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()),chr->GetPhaseMaskForSpawn());

    // this will generate a new guid if the object is in an instance
    if(!pGameObj->LoadFromDB(db_lowGUID, map))
    {
        delete pGameObj;
        return false;
    }

    sLog.outDebug(GetMangosString(LANG_GAMEOBJECT_CURRENT), gInfo->name, db_lowGUID, x, y, z, o);

    map->Add(pGameObj);

    // TODO: is it really necessary to add both the real and DB table guid here ?
    sObjectMgr.AddGameobjectToGrid(db_lowGUID, sObjectMgr.GetGOData(db_lowGUID));

    PSendSysMessage(LANG_GAMEOBJECT_ADD,id,gInfo->name,db_lowGUID,x,y,z);
    return true;
}

//set pahsemask for selected object
bool ChatHandler::HandleGameObjectPhaseCommand(const char* args)
{
    // number or [name] Shift-click form |color|Hgameobject:go_id|h[name]|h|r
    char* cId = extractKeyFromLink((char*)args,"Hgameobject");
    if(!cId)
        return false;

    uint32 lowguid = atoi(cId);
    if(!lowguid)
        return false;

    GameObject* obj = NULL;

    // by DB guid
    if (GameObjectData const* go_data = sObjectMgr.GetGOData(lowguid))
        obj = GetObjectGlobalyWithGuidOrNearWithDbGuid(lowguid,go_data->id);

    if(!obj)
    {
        PSendSysMessage(LANG_COMMAND_OBJNOTFOUND, lowguid);
        SetSentErrorMessage(true);
        return false;
    }

    char* phaseStr = strtok (NULL, " ");
    uint32 phasemask = phaseStr? atoi(phaseStr) : 0;
    if ( phasemask == 0 )
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    obj->SetPhaseMask(phasemask,true);
    obj->SaveToDB();
    return true;
}

bool ChatHandler::HandleGameObjectNearCommand(const char* args)
{
    float distance = (!*args) ? 10.0f : (float)atof(args);
    uint32 count = 0;

    Player* pl = m_session->GetPlayer();
    QueryResult *result = WorldDatabase.PQuery("SELECT guid, id, position_x, position_y, position_z, map, "
        "(POW(position_x - '%f', 2) + POW(position_y - '%f', 2) + POW(position_z - '%f', 2)) AS order_ "
        "FROM gameobject WHERE map='%u' AND (POW(position_x - '%f', 2) + POW(position_y - '%f', 2) + POW(position_z - '%f', 2)) <= '%f' ORDER BY order_",
        pl->GetPositionX(), pl->GetPositionY(), pl->GetPositionZ(),
        pl->GetMapId(),pl->GetPositionX(), pl->GetPositionY(), pl->GetPositionZ(),distance*distance);

    if (result)
    {
        do
        {
            Field *fields = result->Fetch();
            uint32 guid = fields[0].GetUInt32();
            uint32 entry = fields[1].GetUInt32();
            float x = fields[2].GetFloat();
            float y = fields[3].GetFloat();
            float z = fields[4].GetFloat();
            int mapid = fields[5].GetUInt16();

            GameObjectInfo const * gInfo = ObjectMgr::GetGameObjectInfo(entry);

            if(!gInfo)
                continue;

            PSendSysMessage(LANG_GO_LIST_CHAT, guid, guid, gInfo->name, x, y, z, mapid);

            ++count;
        } while (result->NextRow());

        delete result;
    }

    PSendSysMessage(LANG_COMMAND_NEAROBJMESSAGE,distance,count);
    return true;
}

bool ChatHandler::HandleGUIDCommand(const char* /*args*/)
{
    uint64 guid = m_session->GetPlayer()->GetSelection();

    if (guid == 0)
    {
        SendSysMessage(LANG_NO_SELECTION);
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage(LANG_OBJECT_GUID, GUID_LOPART(guid), GUID_HIPART(guid));
    return true;
}

bool ChatHandler::HandleLookupFactionCommand(const char* args)
{
    if (!*args)
        return false;

    // Can be NULL at console call
    Player *target = getSelectedPlayer ();

    std::string namepart = args;
    std::wstring wnamepart;

    if (!Utf8toWStr (namepart,wnamepart))
        return false;

    // converting string that we try to find to lower case
    wstrToLower (wnamepart);

    uint32 counter = 0;                                     // Counter for figure out that we found smth.

    for (uint32 id = 0; id < sFactionStore.GetNumRows(); ++id)
    {
        FactionEntry const *factionEntry = sFactionStore.LookupEntry (id);
        if (factionEntry)
        {
            FactionState const* repState = target ? target->GetReputationMgr().GetState(factionEntry) : NULL;

            int loc = GetSessionDbcLocale();
            std::string name = factionEntry->name[loc];
            if(name.empty())
                continue;

            if (!Utf8FitTo(name, wnamepart))
            {
                loc = 0;
                for(; loc < MAX_LOCALE; ++loc)
                {
                    if(loc==GetSessionDbcLocale())
                        continue;

                    name = factionEntry->name[loc];
                    if(name.empty())
                        continue;

                    if (Utf8FitTo(name, wnamepart))
                        break;
                }
            }

            if(loc < MAX_LOCALE)
            {
                // send faction in "id - [faction] rank reputation [visible] [at war] [own team] [unknown] [invisible] [inactive]" format
                // or              "id - [faction] [no reputation]" format
                std::ostringstream ss;
                if (m_session)
                    ss << id << " - |cffffffff|Hfaction:" << id << "|h[" << name << " " << localeNames[loc] << "]|h|r";
                else
                    ss << id << " - " << name << " " << localeNames[loc];

                if (repState)                               // and then target!=NULL also
                {
                    ReputationRank rank = target->GetReputationMgr().GetRank(factionEntry);
                    std::string rankName = GetMangosString(ReputationRankStrIndex[rank]);

                    ss << " " << rankName << "|h|r (" << target->GetReputationMgr().GetReputation(factionEntry) << ")";

                    if(repState->Flags & FACTION_FLAG_VISIBLE)
                        ss << GetMangosString(LANG_FACTION_VISIBLE);
                    if(repState->Flags & FACTION_FLAG_AT_WAR)
                        ss << GetMangosString(LANG_FACTION_ATWAR);
                    if(repState->Flags & FACTION_FLAG_PEACE_FORCED)
                        ss << GetMangosString(LANG_FACTION_PEACE_FORCED);
                    if(repState->Flags & FACTION_FLAG_HIDDEN)
                        ss << GetMangosString(LANG_FACTION_HIDDEN);
                    if(repState->Flags & FACTION_FLAG_INVISIBLE_FORCED)
                        ss << GetMangosString(LANG_FACTION_INVISIBLE_FORCED);
                    if(repState->Flags & FACTION_FLAG_INACTIVE)
                        ss << GetMangosString(LANG_FACTION_INACTIVE);
                }
                else
                    ss << GetMangosString(LANG_FACTION_NOREPUTATION);

                SendSysMessage(ss.str().c_str());
                counter++;
            }
        }
    }

    if (counter == 0)                                       // if counter == 0 then we found nth
        SendSysMessage(LANG_COMMAND_FACTION_NOTFOUND);
    return true;
}

bool ChatHandler::HandleModifyRepCommand(const char * args)
{
    if (!*args) return false;

    Player* target = NULL;
    target = getSelectedPlayer();

    if(!target)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(target, 0))
        return false;

    char* factionTxt = extractKeyFromLink((char*)args,"Hfaction");
    if(!factionTxt)
        return false;

    uint32 factionId = atoi(factionTxt);

    int32 amount = 0;
    char *rankTxt = strtok(NULL, " ");
    if (!factionTxt || !rankTxt)
        return false;

    amount = atoi(rankTxt);
    if ((amount == 0) && (rankTxt[0] != '-') && !isdigit(rankTxt[0]))
    {
        std::string rankStr = rankTxt;
        std::wstring wrankStr;
        if(!Utf8toWStr(rankStr,wrankStr))
            return false;
        wstrToLower( wrankStr );

        int r = 0;
        amount = -42000;
        for (; r < MAX_REPUTATION_RANK; ++r)
        {
            std::string rank = GetMangosString(ReputationRankStrIndex[r]);
            if(rank.empty())
                continue;

            std::wstring wrank;
            if(!Utf8toWStr(rank,wrank))
                continue;

            wstrToLower(wrank);

            if(wrank.substr(0,wrankStr.size())==wrankStr)
            {
                char *deltaTxt = strtok(NULL, " ");
                if (deltaTxt)
                {
                    int32 delta = atoi(deltaTxt);
                    if ((delta < 0) || (delta > ReputationMgr::PointsInRank[r] -1))
                    {
                        PSendSysMessage(LANG_COMMAND_FACTION_DELTA, (ReputationMgr::PointsInRank[r]-1));
                        SetSentErrorMessage(true);
                        return false;
                    }
                    amount += delta;
                }
                break;
            }
            amount += ReputationMgr::PointsInRank[r];
        }
        if (r >= MAX_REPUTATION_RANK)
        {
            PSendSysMessage(LANG_COMMAND_FACTION_INVPARAM, rankTxt);
            SetSentErrorMessage(true);
            return false;
        }
    }

    FactionEntry const *factionEntry = sFactionStore.LookupEntry(factionId);

    if (!factionEntry)
    {
        PSendSysMessage(LANG_COMMAND_FACTION_UNKNOWN, factionId);
        SetSentErrorMessage(true);
        return false;
    }

    if (factionEntry->reputationListID < 0)
    {
        PSendSysMessage(LANG_COMMAND_FACTION_NOREP_ERROR, factionEntry->name[GetSessionDbcLocale()], factionId);
        SetSentErrorMessage(true);
        return false;
    }

    target->GetReputationMgr().SetReputation(factionEntry,amount);
    PSendSysMessage(LANG_COMMAND_MODIFY_REP, factionEntry->name[GetSessionDbcLocale()], factionId,
        GetNameLink(target).c_str(), target->GetReputationMgr().GetReputation(factionEntry));
    return true;
}

//-----------------------Npc Commands-----------------------
//add spawn of creature
bool ChatHandler::HandleNpcAddCommand(const char* args)
{
    if(!*args)
        return false;
    char* charID = extractKeyFromLink((char*)args,"Hcreature_entry");
    if(!charID)
        return false;

    char* team = strtok(NULL, " ");
    int32 teamval = 0;
    if (team) { teamval = atoi(team); }
    if (teamval < 0) { teamval = 0; }

    uint32 id  = atoi(charID);

    Player *chr = m_session->GetPlayer();
    float x = chr->GetPositionX();
    float y = chr->GetPositionY();
    float z = chr->GetPositionZ();
    float o = chr->GetOrientation();
    Map *map = chr->GetMap();

    Creature* pCreature = new Creature;
    if (!pCreature->Create(sObjectMgr.GenerateLowGuid(HIGHGUID_UNIT), map, chr->GetPhaseMaskForSpawn(), id, (uint32)teamval))
    {
        delete pCreature;
        return false;
    }

    pCreature->Relocate(x,y,z,o);

    if(!pCreature->IsPositionValid())
    {
        sLog.outError("Creature (guidlow %d, entry %d) not created. Suggested coordinates isn't valid (X: %f Y: %f)",pCreature->GetGUIDLow(),pCreature->GetEntry(),pCreature->GetPositionX(),pCreature->GetPositionY());
        delete pCreature;
        return false;
    }

    pCreature->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), chr->GetPhaseMaskForSpawn());

    uint32 db_guid = pCreature->GetDBTableGUIDLow();

    // To call _LoadGoods(); _LoadQuests(); CreateTrainerSpells();
    pCreature->LoadFromDB(db_guid, map);

    map->Add(pCreature);
    sObjectMgr.AddCreatureToGrid(db_guid, sObjectMgr.GetCreatureData(db_guid));
    return true;
}

//add item in vendorlist
bool ChatHandler::HandleNpcAddVendorItemCommand(const char* args)
{
    if (!*args)
        return false;

    char* pitem  = extractKeyFromLink((char*)args,"Hitem");
    if (!pitem)
    {
        SendSysMessage(LANG_COMMAND_NEEDITEMSEND);
        SetSentErrorMessage(true);
        return false;
    }

    uint32 itemId = atol(pitem);

    char* fmaxcount = strtok(NULL, " ");                    //add maxcount, default: 0
    uint32 maxcount = 0;
    if (fmaxcount)
        maxcount = atol(fmaxcount);

    char* fincrtime = strtok(NULL, " ");                    //add incrtime, default: 0
    uint32 incrtime = 0;
    if (fincrtime)
        incrtime = atol(fincrtime);

    char* fextendedcost = strtok(NULL, " ");                //add ExtendedCost, default: 0
    uint32 extendedcost = fextendedcost ? atol(fextendedcost) : 0;

    Creature* vendor = getSelectedCreature();

    uint32 vendor_entry = vendor ? vendor->GetEntry() : 0;

    if(!sObjectMgr.IsVendorItemValid(vendor_entry,itemId,maxcount,incrtime,extendedcost,m_session->GetPlayer()))
    {
        SetSentErrorMessage(true);
        return false;
    }

    sObjectMgr.AddVendorItem(vendor_entry,itemId,maxcount,incrtime,extendedcost);

    ItemPrototype const* pProto = ObjectMgr::GetItemPrototype(itemId);

    PSendSysMessage(LANG_ITEM_ADDED_TO_LIST,itemId,pProto->Name1,maxcount,incrtime,extendedcost);
    return true;
}

//del item from vendor list
bool ChatHandler::HandleNpcDelVendorItemCommand(const char* args)
{
    if (!*args)
        return false;

    Creature* vendor = getSelectedCreature();
    if (!vendor || !vendor->isVendor())
    {
        SendSysMessage(LANG_COMMAND_VENDORSELECTION);
        SetSentErrorMessage(true);
        return false;
    }

    char* pitem  = extractKeyFromLink((char*)args,"Hitem");
    if (!pitem)
    {
        SendSysMessage(LANG_COMMAND_NEEDITEMSEND);
        SetSentErrorMessage(true);
        return false;
    }
    uint32 itemId = atol(pitem);

    if(!sObjectMgr.RemoveVendorItem(vendor->GetEntry(),itemId))
    {
        PSendSysMessage(LANG_ITEM_NOT_IN_LIST,itemId);
        SetSentErrorMessage(true);
        return false;
    }

    ItemPrototype const* pProto = ObjectMgr::GetItemPrototype(itemId);

    PSendSysMessage(LANG_ITEM_DELETED_FROM_LIST,itemId,pProto->Name1);
    return true;
}

//add move for creature
bool ChatHandler::HandleNpcAddMoveCommand(const char* args)
{
    if(!*args)
        return false;

    char* guid_str = strtok((char*)args, " ");
    char* wait_str = strtok((char*)NULL, " ");

    uint32 lowguid = atoi((char*)guid_str);

    Creature* pCreature = NULL;

    /* FIXME: impossible without entry
    if(lowguid)
        pCreature = ObjectAccessor::GetCreature(*m_session->GetPlayer(),MAKE_GUID(lowguid,HIGHGUID_UNIT));
    */

    // attempt check creature existence by DB data
    if(!pCreature)
    {
        CreatureData const* data = sObjectMgr.GetCreatureData(lowguid);
        if(!data)
        {
            PSendSysMessage(LANG_COMMAND_CREATGUIDNOTFOUND, lowguid);
            SetSentErrorMessage(true);
            return false;
        }
    }
    else
    {
        // obtain real GUID for DB operations
        lowguid = pCreature->GetDBTableGUIDLow();
    }

    int wait = wait_str ? atoi(wait_str) : 0;

    if(wait < 0)
        wait = 0;

    Player* player = m_session->GetPlayer();

    sWaypointMgr.AddLastNode(lowguid, player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation(), wait, 0);

    // update movement type
    WorldDatabase.PExecuteLog("UPDATE creature SET MovementType = '%u' WHERE guid = '%u'", WAYPOINT_MOTION_TYPE,lowguid);
    if(pCreature)
    {
        pCreature->SetDefaultMovementType(WAYPOINT_MOTION_TYPE);
        pCreature->GetMotionMaster()->Initialize();
        if(pCreature->isAlive())                            // dead creature will reset movement generator at respawn
        {
            pCreature->setDeathState(JUST_DIED);
            pCreature->Respawn();
        }
        pCreature->SaveToDB();
    }

    SendSysMessage(LANG_WAYPOINT_ADDED);

    return true;
}

//change level of creature or pet
bool ChatHandler::HandleNpcChangeLevelCommand(const char* args)
{
    if (!*args)
        return false;

    uint8 lvl = (uint8) atoi((char*)args);
    if ( lvl < 1 || lvl > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL) + 3)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Creature* pCreature = getSelectedCreature();
    if(!pCreature)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    if(pCreature->isPet())
    {
        if(((Pet*)pCreature)->getPetType()==HUNTER_PET)
        {
            pCreature->SetUInt32Value(UNIT_FIELD_PETNEXTLEVELEXP, sObjectMgr.GetXPForPetLevel(lvl));
            pCreature->SetUInt32Value(UNIT_FIELD_PETEXPERIENCE, 0);
        }
        ((Pet*)pCreature)->GivePetLevel(lvl);
    }
    else
    {
        pCreature->SetMaxHealth( 100 + 30*lvl);
        pCreature->SetHealth( 100 + 30*lvl);
        pCreature->SetLevel( lvl);
        pCreature->SaveToDB();
    }

    return true;
}

//set npcflag of creature
bool ChatHandler::HandleNpcFlagCommand(const char* args)
{
    if (!*args)
        return false;

    uint32 npcFlags = (uint32) atoi((char*)args);

    Creature* pCreature = getSelectedCreature();

    if(!pCreature)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    pCreature->SetUInt32Value(UNIT_NPC_FLAGS, npcFlags);

    WorldDatabase.PExecuteLog("UPDATE creature_template SET npcflag = '%u' WHERE entry = '%u'", npcFlags, pCreature->GetEntry());

    SendSysMessage(LANG_VALUE_SAVED_REJOIN);

    return true;
}

bool ChatHandler::HandleNpcDeleteCommand(const char* args)
{
    Creature* unit = NULL;

    if(*args)
    {
        // number or [name] Shift-click form |color|Hcreature:creature_guid|h[name]|h|r
        char* cId = extractKeyFromLink((char*)args,"Hcreature");
        if(!cId)
            return false;

        uint32 lowguid = atoi(cId);
        if(!lowguid)
            return false;

        if (CreatureData const* cr_data = sObjectMgr.GetCreatureData(lowguid))
            unit = m_session->GetPlayer()->GetMap()->GetCreature(MAKE_NEW_GUID(lowguid, cr_data->id, HIGHGUID_UNIT));
    }
    else
        unit = getSelectedCreature();

    if(!unit || unit->isPet() || unit->isTotem() || unit->isVehicle())
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    // Delete the creature
    unit->CombatStop();
    unit->DeleteFromDB();
    unit->AddObjectToRemoveList();

    SendSysMessage(LANG_COMMAND_DELCREATMESSAGE);

    return true;
}

//move selected creature
bool ChatHandler::HandleNpcMoveCommand(const char* args)
{
    uint32 lowguid = 0;

    Creature* pCreature = getSelectedCreature();

    if(!pCreature)
    {
        // number or [name] Shift-click form |color|Hcreature:creature_guid|h[name]|h|r
        char* cId = extractKeyFromLink((char*)args,"Hcreature");
        if(!cId)
            return false;

        lowguid = atoi(cId);

        /* FIXME: impossibel without entry
        if(lowguid)
            pCreature = ObjectAccessor::GetCreature(*m_session->GetPlayer(),MAKE_GUID(lowguid,HIGHGUID_UNIT));
        */

        // Attempting creature load from DB data
        if(!pCreature)
        {
            CreatureData const* data = sObjectMgr.GetCreatureData(lowguid);
            if(!data)
            {
                PSendSysMessage(LANG_COMMAND_CREATGUIDNOTFOUND, lowguid);
                SetSentErrorMessage(true);
                return false;
            }

            uint32 map_id = data->mapid;

            if(m_session->GetPlayer()->GetMapId()!=map_id)
            {
                PSendSysMessage(LANG_COMMAND_CREATUREATSAMEMAP, lowguid);
                SetSentErrorMessage(true);
                return false;
            }
        }
        else
        {
            lowguid = pCreature->GetDBTableGUIDLow();
        }
    }
    else
    {
        lowguid = pCreature->GetDBTableGUIDLow();
    }

    float x = m_session->GetPlayer()->GetPositionX();
    float y = m_session->GetPlayer()->GetPositionY();
    float z = m_session->GetPlayer()->GetPositionZ();
    float o = m_session->GetPlayer()->GetOrientation();

    if (pCreature)
    {
        if(CreatureData const* data = sObjectMgr.GetCreatureData(pCreature->GetDBTableGUIDLow()))
        {
            const_cast<CreatureData*>(data)->posX = x;
            const_cast<CreatureData*>(data)->posY = y;
            const_cast<CreatureData*>(data)->posZ = z;
            const_cast<CreatureData*>(data)->orientation = o;
        }
        pCreature->GetMap()->CreatureRelocation(pCreature,x, y, z,o);
        pCreature->GetMotionMaster()->Initialize();
        if(pCreature->isAlive())                            // dead creature will reset movement generator at respawn
        {
            pCreature->setDeathState(JUST_DIED);
            pCreature->Respawn();
        }
    }

    WorldDatabase.PExecuteLog("UPDATE creature SET position_x = '%f', position_y = '%f', position_z = '%f', orientation = '%f' WHERE guid = '%u'", x, y, z, o, lowguid);
    PSendSysMessage(LANG_COMMAND_CREATUREMOVED);
    return true;
}

/**HandleNpcSetMoveTypeCommand
 * Set the movement type for an NPC.<br/>
 * <br/>
 * Valid movement types are:
 * <ul>
 * <li> stay - NPC wont move </li>
 * <li> random - NPC will move randomly according to the spawndist </li>
 * <li> way - NPC will move with given waypoints set </li>
 * </ul>
 * additional parameter: NODEL - so no waypoints are deleted, if you
 *                       change the movement type
 */
bool ChatHandler::HandleNpcSetMoveTypeCommand(const char* args)
{
    if(!*args)
        return false;

    // 3 arguments:
    // GUID (optional - you can also select the creature)
    // stay|random|way (determines the kind of movement)
    // NODEL (optional - tells the system NOT to delete any waypoints)
    //        this is very handy if you want to do waypoints, that are
    //        later switched on/off according to special events (like escort
    //        quests, etc)
    char* guid_str = strtok((char*)args, " ");
    char* type_str = strtok((char*)NULL, " ");
    char* dontdel_str = strtok((char*)NULL, " ");

    bool doNotDelete = false;

    if(!guid_str)
        return false;

    uint32 lowguid = 0;
    Creature* pCreature = NULL;

    if( dontdel_str )
    {
        //sLog.outError("DEBUG: All 3 params are set");

        // All 3 params are set
        // GUID
        // type
        // doNotDEL
        if( stricmp( dontdel_str, "NODEL" ) == 0 )
        {
            //sLog.outError("DEBUG: doNotDelete = true;");
            doNotDelete = true;
        }
    }
    else
    {
        // Only 2 params - but maybe NODEL is set
        if( type_str )
        {
            sLog.outError("DEBUG: Only 2 params ");
            if( stricmp( type_str, "NODEL" ) == 0 )
            {
                //sLog.outError("DEBUG: type_str, NODEL ");
                doNotDelete = true;
                type_str = NULL;
            }
        }
    }

    if(!type_str)                                           // case .setmovetype $move_type (with selected creature)
    {
        type_str = guid_str;
        pCreature = getSelectedCreature();
        if(!pCreature || pCreature->isPet())
            return false;
        lowguid = pCreature->GetDBTableGUIDLow();
    }
    else                                                    // case .setmovetype #creature_guid $move_type (with selected creature)
    {
        lowguid = atoi((char*)guid_str);

        /* impossible without entry
        if(lowguid)
            pCreature = ObjectAccessor::GetCreature(*m_session->GetPlayer(),MAKE_GUID(lowguid,HIGHGUID_UNIT));
        */

        // attempt check creature existence by DB data
        if(!pCreature)
        {
            CreatureData const* data = sObjectMgr.GetCreatureData(lowguid);
            if(!data)
            {
                PSendSysMessage(LANG_COMMAND_CREATGUIDNOTFOUND, lowguid);
                SetSentErrorMessage(true);
                return false;
            }
        }
        else
        {
            lowguid = pCreature->GetDBTableGUIDLow();
        }
    }

    // now lowguid is low guid really existed creature
    // and pCreature point (maybe) to this creature or NULL

    MovementGeneratorType move_type;

    std::string type = type_str;

    if(type == "stay")
        move_type = IDLE_MOTION_TYPE;
    else if(type == "random")
        move_type = RANDOM_MOTION_TYPE;
    else if(type == "way")
        move_type = WAYPOINT_MOTION_TYPE;
    else
        return false;

    // update movement type
    if(doNotDelete == false)
        sWaypointMgr.DeletePath(lowguid);

    if(pCreature)
    {
        pCreature->SetDefaultMovementType(move_type);
        pCreature->GetMotionMaster()->Initialize();
        if(pCreature->isAlive())                            // dead creature will reset movement generator at respawn
        {
            pCreature->setDeathState(JUST_DIED);
            pCreature->Respawn();
        }
        pCreature->SaveToDB();
    }
    if( doNotDelete == false )
    {
        PSendSysMessage(LANG_MOVE_TYPE_SET,type_str);
    }
    else
    {
        PSendSysMessage(LANG_MOVE_TYPE_SET_NODEL,type_str);
    }

    return true;
}

//set model of creature
bool ChatHandler::HandleNpcSetModelCommand(const char* args)
{
    if (!*args)
        return false;

    uint32 displayId = (uint32) atoi((char*)args);

    Creature *pCreature = getSelectedCreature();

    if(!pCreature || pCreature->isPet())
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    pCreature->SetDisplayId(displayId);
    pCreature->SetNativeDisplayId(displayId);

    pCreature->SaveToDB();

    return true;
}
//set faction of creature
bool ChatHandler::HandleNpcFactionIdCommand(const char* args)
{
    if (!*args)
        return false;

    uint32 factionId = (uint32) atoi((char*)args);

    if (!sFactionTemplateStore.LookupEntry(factionId))
    {
        PSendSysMessage(LANG_WRONG_FACTION, factionId);
        SetSentErrorMessage(true);
        return false;
    }

    Creature* pCreature = getSelectedCreature();

    if(!pCreature)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    pCreature->setFaction(factionId);

    // faction is set in creature_template - not inside creature

    // update in memory
    if(CreatureInfo const *cinfo = pCreature->GetCreatureInfo())
    {
        const_cast<CreatureInfo*>(cinfo)->faction_A = factionId;
        const_cast<CreatureInfo*>(cinfo)->faction_H = factionId;
    }

    // and DB
    WorldDatabase.PExecuteLog("UPDATE creature_template SET faction_A = '%u', faction_H = '%u' WHERE entry = '%u'", factionId, factionId, pCreature->GetEntry());

    return true;
}
//set spawn dist of creature
bool ChatHandler::HandleNpcSpawnDistCommand(const char* args)
{
    if(!*args)
        return false;

    float option = (float)atof((char*)args);
    if (option < 0.0f)
    {
        SendSysMessage(LANG_BAD_VALUE);
        return false;
    }

    MovementGeneratorType mtype = IDLE_MOTION_TYPE;
    if (option >0.0f)
        mtype = RANDOM_MOTION_TYPE;

    Creature *pCreature = getSelectedCreature();
    uint32 u_guidlow = 0;

    if (pCreature)
        u_guidlow = pCreature->GetDBTableGUIDLow();
    else
        return false;

    pCreature->SetRespawnRadius((float)option);
    pCreature->SetDefaultMovementType(mtype);
    pCreature->GetMotionMaster()->Initialize();
    if(pCreature->isAlive())                                // dead creature will reset movement generator at respawn
    {
        pCreature->setDeathState(JUST_DIED);
        pCreature->Respawn();
    }

    WorldDatabase.PExecuteLog("UPDATE creature SET spawndist=%f, MovementType=%i WHERE guid=%u",option,mtype,u_guidlow);
    PSendSysMessage(LANG_COMMAND_SPAWNDIST,option);
    return true;
}
//spawn time handling
bool ChatHandler::HandleNpcSpawnTimeCommand(const char* args)
{
    if(!*args)
        return false;

    char* stime = strtok((char*)args, " ");

    if (!stime)
        return false;

    int i_stime = atoi((char*)stime);

    if (i_stime < 0)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Creature *pCreature = getSelectedCreature();
    uint32 u_guidlow = 0;

    if (pCreature)
        u_guidlow = pCreature->GetDBTableGUIDLow();
    else
        return false;

    WorldDatabase.PExecuteLog("UPDATE creature SET spawntimesecs=%i WHERE guid=%u",i_stime,u_guidlow);
    pCreature->SetRespawnDelay((uint32)i_stime);
    PSendSysMessage(LANG_COMMAND_SPAWNTIME,i_stime);

    return true;
}
//npc follow handling
bool ChatHandler::HandleNpcFollowCommand(const char* /*args*/)
{
    Player *player = m_session->GetPlayer();
    Creature *creature = getSelectedCreature();

    if(!creature)
    {
        PSendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    // Follow player - Using pet's default dist and angle
    creature->GetMotionMaster()->MoveFollow(player, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);

    PSendSysMessage(LANG_CREATURE_FOLLOW_YOU_NOW, creature->GetName());
    return true;
}
//npc unfollow handling
bool ChatHandler::HandleNpcUnFollowCommand(const char* /*args*/)
{
    Player *player = m_session->GetPlayer();
    Creature *creature = getSelectedCreature();

    if(!creature)
    {
        PSendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    if (creature->GetMotionMaster()->empty() ||
        creature->GetMotionMaster()->GetCurrentMovementGeneratorType ()!=FOLLOW_MOTION_TYPE)
    {
        PSendSysMessage(LANG_CREATURE_NOT_FOLLOW_YOU);
        SetSentErrorMessage(true);
        return false;
    }

    FollowMovementGenerator<Creature> const* mgen
        = static_cast<FollowMovementGenerator<Creature> const*>((creature->GetMotionMaster()->top()));

    if(mgen->GetTarget()!=player)
    {
        PSendSysMessage(LANG_CREATURE_NOT_FOLLOW_YOU);
        SetSentErrorMessage(true);
        return false;
    }

    // reset movement
    creature->GetMotionMaster()->MovementExpired(true);

    PSendSysMessage(LANG_CREATURE_NOT_FOLLOW_YOU_NOW, creature->GetName());
    return true;
}
//npc tame handling
bool ChatHandler::HandleNpcTameCommand(const char* /*args*/)
{
    Creature *creatureTarget = getSelectedCreature ();
    if (!creatureTarget || creatureTarget->isPet ())
    {
        PSendSysMessage (LANG_SELECT_CREATURE);
        SetSentErrorMessage (true);
        return false;
    }

    Player *player = m_session->GetPlayer ();

    if(player->GetPetGUID ())
    {
        SendSysMessage (LANG_YOU_ALREADY_HAVE_PET);
        SetSentErrorMessage (true);
        return false;
    }

    CreatureInfo const* cInfo = creatureTarget->GetCreatureInfo();

    if (!cInfo->isTameable (player->CanTameExoticPets()))
    {
        PSendSysMessage (LANG_CREATURE_NON_TAMEABLE,cInfo->Entry);
        SetSentErrorMessage (true);
        return false;
    }

    // Everything looks OK, create new pet
    Pet* pet = player->CreateTamedPetFrom (creatureTarget);
    if (!pet)
    {
        PSendSysMessage (LANG_CREATURE_NON_TAMEABLE,cInfo->Entry);
        SetSentErrorMessage (true);
        return false;
    }

    // place pet before player
    float x,y,z;
    player->GetClosePoint (x,y,z,creatureTarget->GetObjectSize (),CONTACT_DISTANCE);
    pet->Relocate (x,y,z,M_PI_F-player->GetOrientation ());

    // set pet to defensive mode by default (some classes can't control controlled pets in fact).
    pet->GetCharmInfo()->SetReactState(REACT_DEFENSIVE);

    // calculate proper level
    uint32 level = (creatureTarget->getLevel() < (player->getLevel() - 5)) ? (player->getLevel() - 5) : creatureTarget->getLevel();

    // prepare visual effect for levelup
    pet->SetUInt32Value(UNIT_FIELD_LEVEL, level - 1);

    // add to world
    pet->GetMap()->Add((Creature*)pet);

    // visual effect for levelup
    pet->SetUInt32Value(UNIT_FIELD_LEVEL, level);

    // caster have pet now
    player->SetPet(pet);

    pet->SavePetToDB(PET_SAVE_AS_CURRENT);
    player->PetSpellInitialize();

    return true;
}
//npc phasemask handling
//change phasemask of creature or pet
bool ChatHandler::HandleNpcSetPhaseCommand(const char* args)
{
    if (!*args)
        return false;

    uint32 phasemask = (uint32) atoi((char*)args);
    if ( phasemask == 0 )
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Creature* pCreature = getSelectedCreature();
    if(!pCreature)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    pCreature->SetPhaseMask(phasemask,true);

    if(!pCreature->isPet())
        pCreature->SaveToDB();

    return true;
}
//npc deathstate handling
bool ChatHandler::HandleNpcSetDeathStateCommand(const char* args)
{
    if (!*args)
        return false;

    Creature* pCreature = getSelectedCreature();
    if(!pCreature || pCreature->isPet())
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    if (strncmp(args, "on", 3) == 0)
        pCreature->SetDeadByDefault(true);
    else if (strncmp(args, "off", 4) == 0)
        pCreature->SetDeadByDefault(false);
    else
    {
        SendSysMessage(LANG_USE_BOL);
        SetSentErrorMessage(true);
        return false;
    }

    pCreature->SaveToDB();
    pCreature->Respawn();

    return true;
}

//TODO: NpcCommands that need to be fixed :

bool ChatHandler::HandleNpcNameCommand(const char* /*args*/)
{
    /* Temp. disabled
    if(!*args)
        return false;

    if(strlen((char*)args)>75)
    {
        PSendSysMessage(LANG_TOO_LONG_NAME, strlen((char*)args)-75);
        return true;
    }

    for (uint8 i = 0; i < strlen(args); ++i)
    {
        if(!isalpha(args[i]) && args[i]!=' ')
        {
            SendSysMessage(LANG_CHARS_ONLY);
            return false;
        }
    }

    uint64 guid;
    guid = m_session->GetPlayer()->GetSelection();
    if (guid == 0)
    {
        SendSysMessage(LANG_NO_SELECTION);
        return true;
    }

    Creature* pCreature = ObjectAccessor::GetCreature(*m_session->GetPlayer(), guid);

    if(!pCreature)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        return true;
    }

    pCreature->SetName(args);
    uint32 idname = sObjectMgr.AddCreatureTemplate(pCreature->GetName());
    pCreature->SetUInt32Value(OBJECT_FIELD_ENTRY, idname);

    pCreature->SaveToDB();
    */

    return true;
}

bool ChatHandler::HandleNpcSubNameCommand(const char* /*args*/)
{
    /* Temp. disabled

    if(!*args)
        args = "";

    if(strlen((char*)args)>75)
    {

        PSendSysMessage(LANG_TOO_LONG_SUBNAME, strlen((char*)args)-75);
        return true;
    }

    for (uint8 i = 0; i < strlen(args); i++)
    {
        if(!isalpha(args[i]) && args[i]!=' ')
        {
            SendSysMessage(LANG_CHARS_ONLY);
            return false;
        }
    }
    uint64 guid;
    guid = m_session->GetPlayer()->GetSelection();
    if (guid == 0)
    {
        SendSysMessage(LANG_NO_SELECTION);
        return true;
    }

    Creature* pCreature = ObjectAccessor::GetCreature(*m_session->GetPlayer(), guid);

    if(!pCreature)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        return true;
    }

    uint32 idname = sObjectMgr.AddCreatureSubName(pCreature->GetName(),args,pCreature->GetUInt32Value(UNIT_FIELD_DISPLAYID));
    pCreature->SetUInt32Value(OBJECT_FIELD_ENTRY, idname);

    pCreature->SaveToDB();
    */
    return true;
}

//move item to other slot
bool ChatHandler::HandleItemMoveCommand(const char* args)
{
    if(!*args)
        return false;
    uint8 srcslot, dstslot;

    char* pParam1 = strtok((char*)args, " ");
    if (!pParam1)
        return false;

    char* pParam2 = strtok(NULL, " ");
    if (!pParam2)
        return false;

    srcslot = (uint8)atoi(pParam1);
    dstslot = (uint8)atoi(pParam2);

    if(srcslot==dstslot)
        return true;

    if(!m_session->GetPlayer()->IsValidPos(INVENTORY_SLOT_BAG_0, srcslot, true))
        return false;

    // can be autostore pos
    if(!m_session->GetPlayer()->IsValidPos(INVENTORY_SLOT_BAG_0,dstslot, false))
        return false;

    uint16 src = ((INVENTORY_SLOT_BAG_0 << 8) | srcslot);
    uint16 dst = ((INVENTORY_SLOT_BAG_0 << 8) | dstslot);

    m_session->GetPlayer()->SwapItem( src, dst );

    return true;
}

//demorph player or unit
bool ChatHandler::HandleDeMorphCommand(const char* /*args*/)
{
    Unit *target = getSelectedUnit();
    if(!target)
        target = m_session->GetPlayer();


    // check online security
    else if (target->GetTypeId() == TYPEID_PLAYER && HasLowerSecurity((Player*)target, 0))
        return false;

    target->DeMorph();

    return true;
}

//morph creature or player
bool ChatHandler::HandleModifyMorphCommand(const char* args)
{
    if (!*args)
        return false;

    uint16 display_id = (uint16)atoi((char*)args);

    Unit *target = getSelectedUnit();
    if(!target)
        target = m_session->GetPlayer();

    // check online security
    else if (target->GetTypeId() == TYPEID_PLAYER && HasLowerSecurity((Player*)target, 0))
        return false;

    target->SetDisplayId(display_id);

    return true;
}

//kick player
bool ChatHandler::HandleKickPlayerCommand(const char *args)
{
    Player* target;
    if(!extractPlayerTarget((char*)args,&target))
        return false;

    if (m_session && target==m_session->GetPlayer())
    {
        SendSysMessage(LANG_COMMAND_KICKSELF);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(target, 0))
        return false;

    // send before target pointer invalidate
    PSendSysMessage(LANG_COMMAND_KICKMESSAGE,GetNameLink(target).c_str());
    target->GetSession()->KickPlayer();
    return true;
}

//set temporary phase mask for player
bool ChatHandler::HandleModifyPhaseCommand(const char* args)
{
    if (!*args)
        return false;

    uint32 phasemask = (uint32)atoi((char*)args);

    Unit *target = getSelectedUnit();
    if(!target)
        target = m_session->GetPlayer();

    // check online security
    else if (target->GetTypeId() == TYPEID_PLAYER && HasLowerSecurity((Player*)target, 0))
        return false;

    target->SetPhaseMask(phasemask,true);

    return true;
}

//show info of player
bool ChatHandler::HandlePInfoCommand(const char* args)
{
    Player* target;
    uint64 target_guid;
    std::string target_name;
    if(!extractPlayerTarget((char*)args,&target,&target_guid,&target_name))
        return false;

    uint32 accId = 0;
    uint32 money = 0;
    uint32 total_player_time = 0;
    uint32 level = 0;
    uint32 latency = 0;

    // get additional information from Player object
    if(target)
    {
        // check online security
        if (HasLowerSecurity(target, 0))
            return false;

        accId = target->GetSession()->GetAccountId();
        money = target->GetMoney();
        total_player_time = target->GetTotalPlayedTime();
        level = target->getLevel();
        latency = target->GetSession()->GetLatency();
    }
    // get additional information from DB
    else
    {
        // check offline security
        if (HasLowerSecurity(NULL, target_guid))
            return false;

        //                                                     0          1      2      3
        QueryResult *result = CharacterDatabase.PQuery("SELECT totaltime, level, money, account FROM characters WHERE guid = '%u'", GUID_LOPART(target_guid));
        if (!result)
            return false;

        Field *fields = result->Fetch();
        total_player_time = fields[0].GetUInt32();
        level = fields[1].GetUInt32();
        money = fields[2].GetUInt32();
        accId = fields[3].GetUInt32();
        delete result;
    }

    std::string username = GetMangosString(LANG_ERROR);
    std::string last_ip = GetMangosString(LANG_ERROR);
    AccountTypes security = SEC_PLAYER;
    std::string last_login = GetMangosString(LANG_ERROR);

    QueryResult* result = loginDatabase.PQuery("SELECT username,gmlevel,last_ip,last_login FROM account WHERE id = '%u'",accId);
    if(result)
    {
        Field* fields = result->Fetch();
        username = fields[0].GetCppString();
        security = (AccountTypes)fields[1].GetUInt32();

        if(!m_session || m_session->GetSecurity() >= security)
        {
            last_ip = fields[2].GetCppString();
            last_login = fields[3].GetCppString();
        }
        else
        {
            last_ip = "-";
            last_login = "-";
        }

        delete result;
    }

    std::string nameLink = playerLink(target_name);

    PSendSysMessage(LANG_PINFO_ACCOUNT, (target?"":GetMangosString(LANG_OFFLINE)), nameLink.c_str(), GUID_LOPART(target_guid), username.c_str(), accId, security, last_ip.c_str(), last_login.c_str(), latency);

    std::string timeStr = secsToTimeString(total_player_time,true,true);
    uint32 gold = money /GOLD;
    uint32 silv = (money % GOLD) / SILVER;
    uint32 copp = (money % GOLD) % SILVER;
    PSendSysMessage(LANG_PINFO_LEVEL,  timeStr.c_str(), level, gold,silv,copp );

    return true;
}

//show tickets
void ChatHandler::ShowTicket(uint64 guid, char const* text, char const* time)
{
    std::string name;
    if(!sObjectMgr.GetPlayerNameByGUID(guid,name))
        name = GetMangosString(LANG_UNKNOWN);

    std::string nameLink = playerLink(name);

    PSendSysMessage(LANG_COMMAND_TICKETVIEW, nameLink.c_str(),time,text);
}

//ticket commands
bool ChatHandler::HandleTicketCommand(const char* args)
{
    char* px = strtok((char*)args, " ");

    // ticket<end>
    if (!px)
    {
        if(!m_session)
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }

        size_t count = sTicketMgr.GetTicketCount();

        bool accept = m_session->GetPlayer()->isAcceptTickets();

        PSendSysMessage(LANG_COMMAND_TICKETCOUNT, count, accept ?  GetMangosString(LANG_ON) : GetMangosString(LANG_OFF));
        return true;
    }

    // ticket on
    if(strncmp(px,"on",3) == 0)
    {
        if(!m_session)
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }

        m_session->GetPlayer()->SetAcceptTicket(true);
        SendSysMessage(LANG_COMMAND_TICKETON);
        return true;
    }

    // ticket off
    if(strncmp(px,"off",4) == 0)
    {
        if(!m_session)
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }

        m_session->GetPlayer()->SetAcceptTicket(false);
        SendSysMessage(LANG_COMMAND_TICKETOFF);
        return true;
    }

    // ticket respond
    if(strncmp(px,"respond",8) == 0)
    {
        char *name = strtok(NULL, " ");

        if(!name)
        {
            SendSysMessage(LANG_CMD_SYNTAX);
            SetSentErrorMessage(true);
            return false;
        }

        std::string plName = name;
        uint64 guid = sObjectMgr.GetPlayerGUIDByName(plName);

        if(!guid)
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }

        GMTicket* ticket = sTicketMgr.GetGMTicket(GUID_LOPART(guid));

        if(!ticket)
        {
            PSendSysMessage(LANG_COMMAND_TICKETNOTEXIST, GUID_LOPART(guid));
            SetSentErrorMessage(true);
            return false;
        }

        char* response = strtok(NULL, "");

        if(!response)
        {
            SendSysMessage(LANG_CMD_SYNTAX);
            SetSentErrorMessage(true);
            return false;
        }

        ticket->SetResponseText(response);

        if(Player* pl = sObjectMgr.GetPlayer(guid))
            pl->GetSession()->SendGMResponse(ticket);

        return true;
    }

    // ticket #num
    int num = atoi(px);
    if(num > 0)
    {
        QueryResult *result = CharacterDatabase.PQuery("SELECT guid,ticket_text,ticket_lastchange FROM character_ticket ORDER BY ticket_id ASC "_OFFSET_, num-1);

        if(!result)
        {
            PSendSysMessage(LANG_COMMAND_TICKETNOTEXIST, num);
            SetSentErrorMessage(true);
            return false;
        }

        Field* fields = result->Fetch();

        uint32 guid = fields[0].GetUInt32();
        char const* text = fields[1].GetString();
        char const* time = fields[2].GetString();

        ShowTicket(MAKE_NEW_GUID(guid, 0, HIGHGUID_PLAYER),text,time);
        delete result;
        return true;
    }

    uint64 target_guid;
    if(!extractPlayerTarget(px,NULL,&target_guid))
        return false;

    // ticket $char_name
    GMTicket* ticket = sTicketMgr.GetGMTicket(GUID_LOPART(target_guid));
    if(!ticket)
        return false;

    std::string time = TimeToTimestampStr(ticket->GetLastUpdate());

    ShowTicket(target_guid, ticket->GetText(), time.c_str());

    return true;
}

//dell all tickets
bool ChatHandler::HandleDelTicketCommand(const char *args)
{
    char* px = strtok((char*)args, " ");
    if (!px)
        return false;

    // delticket all
    if(strncmp(px,"all",4) == 0)
    {
        sTicketMgr.DeleteAll();
        SendSysMessage(LANG_COMMAND_ALLTICKETDELETED);
        return true;
    }

    int num = (uint32)atoi(px);

    // delticket #num
    if(num > 0)
    {
        QueryResult* result = CharacterDatabase.PQuery("SELECT guid FROM character_ticket ORDER BY ticket_id ASC "_OFFSET_,num-1);
        if(!result)
        {
            PSendSysMessage(LANG_COMMAND_TICKETNOTEXIST, num);
            SetSentErrorMessage(true);
            return false;
        }
        Field* fields = result->Fetch();
        uint32 guid = fields[0].GetUInt32();
        delete result;

        sTicketMgr.Delete(guid);

        //notify player
        if(Player* pl = sObjectMgr.GetPlayer(MAKE_NEW_GUID(guid, 0, HIGHGUID_PLAYER)))
        {
            pl->GetSession()->SendGMTicketGetTicket(0x0A, 0);
            PSendSysMessage(LANG_COMMAND_TICKETPLAYERDEL, GetNameLink(pl).c_str());
        }
        else
            PSendSysMessage(LANG_COMMAND_TICKETDEL);

        return true;
    }

    Player* target;
    uint64 target_guid;
    std::string target_name;
    if(!extractPlayerTarget(px,&target,&target_guid,&target_name))
        return false;

    // delticket $char_name
    sTicketMgr.Delete(GUID_LOPART(target_guid));

    // notify players about ticket deleting
    if(target)
        target->GetSession()->SendGMTicketGetTicket(0x0A,0);

    std::string nameLink = playerLink(target_name);

    PSendSysMessage(LANG_COMMAND_TICKETPLAYERDEL,nameLink.c_str());
    return true;
}

/**
 * Add a waypoint to a creature.
 *
 * The user can either select an npc or provide its GUID.
 *
 * The user can even select a visual waypoint - then the new waypoint
 * is placed *after* the selected one - this makes insertion of new
 * waypoints possible.
 *
 * eg:
 * .wp add 12345
 * -> adds a waypoint to the npc with the GUID 12345
 *
 * .wp add
 * -> adds a waypoint to the currently selected creature
 *
 *
 * @param args if the user did not provide a GUID, it is NULL
 *
 * @return true - command did succeed, false - something went wrong
 */
bool ChatHandler::HandleWpAddCommand(const char* args)
{
    sLog.outDebug("DEBUG: HandleWpAddCommand");

    // optional
    char* guid_str = NULL;

    if(*args)
    {
        guid_str = strtok((char*)args, " ");
    }

    uint32 lowguid = 0;
    uint32 point = 0;
    Creature* target = getSelectedCreature();
    // Did player provide a GUID?
    if (!guid_str)
    {
        sLog.outDebug("DEBUG: HandleWpAddCommand - No GUID provided");

        // No GUID provided
        // -> Player must have selected a creature

        if(!target || target->isPet())
        {
            SendSysMessage(LANG_SELECT_CREATURE);
            SetSentErrorMessage(true);
            return false;
        }
        if (target->GetEntry() == VISUAL_WAYPOINT )
        {
            sLog.outDebug("DEBUG: HandleWpAddCommand - target->GetEntry() == VISUAL_WAYPOINT (1) ");

            QueryResult *result =
                WorldDatabase.PQuery( "SELECT id, point FROM creature_movement WHERE wpguid = %u",
                target->GetGUIDLow() );
            if(!result)
            {
                PSendSysMessage(LANG_WAYPOINT_NOTFOUNDSEARCH, target->GetGUIDLow());
                // User selected a visual spawnpoint -> get the NPC
                // Select NPC GUID
                // Since we compare float values, we have to deal with
                // some difficulties.
                // Here we search for all waypoints that only differ in one from 1 thousand
                // (0.001) - There is no other way to compare C++ floats with mySQL floats
                // See also: http://dev.mysql.com/doc/refman/5.0/en/problems-with-float.html
                const char* maxDIFF = "0.01";
                result = WorldDatabase.PQuery( "SELECT id, point FROM creature_movement WHERE (abs(position_x - %f) <= %s ) and (abs(position_y - %f) <= %s ) and (abs(position_z - %f) <= %s )",
                    target->GetPositionX(), maxDIFF, target->GetPositionY(), maxDIFF, target->GetPositionZ(), maxDIFF);
                if(!result)
                {
                    PSendSysMessage(LANG_WAYPOINT_NOTFOUNDDBPROBLEM, target->GetGUIDLow());
                    SetSentErrorMessage(true);
                    return false;
                }
            }
            do
            {
                Field *fields = result->Fetch();
                lowguid = fields[0].GetUInt32();
                point   = fields[1].GetUInt32();
            }while( result->NextRow() );
            delete result;

            CreatureData const* data = sObjectMgr.GetCreatureData(lowguid);
            if(!data)
            {
                PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, lowguid);
                SetSentErrorMessage(true);
                return false;
            }

            target = m_session->GetPlayer()->GetMap()->GetCreature(MAKE_NEW_GUID(lowguid,data->id,HIGHGUID_UNIT));
            if(!target)
            {
                PSendSysMessage(LANG_WAYPOINT_NOTFOUNDDBPROBLEM, lowguid);
                SetSentErrorMessage(true);
                return false;
            }
        }
        else
        {
            lowguid = target->GetDBTableGUIDLow();
        }
    }
    else
    {
        sLog.outDebug("DEBUG: HandleWpAddCommand - GUID provided");

        // GUID provided
        // Warn if player also selected a creature
        // -> Creature selection is ignored <-
        if(target)
        {
            SendSysMessage(LANG_WAYPOINT_CREATSELECTED);
        }
        lowguid = atoi((char*)guid_str);

        CreatureData const* data = sObjectMgr.GetCreatureData(lowguid);
        if(!data)
        {
            PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, lowguid);
            SetSentErrorMessage(true);
            return false;
        }

        target = m_session->GetPlayer()->GetMap()->GetCreature(MAKE_NEW_GUID(lowguid,data->id,HIGHGUID_UNIT));
        if(!target || target->isPet())
        {
            PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, lowguid);
            SetSentErrorMessage(true);
            return false;
        }
    }
    // lowguid -> GUID of the NPC
    // point   -> number of the waypoint (if not 0)
    sLog.outDebug("DEBUG: HandleWpAddCommand - danach");

    sLog.outDebug("DEBUG: HandleWpAddCommand - point == 0");

    Player* player = m_session->GetPlayer();
    sWaypointMgr.AddLastNode(lowguid, player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation(), 0, 0);

    // update movement type
    if(target)
    {
        target->SetDefaultMovementType(WAYPOINT_MOTION_TYPE);
        target->GetMotionMaster()->Initialize();
        if(target->isAlive())                               // dead creature will reset movement generator at respawn
        {
            target->setDeathState(JUST_DIED);
            target->Respawn();
        }
        target->SaveToDB();
    }
    else
        WorldDatabase.PExecuteLog("UPDATE creature SET MovementType = '%u' WHERE guid = '%u'", WAYPOINT_MOTION_TYPE,lowguid);

    PSendSysMessage(LANG_WAYPOINT_ADDED, point, lowguid);

    return true;
}                                                           // HandleWpAddCommand

/**
 * .wp modify emote | spell | text | del | move | add
 *
 * add -> add a WP after the selected visual waypoint
 *        User must select a visual waypoint and then issue ".wp modify add"
 *
 * emote <emoteID>
 *   User has selected a visual waypoint before.
 *   <emoteID> is added to this waypoint. Everytime the
 *   NPC comes to this waypoint, the emote is called.
 *
 * emote <GUID> <WPNUM> <emoteID>
 *   User has not selected visual waypoint before.
 *   For the waypoint <WPNUM> for the NPC with <GUID>
 *   an emote <emoteID> is added.
 *   Everytime the NPC comes to this waypoint, the emote is called.
 *
 *
 * info <GUID> <WPNUM> -> User did not select a visual waypoint and
 */
bool ChatHandler::HandleWpModifyCommand(const char* args)
{
    sLog.outDebug("DEBUG: HandleWpModifyCommand");

    if(!*args)
        return false;

    // first arg: add del text emote spell waittime move
    char* show_str = strtok((char*)args, " ");
    if (!show_str)
    {
        return false;
    }

    std::string show = show_str;
    // Check
    // Remember: "show" must also be the name of a column!
    if( (show != "emote") && (show != "spell") && (show != "textid1") && (show != "textid2")
        && (show != "textid3") && (show != "textid4") && (show != "textid5")
        && (show != "waittime") && (show != "del") && (show != "move") && (show != "add")
        && (show != "model1") && (show != "model2") && (show != "orientation"))
    {
        return false;
    }

    // Next arg is: <GUID> <WPNUM> <ARGUMENT>

    // Did user provide a GUID
    // or did the user select a creature?
    // -> variable lowguid is filled with the GUID of the NPC
    uint32 lowguid = 0;
    uint32 point = 0;
    uint32 wpGuid = 0;
    Creature* target = getSelectedCreature();

    if(target)
    {
        sLog.outDebug("DEBUG: HandleWpModifyCommand - User did select an NPC");

        // Did the user select a visual spawnpoint?
        if (target->GetEntry() != VISUAL_WAYPOINT )
        {
            PSendSysMessage(LANG_WAYPOINT_VP_SELECT);
            SetSentErrorMessage(true);
            return false;
        }

        wpGuid = target->GetGUIDLow();

        // The visual waypoint
        QueryResult *result =
            WorldDatabase.PQuery( "SELECT id, point FROM creature_movement WHERE wpguid = %u LIMIT 1",
            target->GetGUIDLow() );
        if(!result)
        {
            PSendSysMessage(LANG_WAYPOINT_NOTFOUNDDBPROBLEM, wpGuid);
            SetSentErrorMessage(true);
            return false;
        }
        sLog.outDebug("DEBUG: HandleWpModifyCommand - After getting wpGuid");

        Field *fields = result->Fetch();
        lowguid = fields[0].GetUInt32();
        point   = fields[1].GetUInt32();

        // Cleanup memory
        sLog.outDebug("DEBUG: HandleWpModifyCommand - Cleanup memory");
        delete result;
    }
    else
    {
        // User did provide <GUID> <WPNUM>

        char* guid_str = strtok((char*)NULL, " ");
        if( !guid_str )
        {
            SendSysMessage(LANG_WAYPOINT_NOGUID);
            return false;
        }
        lowguid = atoi((char*)guid_str);

        CreatureData const* data = sObjectMgr.GetCreatureData(lowguid);
        if(!data)
        {
            PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, lowguid);
            SetSentErrorMessage(true);
            return false;
        }

        PSendSysMessage("DEBUG: GUID provided: %d", lowguid);

        char* point_str = strtok((char*)NULL, " ");
        if( !point_str )
        {
            SendSysMessage(LANG_WAYPOINT_NOWAYPOINTGIVEN);
            return false;
        }
        point    = atoi((char*)point_str);

        PSendSysMessage("DEBUG: wpNumber provided: %d", point);

        // Now we need the GUID of the visual waypoint
        // -> "del", "move", "add" command

        QueryResult *result = WorldDatabase.PQuery( "SELECT wpguid FROM creature_movement WHERE id = '%u' AND point = '%u' LIMIT 1", lowguid, point);
        if (!result)
        {
            PSendSysMessage(LANG_WAYPOINT_NOTFOUNDSEARCH, lowguid, point);
            SetSentErrorMessage(true);
            return false;
        }

        Field *fields = result->Fetch();
        wpGuid  = fields[0].GetUInt32();

        // Free memory
        delete result;
    }

    char* arg_str = NULL;
    // Check for argument
    if( (show.find("text") == std::string::npos ) && (show != "del") && (show != "move") && (show != "add"))
    {
        // Text is enclosed in "<>", all other arguments not
        if( show.find("text") != std::string::npos )
            arg_str = strtok((char*)NULL, "<>");
        else
            arg_str = strtok((char*)NULL, " ");

        if( !arg_str)
        {
            PSendSysMessage(LANG_WAYPOINT_ARGUMENTREQ, show_str);
            return false;
        }
    }

    sLog.outDebug("DEBUG: HandleWpModifyCommand - Parameters parsed - now execute the command");

    // wpGuid  -> GUID of the waypoint creature
    // lowguid -> GUID of the NPC
    // point   -> waypoint number

    // Special functions:
    // add - move - del -> no args commands
    // Add a waypoint after the selected visual
    if(show == "add" && target)
    {
        PSendSysMessage("DEBUG: wp modify add, GUID: %u", lowguid);

        // Get the creature for which we read the waypoint
        CreatureData const* data = sObjectMgr.GetCreatureData(lowguid);
        if(!data)
        {
            PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, lowguid);
            SetSentErrorMessage(true);
            return false;
        }

        Creature* npcCreature = m_session->GetPlayer()->GetMap()->GetCreature(MAKE_NEW_GUID(lowguid, data->id, HIGHGUID_UNIT));

        if( !npcCreature )
        {
            PSendSysMessage(LANG_WAYPOINT_NPCNOTFOUND);
            SetSentErrorMessage(true);
            return false;
        }

        sLog.outDebug("DEBUG: HandleWpModifyCommand - add -- npcCreature");

        // What to do:
        // Add the visual spawnpoint (DB only)
        // Adjust the waypoints
        // Respawn the owner of the waypoints
        sLog.outDebug("DEBUG: HandleWpModifyCommand - add");

        Player* chr = m_session->GetPlayer();
        Map *map = chr->GetMap();

        if(npcCreature)
        {
            npcCreature->GetMotionMaster()->Initialize();
            if(npcCreature->isAlive())                      // dead creature will reset movement generator at respawn
            {
                npcCreature->setDeathState(JUST_DIED);
                npcCreature->Respawn();
            }
        }

        // create the waypoint creature
        wpGuid = 0;
        Creature* wpCreature = new Creature;
        if (!wpCreature->Create(sObjectMgr.GenerateLowGuid(HIGHGUID_UNIT), map, chr->GetPhaseMaskForSpawn(), VISUAL_WAYPOINT,0))
        {
            PSendSysMessage(LANG_WAYPOINT_VP_NOTCREATED, VISUAL_WAYPOINT);
            delete wpCreature;
        }
        else
        {
            wpCreature->Relocate(chr->GetPositionX(), chr->GetPositionY(), chr->GetPositionZ(), chr->GetOrientation());

            if(!wpCreature->IsPositionValid())
            {
                sLog.outError("Creature (guidlow %d, entry %d) not created. Suggested coordinates isn't valid (X: %f Y: %f)",wpCreature->GetGUIDLow(),wpCreature->GetEntry(),wpCreature->GetPositionX(),wpCreature->GetPositionY());
                delete wpCreature;
            }
            else
            {
                wpCreature->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), chr->GetPhaseMaskForSpawn());
                // To call _LoadGoods(); _LoadQuests(); CreateTrainerSpells();
                wpCreature->LoadFromDB(wpCreature->GetDBTableGUIDLow(), map);
                map->Add(wpCreature);
                wpGuid = wpCreature->GetGUIDLow();
            }
        }

        sWaypointMgr.AddAfterNode(lowguid, point, chr->GetPositionX(), chr->GetPositionY(), chr->GetPositionZ(), 0, 0, wpGuid);

        if(!wpGuid)
            return false;

        PSendSysMessage(LANG_WAYPOINT_ADDED_NO, point+1);
        return true;
    }                                                       // add

    if(show == "del" && target)
    {
        PSendSysMessage("DEBUG: wp modify del, GUID: %u", lowguid);

        // Get the creature for which we read the waypoint
        CreatureData const* data = sObjectMgr.GetCreatureData(lowguid);
        if(!data)
        {
            PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, lowguid);
            SetSentErrorMessage(true);
            return false;
        }

        Creature* npcCreature = m_session->GetPlayer()->GetMap()->GetCreature(MAKE_NEW_GUID(lowguid, data->id, HIGHGUID_UNIT));

        // wpCreature
        Creature* wpCreature = NULL;
        if( wpGuid != 0 )
        {
            wpCreature = m_session->GetPlayer()->GetMap()->GetCreature(MAKE_NEW_GUID(wpGuid, VISUAL_WAYPOINT, HIGHGUID_UNIT));
            wpCreature->DeleteFromDB();
            wpCreature->AddObjectToRemoveList();
        }

        // What to do:
        // Remove the visual spawnpoint
        // Adjust the waypoints
        // Respawn the owner of the waypoints

        sWaypointMgr.DeleteNode(lowguid, point);

        if(npcCreature)
        {
            // Any waypoints left?
            QueryResult *result2 = WorldDatabase.PQuery( "SELECT point FROM creature_movement WHERE id = '%u'",lowguid);
            if(!result2)
            {
                npcCreature->SetDefaultMovementType(RANDOM_MOTION_TYPE);
            }
            else
            {
                npcCreature->SetDefaultMovementType(WAYPOINT_MOTION_TYPE);
                delete result2;
            }
            npcCreature->GetMotionMaster()->Initialize();
            if(npcCreature->isAlive())                      // dead creature will reset movement generator at respawn
            {
                npcCreature->setDeathState(JUST_DIED);
                npcCreature->Respawn();
            }
            npcCreature->SaveToDB();
        }

        PSendSysMessage(LANG_WAYPOINT_REMOVED);
        return true;
    }                                                       // del

    if(show == "move" && target)
    {
        PSendSysMessage("DEBUG: wp move, GUID: %u", lowguid);

        Player *chr = m_session->GetPlayer();
        Map *map = chr->GetMap();
        {
            // Get the creature for which we read the waypoint
            CreatureData const* data = sObjectMgr.GetCreatureData(lowguid);
            if(!data)
            {
                PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, lowguid);
                SetSentErrorMessage(true);
                return false;
            }

            Creature* npcCreature = m_session->GetPlayer()->GetMap()->GetCreature(MAKE_NEW_GUID(lowguid, data->id, HIGHGUID_UNIT));

            // wpCreature
            Creature* wpCreature = NULL;
            // What to do:
            // Move the visual spawnpoint
            // Respawn the owner of the waypoints
            if( wpGuid != 0 )
            {
                wpCreature = m_session->GetPlayer()->GetMap()->GetCreature(MAKE_NEW_GUID(wpGuid, VISUAL_WAYPOINT, HIGHGUID_UNIT));
                wpCreature->DeleteFromDB();
                wpCreature->AddObjectToRemoveList();
                // re-create
                Creature* wpCreature2 = new Creature;
                if (!wpCreature2->Create(sObjectMgr.GenerateLowGuid(HIGHGUID_UNIT), map, chr->GetPhaseMaskForSpawn(), VISUAL_WAYPOINT, 0))
                {
                    PSendSysMessage(LANG_WAYPOINT_VP_NOTCREATED, VISUAL_WAYPOINT);
                    delete wpCreature2;
                    return false;
                }

                wpCreature2->Relocate(chr->GetPositionX(), chr->GetPositionY(), chr->GetPositionZ(), chr->GetOrientation());

                if(!wpCreature2->IsPositionValid())
                {
                    sLog.outError("Creature (guidlow %d, entry %d) not created. Suggested coordinates isn't valid (X: %f Y: %f)",wpCreature2->GetGUIDLow(),wpCreature2->GetEntry(),wpCreature2->GetPositionX(),wpCreature2->GetPositionY());
                    delete wpCreature2;
                    return false;
                }

                wpCreature2->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), chr->GetPhaseMaskForSpawn());
                // To call _LoadGoods(); _LoadQuests(); CreateTrainerSpells();
                wpCreature2->LoadFromDB(wpCreature2->GetDBTableGUIDLow(), map);
                map->Add(wpCreature2);
                //npcCreature->GetMap()->Add(wpCreature2);
            }

            sWaypointMgr.SetNodePosition(lowguid, point, chr->GetPositionX(), chr->GetPositionY(), chr->GetPositionZ());

            if(npcCreature)
            {
                npcCreature->GetMotionMaster()->Initialize();
                if(npcCreature->isAlive())                  // dead creature will reset movement generator at respawn
                {
                    npcCreature->setDeathState(JUST_DIED);
                    npcCreature->Respawn();
                }
            }
            PSendSysMessage(LANG_WAYPOINT_CHANGED);
        }
        return true;
    }                                                       // move

    // Create creature - npc that has the waypoint
    CreatureData const* data = sObjectMgr.GetCreatureData(lowguid);
    if(!data)
    {
        PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, lowguid);
        SetSentErrorMessage(true);
        return false;
    }

    // set in game textids not supported
    if( show == "textid1" || show == "textid2" || show == "textid3" ||
        show == "textid4" || show == "textid5" )
    {
        return false;
    }

    sWaypointMgr.SetNodeText(lowguid, point, show_str, arg_str);

    Creature* npcCreature = m_session->GetPlayer()->GetMap()->GetCreature(MAKE_NEW_GUID(lowguid, data->id, HIGHGUID_UNIT));
    if(npcCreature)
    {
        npcCreature->SetDefaultMovementType(WAYPOINT_MOTION_TYPE);
        npcCreature->GetMotionMaster()->Initialize();
        if(npcCreature->isAlive())                          // dead creature will reset movement generator at respawn
        {
            npcCreature->setDeathState(JUST_DIED);
            npcCreature->Respawn();
        }
    }
    PSendSysMessage(LANG_WAYPOINT_CHANGED_NO, show_str);

    return true;
}

/**
 * .wp show info | on | off
 *
 * info -> User has selected a visual waypoint before
 *
 * info <GUID> <WPNUM> -> User did not select a visual waypoint and
 *                        provided the GUID of the NPC and the number of
 *                        the waypoint.
 *
 * on -> User has selected an NPC; all visual waypoints for this
 *       NPC are added to the world
 *
 * on <GUID> -> User did not select an NPC - instead the GUID of the
 *              NPC is provided. All visual waypoints for this NPC
 *              are added from the world.
 *
 * off -> User has selected an NPC; all visual waypoints for this
 *        NPC are removed from the world.
 *
 * on <GUID> -> User did not select an NPC - instead the GUID of the
 *              NPC is provided. All visual waypoints for this NPC
 *              are removed from the world.
 *
 *
 */
bool ChatHandler::HandleWpShowCommand(const char* args)
{
    sLog.outDebug("DEBUG: HandleWpShowCommand");

    if(!*args)
        return false;

    // first arg: on, off, first, last
    char* show_str = strtok((char*)args, " ");
    if (!show_str)
    {
        return false;
    }
    // second arg: GUID (optional, if a creature is selected)
    char* guid_str = strtok((char*)NULL, " ");
    sLog.outDebug("DEBUG: HandleWpShowCommand: show_str: %s guid_str: %s", show_str, guid_str);
    //if (!guid_str) {
    //    return false;
    //}

    // Did user provide a GUID
    // or did the user select a creature?
    // -> variable lowguid is filled with the GUID
    Creature* target = getSelectedCreature();
    // Did player provide a GUID?
    if (!guid_str)
    {
        sLog.outDebug("DEBUG: HandleWpShowCommand: !guid_str");
        // No GUID provided
        // -> Player must have selected a creature

        if(!target)
        {
            SendSysMessage(LANG_SELECT_CREATURE);
            SetSentErrorMessage(true);
            return false;
        }
    }
    else
    {
        sLog.outDebug("DEBUG: HandleWpShowCommand: GUID provided");
        // GUID provided
        // Warn if player also selected a creature
        // -> Creature selection is ignored <-
        if(target)
        {
            SendSysMessage(LANG_WAYPOINT_CREATSELECTED);
        }

        uint32 lowguid = atoi((char*)guid_str);

        CreatureData const* data = sObjectMgr.GetCreatureData(lowguid);
        if(!data)
        {
            PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, lowguid);
            SetSentErrorMessage(true);
            return false;
        }

        target = m_session->GetPlayer()->GetMap()->GetCreature(MAKE_NEW_GUID(lowguid,data->id,HIGHGUID_UNIT));

        if(!target)
        {
            PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, lowguid);
            SetSentErrorMessage(true);
            return false;
        }
    }

    uint32 lowguid = target->GetDBTableGUIDLow();

    std::string show = show_str;
    uint32 Maxpoint;

    sLog.outDebug("DEBUG: HandleWpShowCommand: lowguid: %u show: %s", lowguid, show_str);

    // Show info for the selected waypoint
    if(show == "info")
    {
        PSendSysMessage("DEBUG: wp info, GUID: %u", lowguid);

        // Check if the user did specify a visual waypoint
        if( target->GetEntry() != VISUAL_WAYPOINT )
        {
            PSendSysMessage(LANG_WAYPOINT_VP_SELECT);
            SetSentErrorMessage(true);
            return false;
        }

        //PSendSysMessage("wp on, GUID: %u", lowguid);

        //pCreature->GetPositionX();

        QueryResult *result =
            WorldDatabase.PQuery( "SELECT id, point, waittime, emote, spell, textid1, textid2, textid3, textid4, textid5, model1, model2 FROM creature_movement WHERE wpguid = %u",
            target->GetGUIDLow() );
        if(!result)
        {
            // Since we compare float values, we have to deal with
            // some difficulties.
            // Here we search for all waypoints that only differ in one from 1 thousand
            // (0.001) - There is no other way to compare C++ floats with mySQL floats
            // See also: http://dev.mysql.com/doc/refman/5.0/en/problems-with-float.html
            const char* maxDIFF = "0.01";
            PSendSysMessage(LANG_WAYPOINT_NOTFOUNDSEARCH, target->GetGUID());

            result = WorldDatabase.PQuery( "SELECT id, point, waittime, emote, spell, textid1, textid2, textid3, textid4, textid5, model1, model2 FROM creature_movement WHERE (abs(position_x - %f) <= %s ) and (abs(position_y - %f) <= %s ) and (abs(position_z - %f) <= %s )",
                target->GetPositionX(), maxDIFF, target->GetPositionY(), maxDIFF, target->GetPositionZ(), maxDIFF);
            if(!result)
            {
                PSendSysMessage(LANG_WAYPOINT_NOTFOUNDDBPROBLEM, lowguid);
                SetSentErrorMessage(true);
                return false;
            }
        }
        do
        {
            Field *fields = result->Fetch();
            uint32 creGUID          = fields[0].GetUInt32();
            uint32 point            = fields[1].GetUInt32();
            int waittime            = fields[2].GetUInt32();
            uint32 emote            = fields[3].GetUInt32();
            uint32 spell            = fields[4].GetUInt32();
            uint32 textid[MAX_WAYPOINT_TEXT];
            for(int i = 0;  i < MAX_WAYPOINT_TEXT; ++i)
                textid[i]           = fields[5+i].GetUInt32();
            uint32 model1           = fields[10].GetUInt32();
            uint32 model2           = fields[11].GetUInt32();

            // Get the creature for which we read the waypoint
            Creature* wpCreature = m_session->GetPlayer()->GetMap()->GetCreature(MAKE_NEW_GUID(creGUID,VISUAL_WAYPOINT,HIGHGUID_UNIT));

            PSendSysMessage(LANG_WAYPOINT_INFO_TITLE, point, (wpCreature ? wpCreature->GetName() : "<not found>"), creGUID);
            PSendSysMessage(LANG_WAYPOINT_INFO_WAITTIME, waittime);
            PSendSysMessage(LANG_WAYPOINT_INFO_MODEL, 1, model1);
            PSendSysMessage(LANG_WAYPOINT_INFO_MODEL, 2, model2);
            PSendSysMessage(LANG_WAYPOINT_INFO_EMOTE, emote);
            PSendSysMessage(LANG_WAYPOINT_INFO_SPELL, spell);
            for(int i = 0;  i < MAX_WAYPOINT_TEXT; ++i)
                PSendSysMessage(LANG_WAYPOINT_INFO_TEXT, i+1, textid[i], (textid[i] ? GetMangosString(textid[i]) : ""));

        }while( result->NextRow() );
        // Cleanup memory
        delete result;
        return true;
    }

    if(show == "on")
    {
        PSendSysMessage("DEBUG: wp on, GUID: %u", lowguid);

        QueryResult *result = WorldDatabase.PQuery( "SELECT point, position_x,position_y,position_z FROM creature_movement WHERE id = '%u'",lowguid);
        if(!result)
        {
            PSendSysMessage(LANG_WAYPOINT_NOTFOUND, lowguid);
            SetSentErrorMessage(true);
            return false;
        }
        // Delete all visuals for this NPC
        QueryResult *result2 = WorldDatabase.PQuery( "SELECT wpguid FROM creature_movement WHERE id = '%u' and wpguid <> 0", lowguid);
        if(result2)
        {
            bool hasError = false;
            do
            {
                Field *fields = result2->Fetch();
                uint32 wpguid = fields[0].GetUInt32();
                Creature* pCreature = m_session->GetPlayer()->GetMap()->GetCreature(MAKE_NEW_GUID(wpguid,VISUAL_WAYPOINT,HIGHGUID_UNIT));

                if(!pCreature)
                {
                    PSendSysMessage(LANG_WAYPOINT_NOTREMOVED, wpguid);
                    hasError = true;
                    WorldDatabase.PExecuteLog("DELETE FROM creature WHERE guid = '%u'", wpguid);
                }
                else
                {
                    pCreature->DeleteFromDB();
                    pCreature->AddObjectToRemoveList();
                }

            }while( result2->NextRow() );
            delete result2;
            if( hasError )
            {
                PSendSysMessage(LANG_WAYPOINT_TOOFAR1);
                PSendSysMessage(LANG_WAYPOINT_TOOFAR2);
                PSendSysMessage(LANG_WAYPOINT_TOOFAR3);
            }
        }

        do
        {
            Field *fields = result->Fetch();
            uint32 point    = fields[0].GetUInt32();
            float x         = fields[1].GetFloat();
            float y         = fields[2].GetFloat();
            float z         = fields[3].GetFloat();

            uint32 id = VISUAL_WAYPOINT;

            Player *chr = m_session->GetPlayer();
            Map *map = chr->GetMap();
            float o = chr->GetOrientation();

            Creature* wpCreature = new Creature;
            if (!wpCreature->Create(sObjectMgr.GenerateLowGuid(HIGHGUID_UNIT), map, chr->GetPhaseMaskForSpawn(), id, 0))
            {
                PSendSysMessage(LANG_WAYPOINT_VP_NOTCREATED, id);
                delete wpCreature;
                delete result;
                return false;
            }

            wpCreature->Relocate(x, y, z, o);

            if(!wpCreature->IsPositionValid())
            {
                sLog.outError("Creature (guidlow %d, entry %d) not created. Suggested coordinates isn't valid (X: %f Y: %f)",wpCreature->GetGUIDLow(),wpCreature->GetEntry(),wpCreature->GetPositionX(),wpCreature->GetPositionY());
                delete wpCreature;
                delete result;
                return false;
            }

            wpCreature->SetVisibility(VISIBILITY_OFF);
            sLog.outDebug("DEBUG: UPDATE creature_movement SET wpguid = '%u", wpCreature->GetGUIDLow());
            // set "wpguid" column to the visual waypoint
            WorldDatabase.PExecuteLog("UPDATE creature_movement SET wpguid = '%u' WHERE id = '%u' and point = '%u'", wpCreature->GetGUIDLow(), lowguid, point);

            wpCreature->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), chr->GetPhaseMaskForSpawn());
            // To call _LoadGoods(); _LoadQuests(); CreateTrainerSpells();
            wpCreature->LoadFromDB(wpCreature->GetDBTableGUIDLow(),map);
            map->Add(wpCreature);
            //wpCreature->GetMap()->Add(wpCreature);
        }while( result->NextRow() );

        // Cleanup memory
        delete result;
        return true;
    }

    if(show == "first")
    {
        PSendSysMessage("DEBUG: wp first, GUID: %u", lowguid);

        QueryResult *result = WorldDatabase.PQuery( "SELECT position_x,position_y,position_z FROM creature_movement WHERE point='1' AND id = '%u'",lowguid);
        if(!result)
        {
            PSendSysMessage(LANG_WAYPOINT_NOTFOUND, lowguid);
            SetSentErrorMessage(true);
            return false;
        }

        Field *fields = result->Fetch();
        float x         = fields[0].GetFloat();
        float y         = fields[1].GetFloat();
        float z         = fields[2].GetFloat();
        uint32 id = VISUAL_WAYPOINT;

        Player *chr = m_session->GetPlayer();
        float o = chr->GetOrientation();
        Map *map = chr->GetMap();

        Creature* pCreature = new Creature;
        if (!pCreature->Create(sObjectMgr.GenerateLowGuid(HIGHGUID_UNIT),map, chr->GetPhaseMaskForSpawn(), id, 0))
        {
            PSendSysMessage(LANG_WAYPOINT_VP_NOTCREATED, id);
            delete pCreature;
            delete result;
            return false;
        }

        pCreature->Relocate(x, y, z, o);

        if(!pCreature->IsPositionValid())
        {
            sLog.outError("Creature (guidlow %d, entry %d) not created. Suggested coordinates isn't valid (X: %f Y: %f)",pCreature->GetGUIDLow(),pCreature->GetEntry(),pCreature->GetPositionX(),pCreature->GetPositionY());
            delete pCreature;
            delete result;
            return false;
        }

        pCreature->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), chr->GetPhaseMaskForSpawn());
        pCreature->LoadFromDB(pCreature->GetDBTableGUIDLow(), map);
        map->Add(pCreature);
        //player->PlayerTalkClass->SendPointOfInterest(x, y, 6, 6, 0, "First Waypoint");

        // Cleanup memory
        delete result;
        return true;
    }

    if(show == "last")
    {
        PSendSysMessage("DEBUG: wp last, GUID: %u", lowguid);

        QueryResult *result = WorldDatabase.PQuery( "SELECT MAX(point) FROM creature_movement WHERE id = '%u'",lowguid);
        if( result )
        {
            Maxpoint = (*result)[0].GetUInt32();

            delete result;
        }
        else
            Maxpoint = 0;

        result = WorldDatabase.PQuery( "SELECT position_x,position_y,position_z FROM creature_movement WHERE point ='%u' AND id = '%u'",Maxpoint, lowguid);
        if(!result)
        {
            PSendSysMessage(LANG_WAYPOINT_NOTFOUNDLAST, lowguid);
            SetSentErrorMessage(true);
            return false;
        }
        Field *fields = result->Fetch();
        float x         = fields[0].GetFloat();
        float y         = fields[1].GetFloat();
        float z         = fields[2].GetFloat();
        uint32 id = VISUAL_WAYPOINT;

        Player *chr = m_session->GetPlayer();
        float o = chr->GetOrientation();
        Map *map = chr->GetMap();

        Creature* pCreature = new Creature;
        if (!pCreature->Create(sObjectMgr.GenerateLowGuid(HIGHGUID_UNIT), map, chr->GetPhaseMaskForSpawn(), id, 0))
        {
            PSendSysMessage(LANG_WAYPOINT_NOTCREATED, id);
            delete pCreature;
            delete result;
            return false;
        }

        pCreature->Relocate(x, y, z, o);

        if(!pCreature->IsPositionValid())
        {
            sLog.outError("Creature (guidlow %d, entry %d) not created. Suggested coordinates isn't valid (X: %f Y: %f)",pCreature->GetGUIDLow(),pCreature->GetEntry(),pCreature->GetPositionX(),pCreature->GetPositionY());
            delete pCreature;
            delete result;
            return false;
        }

        pCreature->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), chr->GetPhaseMaskForSpawn());
        pCreature->LoadFromDB(pCreature->GetDBTableGUIDLow(), map);
        map->Add(pCreature);
        //player->PlayerTalkClass->SendPointOfInterest(x, y, 6, 6, 0, "Last Waypoint");
        // Cleanup memory
        delete result;
        return true;
    }

    if(show == "off")
    {
        QueryResult *result = WorldDatabase.PQuery("SELECT guid FROM creature WHERE id = '%d'", VISUAL_WAYPOINT);
        if(!result)
        {
            SendSysMessage(LANG_WAYPOINT_VP_NOTFOUND);
            SetSentErrorMessage(true);
            return false;
        }
        bool hasError = false;
        do
        {
            Field *fields = result->Fetch();
            uint32 guid = fields[0].GetUInt32();
            Creature* pCreature = m_session->GetPlayer()->GetMap()->GetCreature(MAKE_NEW_GUID(guid,VISUAL_WAYPOINT,HIGHGUID_UNIT));
            if(!pCreature)
            {
                PSendSysMessage(LANG_WAYPOINT_NOTREMOVED, guid);
                hasError = true;
                WorldDatabase.PExecuteLog("DELETE FROM creature WHERE guid = '%u'", guid);
            }
            else
            {
                pCreature->DeleteFromDB();
                pCreature->AddObjectToRemoveList();
            }
        }while(result->NextRow());
        // set "wpguid" column to "empty" - no visual waypoint spawned
        WorldDatabase.PExecuteLog("UPDATE creature_movement SET wpguid = '0' WHERE wpguid <> '0'");

        if( hasError )
        {
            PSendSysMessage(LANG_WAYPOINT_TOOFAR1);
            PSendSysMessage(LANG_WAYPOINT_TOOFAR2);
            PSendSysMessage(LANG_WAYPOINT_TOOFAR3);
        }

        SendSysMessage(LANG_WAYPOINT_VP_ALLREMOVED);
        // Cleanup memory
        delete result;

        return true;
    }

    PSendSysMessage("DEBUG: wpshow - no valid command found");

    return true;
}                                                           // HandleWpShowCommand

bool ChatHandler::HandleWpExportCommand(const char *args)
{
    if(!*args)
        return false;

    // Next arg is: <GUID> <ARGUMENT>

    // Did user provide a GUID
    // or did the user select a creature?
    // -> variable lowguid is filled with the GUID of the NPC
    uint32 lowguid = 0;
    Creature* target = getSelectedCreature();
    char* arg_str = NULL;
    if (target)
    {
        if (target->GetEntry() != VISUAL_WAYPOINT)
            lowguid = target->GetGUIDLow();
        else
        {
            QueryResult *result = WorldDatabase.PQuery( "SELECT id FROM creature_movement WHERE wpguid = %u LIMIT 1", target->GetGUIDLow() );
            if (!result)
            {
                PSendSysMessage(LANG_WAYPOINT_NOTFOUNDDBPROBLEM, target->GetGUIDLow());
                return true;
            }
            Field *fields = result->Fetch();
            lowguid = fields[0].GetUInt32();;
            delete result;
        }

        arg_str = strtok((char*)args, " ");
    }
    else
    {
        // user provided <GUID>
        char* guid_str = strtok((char*)args, " ");
        if( !guid_str )
        {
            SendSysMessage(LANG_WAYPOINT_NOGUID);
            return false;
        }
        lowguid = atoi((char*)guid_str);

        arg_str = strtok((char*)NULL, " ");
    }

    if( !arg_str)
    {
        PSendSysMessage(LANG_WAYPOINT_ARGUMENTREQ, "export");
        return false;
    }

    PSendSysMessage("DEBUG: wp export, GUID: %u", lowguid);

    QueryResult *result = WorldDatabase.PQuery(
    //          0      1           2           3           4            5       6       7         8      9      10       11       12       13       14       15
        "SELECT point, position_x, position_y, position_z, orientation, model1, model2, waittime, emote, spell, textid1, textid2, textid3, textid4, textid5, id FROM creature_movement WHERE id = '%u' ORDER BY point", lowguid );

    if (!result)
    {
        PSendSysMessage(LANG_WAYPOINT_NOTHINGTOEXPORT);
        SetSentErrorMessage(true);
        return false;
    }

    std::ofstream outfile;
    outfile.open (arg_str);

    do
    {
        Field *fields = result->Fetch();

        outfile << "INSERT INTO creature_movement ";
        outfile << "( id, point, position_x, position_y, position_z, orientation, model1, model2, waittime, emote, spell, textid1, textid2, textid3, textid4, textid5 ) VALUES ";

        outfile << "( ";
        outfile << fields[15].GetUInt32();                  // id
        outfile << ", ";
        outfile << fields[0].GetUInt32();                   // point
        outfile << ", ";
        outfile << fields[1].GetFloat();                    // position_x
        outfile << ", ";
        outfile << fields[2].GetFloat();                    // position_y
        outfile << ", ";
        outfile << fields[3].GetUInt32();                   // position_z
        outfile << ", ";
        outfile << fields[4].GetUInt32();                   // orientation
        outfile << ", ";
        outfile << fields[5].GetUInt32();                   // model1
        outfile << ", ";
        outfile << fields[6].GetUInt32();                   // model2
        outfile << ", ";
        outfile << fields[7].GetUInt16();                   // waittime
        outfile << ", ";
        outfile << fields[8].GetUInt32();                   // emote
        outfile << ", ";
        outfile << fields[9].GetUInt32();                   // spell
        outfile << ", ";
        outfile << fields[10].GetUInt32();                  // textid1
        outfile << ", ";
        outfile << fields[11].GetUInt32();                  // textid2
        outfile << ", ";
        outfile << fields[12].GetUInt32();                  // textid3
        outfile << ", ";
        outfile << fields[13].GetUInt32();                  // textid4
        outfile << ", ";
        outfile << fields[14].GetUInt32();                  // textid5
        outfile << ");\n ";

    } while( result->NextRow() );
    delete result;

    PSendSysMessage(LANG_WAYPOINT_EXPORTED);
    outfile.close();

    return true;
}

bool ChatHandler::HandleWpImportCommand(const char *args)
{
    if(!*args)
        return false;

    char* arg_str = strtok((char*)args, " ");
    if (!arg_str)
        return false;

    std::string line;
    std::ifstream infile (arg_str);
    if (infile.is_open())
    {
        while (! infile.eof() )
        {
            getline (infile,line);
            //cout << line << endl;
            QueryResult *result = WorldDatabase.Query(line.c_str());
            delete result;
        }
        infile.close();
    }
    PSendSysMessage(LANG_WAYPOINT_IMPORTED);

    return true;
}

//rename characters
bool ChatHandler::HandleCharacterRenameCommand(const char* args)
{
    Player* target;
    uint64 target_guid;
    std::string target_name;
    if(!extractPlayerTarget((char*)args,&target,&target_guid,&target_name))
        return false;

    if(target)
    {
        // check online security
        if (HasLowerSecurity(target, 0))
            return false;

        PSendSysMessage(LANG_RENAME_PLAYER, GetNameLink(target).c_str());
        target->SetAtLoginFlag(AT_LOGIN_RENAME);
        CharacterDatabase.PExecute("UPDATE characters SET at_login = at_login | '1' WHERE guid = '%u'", target->GetGUIDLow());
    }
    else
    {
        // check offline security
        if (HasLowerSecurity(NULL, target_guid))
            return false;

        std::string oldNameLink = playerLink(target_name);

        PSendSysMessage(LANG_RENAME_PLAYER_GUID, oldNameLink.c_str(), GUID_LOPART(target_guid));
        CharacterDatabase.PExecute("UPDATE characters SET at_login = at_login | '1' WHERE guid = '%u'", GUID_LOPART(target_guid));
    }

    return true;
}

// customize characters
bool ChatHandler::HandleCharacterCustomizeCommand(const char* args)
{
    Player* target;
    uint64 target_guid;
    std::string target_name;
    if(!extractPlayerTarget((char*)args,&target,&target_guid,&target_name))
        return false;

    if(target)
    {
        PSendSysMessage(LANG_CUSTOMIZE_PLAYER, GetNameLink(target).c_str());
        target->SetAtLoginFlag(AT_LOGIN_CUSTOMIZE);
        CharacterDatabase.PExecute("UPDATE characters SET at_login = at_login | '8' WHERE guid = '%u'", target->GetGUIDLow());
    }
    else
    {
        std::string oldNameLink = playerLink(target_name);

        PSendSysMessage(LANG_CUSTOMIZE_PLAYER_GUID, oldNameLink.c_str(), GUID_LOPART(target_guid));
        CharacterDatabase.PExecute("UPDATE characters SET at_login = at_login | '8' WHERE guid = '%u'", GUID_LOPART(target_guid));
    }

    return true;
}

bool ChatHandler::HandleCharacterReputationCommand(const char* args)
{
    Player* target;
    if(!extractPlayerTarget((char*)args,&target))
        return false;

    LocaleConstant loc = GetSessionDbcLocale();

    FactionStateList const& targetFSL = target->GetReputationMgr().GetStateList();
    for(FactionStateList::const_iterator itr = targetFSL.begin(); itr != targetFSL.end(); ++itr)
    {
        FactionEntry const *factionEntry = sFactionStore.LookupEntry(itr->second.ID);
        char const* factionName = factionEntry ? factionEntry->name[loc] : "#Not found#";
        ReputationRank rank = target->GetReputationMgr().GetRank(factionEntry);
        std::string rankName = GetMangosString(ReputationRankStrIndex[rank]);
        std::ostringstream ss;
        if (m_session)
            ss << itr->second.ID << " - |cffffffff|Hfaction:" << itr->second.ID << "|h[" << factionName << " " << localeNames[loc] << "]|h|r";
        else
            ss << itr->second.ID << " - " << factionName << " " << localeNames[loc];

        ss << " " << rankName << " (" << target->GetReputationMgr().GetReputation(factionEntry) << ")";

        if(itr->second.Flags & FACTION_FLAG_VISIBLE)
            ss << GetMangosString(LANG_FACTION_VISIBLE);
        if(itr->second.Flags & FACTION_FLAG_AT_WAR)
            ss << GetMangosString(LANG_FACTION_ATWAR);
        if(itr->second.Flags & FACTION_FLAG_PEACE_FORCED)
            ss << GetMangosString(LANG_FACTION_PEACE_FORCED);
        if(itr->second.Flags & FACTION_FLAG_HIDDEN)
            ss << GetMangosString(LANG_FACTION_HIDDEN);
        if(itr->second.Flags & FACTION_FLAG_INVISIBLE_FORCED)
            ss << GetMangosString(LANG_FACTION_INVISIBLE_FORCED);
        if(itr->second.Flags & FACTION_FLAG_INACTIVE)
            ss << GetMangosString(LANG_FACTION_INACTIVE);

        SendSysMessage(ss.str().c_str());
    }
    return true;
}

//change standstate
bool ChatHandler::HandleModifyStandStateCommand(const char* args)
{
    if (!*args)
        return false;

    uint32 anim_id = atoi((char*)args);
    m_session->GetPlayer( )->SetUInt32Value( UNIT_NPC_EMOTESTATE , anim_id );

    return true;
}

bool ChatHandler::HandleHonorAddCommand(const char* args)
{
    if (!*args)
        return false;

    Player *target = getSelectedPlayer();
    if(!target)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(target, 0))
        return false;

    float amount = (float)atof(args);
    target->RewardHonor(NULL, 1, amount);
    return true;
}

bool ChatHandler::HandleHonorAddKillCommand(const char* /*args*/)
{
    Unit *target = getSelectedUnit();
    if(!target)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (target->GetTypeId() == TYPEID_PLAYER && HasLowerSecurity((Player*)target, 0))
        return false;

    m_session->GetPlayer()->RewardHonor(target, 1);
    return true;
}

bool ChatHandler::HandleHonorUpdateCommand(const char* /*args*/)
{
    Player *target = getSelectedPlayer();
    if(!target)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(target, 0))
        return false;

    target->UpdateHonorFields();
    return true;
}

bool ChatHandler::HandleLookupEventCommand(const char* args)
{
    if(!*args)
        return false;

    std::string namepart = args;
    std::wstring wnamepart;

    // converting string that we try to find to lower case
    if(!Utf8toWStr(namepart,wnamepart))
        return false;

    wstrToLower(wnamepart);

    uint32 counter = 0;

    GameEventMgr::GameEventDataMap const& events = sGameEventMgr.GetEventMap();
    GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr.GetActiveEventList();

    for(uint32 id = 0; id < events.size(); ++id )
    {
        GameEventData const& eventData = events[id];

        std::string descr = eventData.description;
        if(descr.empty())
            continue;

        if (Utf8FitTo(descr, wnamepart))
        {
            char const* active = activeEvents.find(id) != activeEvents.end() ? GetMangosString(LANG_ACTIVE) : "";

            if(m_session)
                PSendSysMessage(LANG_EVENT_ENTRY_LIST_CHAT,id,id,eventData.description.c_str(),active );
            else
                PSendSysMessage(LANG_EVENT_ENTRY_LIST_CONSOLE,id,eventData.description.c_str(),active );

            ++counter;
        }
    }

    if (counter==0)
        SendSysMessage(LANG_NOEVENTFOUND);

    return true;
}

bool ChatHandler::HandleEventListCommand(const char* args)
{
    uint32 counter = 0;
    bool all = false;
    std::string arg = args;
    if (arg == "all")
        all = true;

    GameEventMgr::GameEventDataMap const& events = sGameEventMgr.GetEventMap();
    GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr.GetActiveEventList();

    char const* active = GetMangosString(LANG_ACTIVE);
    char const* inactive = GetMangosString(LANG_FACTION_INACTIVE);
    char const* state = "";

    for (uint32 event_id = 0; event_id < events.size(); ++event_id)
    {
        if (activeEvents.find(event_id) == activeEvents.end())
        {
            if (!all)
                continue;
            state = inactive;
        }
        else
            state = active;

        GameEventData const& eventData = events[event_id];

        if(m_session)
            PSendSysMessage(LANG_EVENT_ENTRY_LIST_CHAT, event_id, event_id, eventData.description.c_str(), state);
        else
            PSendSysMessage(LANG_EVENT_ENTRY_LIST_CONSOLE, event_id, eventData.description.c_str(), state);

        ++counter;
    }

    if (counter==0)
        SendSysMessage(LANG_NOEVENTFOUND);

    return true;
}

bool ChatHandler::HandleEventInfoCommand(const char* args)
{
    if(!*args)
        return false;

    // id or [name] Shift-click form |color|Hgameevent:id|h[name]|h|r
    char* cId = extractKeyFromLink((char*)args,"Hgameevent");
    if(!cId)
        return false;

    uint32 event_id = atoi(cId);

    GameEventMgr::GameEventDataMap const& events = sGameEventMgr.GetEventMap();

    if(event_id >=events.size())
    {
        SendSysMessage(LANG_EVENT_NOT_EXIST);
        SetSentErrorMessage(true);
        return false;
    }

    GameEventData const& eventData = events[event_id];
    if(!eventData.isValid())
    {
        SendSysMessage(LANG_EVENT_NOT_EXIST);
        SetSentErrorMessage(true);
        return false;
    }

    GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr.GetActiveEventList();
    bool active = activeEvents.find(event_id) != activeEvents.end();
    char const* activeStr = active ? GetMangosString(LANG_ACTIVE) : "";

    std::string startTimeStr = TimeToTimestampStr(eventData.start);
    std::string endTimeStr = TimeToTimestampStr(eventData.end);

    uint32 delay = sGameEventMgr.NextCheck(event_id);
    time_t nextTime = time(NULL)+delay;
    std::string nextStr = nextTime >= eventData.start && nextTime < eventData.end ? TimeToTimestampStr(time(NULL)+delay) : "-";

    std::string occurenceStr = secsToTimeString(eventData.occurence * MINUTE);
    std::string lengthStr = secsToTimeString(eventData.length * MINUTE);

    PSendSysMessage(LANG_EVENT_INFO,event_id,eventData.description.c_str(),activeStr,
        startTimeStr.c_str(),endTimeStr.c_str(),occurenceStr.c_str(),lengthStr.c_str(),
        nextStr.c_str());
    return true;
}

bool ChatHandler::HandleEventStartCommand(const char* args)
{
    if(!*args)
        return false;

    // id or [name] Shift-click form |color|Hgameevent:id|h[name]|h|r
    char* cId = extractKeyFromLink((char*)args,"Hgameevent");
    if(!cId)
        return false;

    int32 event_id = atoi(cId);

    GameEventMgr::GameEventDataMap const& events = sGameEventMgr.GetEventMap();

    if(event_id < 1 || event_id >=(int32)events.size())
    {
        SendSysMessage(LANG_EVENT_NOT_EXIST);
        SetSentErrorMessage(true);
        return false;
    }

    GameEventData const& eventData = events[event_id];
    if(!eventData.isValid())
    {
        SendSysMessage(LANG_EVENT_NOT_EXIST);
        SetSentErrorMessage(true);
        return false;
    }

    GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr.GetActiveEventList();
    if(activeEvents.find(event_id) != activeEvents.end())
    {
        PSendSysMessage(LANG_EVENT_ALREADY_ACTIVE,event_id);
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage(LANG_EVENT_STARTED, event_id, eventData.description.c_str());
    sGameEventMgr.StartEvent(event_id,true);
    return true;
}

bool ChatHandler::HandleEventStopCommand(const char* args)
{
    if(!*args)
        return false;

    // id or [name] Shift-click form |color|Hgameevent:id|h[name]|h|r
    char* cId = extractKeyFromLink((char*)args,"Hgameevent");
    if(!cId)
        return false;

    int32 event_id = atoi(cId);

    GameEventMgr::GameEventDataMap const& events = sGameEventMgr.GetEventMap();

    if(event_id < 1 || event_id >=(int32)events.size())
    {
        SendSysMessage(LANG_EVENT_NOT_EXIST);
        SetSentErrorMessage(true);
        return false;
    }

    GameEventData const& eventData = events[event_id];
    if(!eventData.isValid())
    {
        SendSysMessage(LANG_EVENT_NOT_EXIST);
        SetSentErrorMessage(true);
        return false;
    }

    GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr.GetActiveEventList();

    if(activeEvents.find(event_id) == activeEvents.end())
    {
        PSendSysMessage(LANG_EVENT_NOT_ACTIVE,event_id);
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage(LANG_EVENT_STOPPED, event_id, eventData.description.c_str());
    sGameEventMgr.StopEvent(event_id,true);
    return true;
}

bool ChatHandler::HandleCombatStopCommand(const char* args)
{
    Player* target;
    if(!extractPlayerTarget((char*)args,&target))
        return false;

    // check online security
    if (HasLowerSecurity(target, 0))
        return false;

    target->CombatStop();
    target->getHostileRefManager().deleteReferences();
    return true;
}

void ChatHandler::HandleLearnSkillRecipesHelper(Player* player,uint32 skill_id)
{
    uint32 classmask = player->getClassMask();

    for (uint32 j = 0; j < sSkillLineAbilityStore.GetNumRows(); ++j)
    {
        SkillLineAbilityEntry const *skillLine = sSkillLineAbilityStore.LookupEntry(j);
        if (!skillLine)
            continue;

        // wrong skill
        if( skillLine->skillId != skill_id)
            continue;

        // not high rank
        if(skillLine->forward_spellid )
            continue;

        // skip racial skills
        if (skillLine->racemask != 0)
            continue;

        // skip wrong class skills
        if( skillLine->classmask && (skillLine->classmask & classmask) == 0)
            continue;

        SpellEntry const* spellInfo = sSpellStore.LookupEntry(skillLine->spellId);
        if(!spellInfo || !SpellMgr::IsSpellValid(spellInfo,player,false))
            continue;

        player->learnSpell(skillLine->spellId,false);
    }
}

bool ChatHandler::HandleLearnAllCraftsCommand(const char* /*args*/)
{
    for (uint32 i = 0; i < sSkillLineStore.GetNumRows(); ++i)
    {
        SkillLineEntry const *skillInfo = sSkillLineStore.LookupEntry(i);
        if( !skillInfo )
            continue;

        if ((skillInfo->categoryId == SKILL_CATEGORY_PROFESSION || skillInfo->categoryId == SKILL_CATEGORY_SECONDARY) &&
            skillInfo->canLink)                             // only prof. with recipes have
        {
            HandleLearnSkillRecipesHelper(m_session->GetPlayer(),skillInfo->id);
        }
    }

    SendSysMessage(LANG_COMMAND_LEARN_ALL_CRAFT);
    return true;
}

bool ChatHandler::HandleLearnAllRecipesCommand(const char* args)
{
    //  Learns all recipes of specified profession and sets skill to max
    //  Example: .learn all_recipes enchanting

    Player* target = getSelectedPlayer();
    if( !target )
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        return false;
    }

    if(!*args)
        return false;

    std::wstring wnamepart;

    if(!Utf8toWStr(args,wnamepart))
        return false;

    // converting string that we try to find to lower case
    wstrToLower( wnamepart );

    std::string name;

    SkillLineEntry const *targetSkillInfo = NULL;
    for (uint32 i = 1; i < sSkillLineStore.GetNumRows(); ++i)
    {
        SkillLineEntry const *skillInfo = sSkillLineStore.LookupEntry(i);
        if (!skillInfo)
            continue;

        if ((skillInfo->categoryId != SKILL_CATEGORY_PROFESSION &&
            skillInfo->categoryId != SKILL_CATEGORY_SECONDARY) ||
            !skillInfo->canLink)                            // only prof with recipes have set
            continue;

        int loc = GetSessionDbcLocale();
        name = skillInfo->name[loc];
        if(name.empty())
            continue;

        if (!Utf8FitTo(name, wnamepart))
        {
            loc = 0;
            for(; loc < MAX_LOCALE; ++loc)
            {
                if(loc==GetSessionDbcLocale())
                    continue;

                name = skillInfo->name[loc];
                if(name.empty())
                    continue;

                if (Utf8FitTo(name, wnamepart))
                    break;
            }
        }

        if(loc < MAX_LOCALE)
        {
            targetSkillInfo = skillInfo;
            break;
        }
    }

    if(!targetSkillInfo)
        return false;

    HandleLearnSkillRecipesHelper(target,targetSkillInfo->id);

    uint16 maxLevel = target->GetPureMaxSkillValue(targetSkillInfo->id);
    target->SetSkill(targetSkillInfo->id, maxLevel, maxLevel);
    PSendSysMessage(LANG_COMMAND_LEARN_ALL_RECIPES, name.c_str());
    return true;
}

bool ChatHandler::HandleLookupPlayerIpCommand(const char* args)
{

    if (!*args)
        return false;

    std::string ip = strtok ((char*)args, " ");
    char* limit_str = strtok (NULL, " ");
    int32 limit = limit_str ? atoi (limit_str) : -1;

    loginDatabase.escape_string (ip);

    QueryResult* result = loginDatabase.PQuery ("SELECT id,username FROM account WHERE last_ip = '%s'", ip.c_str ());

    return LookupPlayerSearchCommand (result,limit);
}

bool ChatHandler::HandleLookupPlayerAccountCommand(const char* args)
{
    if (!*args)
        return false;

    std::string account = strtok ((char*)args, " ");
    char* limit_str = strtok (NULL, " ");
    int32 limit = limit_str ? atoi (limit_str) : -1;

    if (!AccountMgr::normalizeString (account))
        return false;

    loginDatabase.escape_string (account);

    QueryResult* result = loginDatabase.PQuery ("SELECT id,username FROM account WHERE username = '%s'", account.c_str ());

    return LookupPlayerSearchCommand (result,limit);
}

bool ChatHandler::HandleLookupPlayerEmailCommand(const char* args)
{

    if (!*args)
        return false;

    std::string email = strtok ((char*)args, " ");
    char* limit_str = strtok (NULL, " ");
    int32 limit = limit_str ? atoi (limit_str) : -1;

    loginDatabase.escape_string (email);

    QueryResult* result = loginDatabase.PQuery ("SELECT id,username FROM account WHERE email = '%s'", email.c_str ());

    return LookupPlayerSearchCommand (result,limit);
}

bool ChatHandler::LookupPlayerSearchCommand(QueryResult* result, int32 limit)
{
    if(!result)
    {
        PSendSysMessage(LANG_NO_PLAYERS_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    int i =0;
    do
    {
        Field* fields = result->Fetch();
        uint32 acc_id = fields[0].GetUInt32();
        std::string acc_name = fields[1].GetCppString();

        QueryResult* chars = CharacterDatabase.PQuery("SELECT guid,name FROM characters WHERE account = '%u'", acc_id);
        if(chars)
        {
            PSendSysMessage(LANG_LOOKUP_PLAYER_ACCOUNT,acc_name.c_str(),acc_id);

            uint64 guid = 0;
            std::string name;

            do
            {
                Field* charfields = chars->Fetch();
                guid = charfields[0].GetUInt64();
                name = charfields[1].GetCppString();

                PSendSysMessage(LANG_LOOKUP_PLAYER_CHARACTER,name.c_str(),guid);
                ++i;

            } while( chars->NextRow() && ( limit == -1 || i < limit ) );

            delete chars;
        }
    } while(result->NextRow());

    delete result;

    if(i==0)                                                // empty accounts only
    {
        PSendSysMessage(LANG_NO_PLAYERS_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    return true;
}

/// Triggering corpses expire check in world
bool ChatHandler::HandleServerCorpsesCommand(const char* /*args*/)
{
    CorpsesErase();
    return true;
}

bool ChatHandler::HandleRepairitemsCommand(const char* args)
{
    Player* target;
    if(!extractPlayerTarget((char*)args,&target))
        return false;

    // check online security
    if (HasLowerSecurity(target, 0))
        return false;

    // Repair items
    target->DurabilityRepairAll(false, 0, false);

    PSendSysMessage(LANG_YOU_REPAIR_ITEMS, GetNameLink(target).c_str());
    if(needReportToTarget(target))
        ChatHandler(target).PSendSysMessage(LANG_YOUR_ITEMS_REPAIRED, GetNameLink().c_str());
    return true;
}

bool ChatHandler::HandleWaterwalkCommand(const char* args)
{
    if(!*args)
        return false;

    Player *player = getSelectedPlayer();

    if(!player)
    {
        PSendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(player, 0))
        return false;

    if (strncmp(args, "on", 3) == 0)
        player->SetMovement(MOVE_WATER_WALK);               // ON
    else if (strncmp(args, "off", 4) == 0)
        player->SetMovement(MOVE_LAND_WALK);                // OFF
    else
    {
        SendSysMessage(LANG_USE_BOL);
        return false;
    }

    PSendSysMessage(LANG_YOU_SET_WATERWALK, args, GetNameLink(player).c_str());
    if(needReportToTarget(player))
        ChatHandler(player).PSendSysMessage(LANG_YOUR_WATERWALK_SET, args, GetNameLink().c_str());
    return true;
}

bool ChatHandler::HandleLookupTitleCommand(const char* args)
{
    if(!*args)
        return false;

    // can be NULL in console call
    Player* target = getSelectedPlayer();

    // title name have single string arg for player name
    char const* targetName = target ? target->GetName() : "NAME";

    std::string namepart = args;
    std::wstring wnamepart;

    if(!Utf8toWStr(namepart,wnamepart))
        return false;

    // converting string that we try to find to lower case
    wstrToLower( wnamepart );

    uint32 counter = 0;                                     // Counter for figure out that we found smth.

    // Search in CharTitles.dbc
    for (uint32 id = 0; id < sCharTitlesStore.GetNumRows(); id++)
    {
        CharTitlesEntry const *titleInfo = sCharTitlesStore.LookupEntry(id);
        if(titleInfo)
        {
            int loc = GetSessionDbcLocale();
            std::string name = titleInfo->name[loc];
            if(name.empty())
                continue;

            if (!Utf8FitTo(name, wnamepart))
            {
                loc = 0;
                for(; loc < MAX_LOCALE; ++loc)
                {
                    if(loc==GetSessionDbcLocale())
                        continue;

                    name = titleInfo->name[loc];
                    if(name.empty())
                        continue;

                    if (Utf8FitTo(name, wnamepart))
                        break;
                }
            }

            if(loc < MAX_LOCALE)
            {
                char const* knownStr = target && target->HasTitle(titleInfo) ? GetMangosString(LANG_KNOWN) : "";

                char const* activeStr = target && target->GetUInt32Value(PLAYER_CHOSEN_TITLE)==titleInfo->bit_index
                    ? GetMangosString(LANG_ACTIVE)
                    : "";

                char titleNameStr[80];
                snprintf(titleNameStr,80,name.c_str(),targetName);

                // send title in "id (idx:idx) - [namedlink locale]" format
                if (m_session)
                    PSendSysMessage(LANG_TITLE_LIST_CHAT,id,titleInfo->bit_index,id,titleNameStr,localeNames[loc],knownStr,activeStr);
                else
                    PSendSysMessage(LANG_TITLE_LIST_CONSOLE,id,titleInfo->bit_index,titleNameStr,localeNames[loc],knownStr,activeStr);

                ++counter;
            }
        }
    }
    if (counter == 0)                                       // if counter == 0 then we found nth
        SendSysMessage(LANG_COMMAND_NOTITLEFOUND);
    return true;
}

bool ChatHandler::HandleTitlesAddCommand(const char* args)
{
    // number or [name] Shift-click form |color|Htitle:title_id|h[name]|h|r
    char* id_p = extractKeyFromLink((char*)args,"Htitle");
    if(!id_p)
        return false;

    int32 id = atoi(id_p);
    if (id <= 0)
    {
        PSendSysMessage(LANG_INVALID_TITLE_ID, id);
        SetSentErrorMessage(true);
        return false;
    }

    Player * target = getSelectedPlayer();
    if(!target)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(target, 0))
        return false;

    CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(id);
    if(!titleInfo)
    {
        PSendSysMessage(LANG_INVALID_TITLE_ID, id);
        SetSentErrorMessage(true);
        return false;
    }

    std::string tNameLink = GetNameLink(target);

    char const* targetName = target->GetName();
    char titleNameStr[80];
    snprintf(titleNameStr,80,titleInfo->name[GetSessionDbcLocale()],targetName);

    target->SetTitle(titleInfo);
    PSendSysMessage(LANG_TITLE_ADD_RES, id, titleNameStr, tNameLink.c_str());

    return true;
}

bool ChatHandler::HandleTitlesRemoveCommand(const char* args)
{
    // number or [name] Shift-click form |color|Htitle:title_id|h[name]|h|r
    char* id_p = extractKeyFromLink((char*)args,"Htitle");
    if(!id_p)
        return false;

    int32 id = atoi(id_p);
    if (id <= 0)
    {
        PSendSysMessage(LANG_INVALID_TITLE_ID, id);
        SetSentErrorMessage(true);
        return false;
    }

    Player * target = getSelectedPlayer();
    if(!target)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(target, 0))
        return false;

    CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(id);
    if(!titleInfo)
    {
        PSendSysMessage(LANG_INVALID_TITLE_ID, id);
        SetSentErrorMessage(true);
        return false;
    }

    target->SetTitle(titleInfo,true);

    std::string tNameLink = GetNameLink(target);

    char const* targetName = target->GetName();
    char titleNameStr[80];
    snprintf(titleNameStr,80,titleInfo->name[GetSessionDbcLocale()],targetName);

    PSendSysMessage(LANG_TITLE_REMOVE_RES, id, titleNameStr, tNameLink.c_str());

    if (!target->HasTitle(target->GetInt32Value(PLAYER_CHOSEN_TITLE)))
    {
        target->SetUInt32Value(PLAYER_CHOSEN_TITLE,0);
        PSendSysMessage(LANG_CURRENT_TITLE_RESET, tNameLink.c_str());
    }

    return true;
}

//Edit Player KnownTitles
bool ChatHandler::HandleTitlesSetMaskCommand(const char* args)
{
    if(!*args)
        return false;

    uint64 titles = 0;

    sscanf((char*)args, UI64FMTD, &titles);

    Player *target = getSelectedPlayer();
    if (!target)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(target, 0))
        return false;

    uint64 titles2 = titles;

    for(uint32 i = 1; i < sCharTitlesStore.GetNumRows(); ++i)
        if(CharTitlesEntry const* tEntry = sCharTitlesStore.LookupEntry(i))
            titles2 &= ~(uint64(1) << tEntry->bit_index);

    titles &= ~titles2;                                     // remove not existed titles

    target->SetUInt64Value(PLAYER__FIELD_KNOWN_TITLES, titles);
    SendSysMessage(LANG_DONE);

    if (!target->HasTitle(target->GetInt32Value(PLAYER_CHOSEN_TITLE)))
    {
        target->SetUInt32Value(PLAYER_CHOSEN_TITLE,0);
        PSendSysMessage(LANG_CURRENT_TITLE_RESET,GetNameLink(target).c_str());
    }

    return true;
}

bool ChatHandler::HandleCharacterTitlesCommand(const char* args)
{
    Player* target;
    if(!extractPlayerTarget((char*)args,&target))
        return false;

    LocaleConstant loc = GetSessionDbcLocale();
    char const* targetName = target->GetName();
    char const* knownStr = GetMangosString(LANG_KNOWN);

    // Search in CharTitles.dbc
    for (uint32 id = 0; id < sCharTitlesStore.GetNumRows(); id++)
    {
        CharTitlesEntry const *titleInfo = sCharTitlesStore.LookupEntry(id);
        if (titleInfo && target->HasTitle(titleInfo))
        {
            std::string name = titleInfo->name[loc];
            if(name.empty())
                continue;

            char const* activeStr = target && target->GetUInt32Value(PLAYER_CHOSEN_TITLE)==titleInfo->bit_index
                ? GetMangosString(LANG_ACTIVE)
                : "";

            char titleNameStr[80];
            snprintf(titleNameStr,80,name.c_str(),targetName);

            // send title in "id (idx:idx) - [namedlink locale]" format
            if (m_session)
                PSendSysMessage(LANG_TITLE_LIST_CHAT,id,titleInfo->bit_index,id,titleNameStr,localeNames[loc],knownStr,activeStr);
            else
                PSendSysMessage(LANG_TITLE_LIST_CONSOLE,id,titleInfo->bit_index,name.c_str(),localeNames[loc],knownStr,activeStr);
        }
    }
    return true;
}

bool ChatHandler::HandleTitlesCurrentCommand(const char* args)
{
    // number or [name] Shift-click form |color|Htitle:title_id|h[name]|h|r
    char* id_p = extractKeyFromLink((char*)args,"Htitle");
    if(!id_p)
        return false;

    int32 id = atoi(id_p);
    if (id <= 0)
    {
        PSendSysMessage(LANG_INVALID_TITLE_ID, id);
        SetSentErrorMessage(true);
        return false;
    }

    Player * target = getSelectedPlayer();
    if(!target)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(target, 0))
        return false;

    CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(id);
    if(!titleInfo)
    {
        PSendSysMessage(LANG_INVALID_TITLE_ID, id);
        SetSentErrorMessage(true);
        return false;
    }

    std::string tNameLink = GetNameLink(target);

    target->SetTitle(titleInfo);                            // to be sure that title now known
    target->SetUInt32Value(PLAYER_CHOSEN_TITLE,titleInfo->bit_index);

    PSendSysMessage(LANG_TITLE_CURRENT_RES, id, titleInfo->name[GetSessionDbcLocale()], tNameLink.c_str());

    return true;
}
