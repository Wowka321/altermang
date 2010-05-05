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
#include "WorldPacket.h"
#include "WorldSession.h"
#include "World.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Opcodes.h"
#include "Chat.h"
#include "Log.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "Language.h"
#include "CellImpl.h"
#include "InstanceSaveMgr.h"
#include "Mail.h"
#include "Util.h"
#ifdef _DEBUG_VMAPS
#include "VMapFactory.h"
#endif

//-----------------------Npc Commands-----------------------
bool ChatHandler::HandleNpcSayCommand(const char* args)
{
    if(!*args)
        return false;

    Creature* pCreature = getSelectedCreature();
    if(!pCreature)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    pCreature->MonsterSay(args, LANG_UNIVERSAL, 0);

    return true;
}

bool ChatHandler::HandleNpcYellCommand(const char* args)
{
    if(!*args)
        return false;

    Creature* pCreature = getSelectedCreature();
    if(!pCreature)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    pCreature->MonsterYell(args, LANG_UNIVERSAL, 0);

    return true;
}

//show text emote by creature in chat
bool ChatHandler::HandleNpcTextEmoteCommand(const char* args)
{
    if(!*args)
        return false;

    Creature* pCreature = getSelectedCreature();

    if(!pCreature)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    pCreature->MonsterTextEmote(args, 0);

    return true;
}

// make npc whisper to player
bool ChatHandler::HandleNpcWhisperCommand(const char* args)
{
    if(!*args)
        return false;

    char* receiver_str = strtok((char*)args, " ");
    char* text = strtok(NULL, "");

    uint64 guid = m_session->GetPlayer()->GetSelection();
    Creature* pCreature = m_session->GetPlayer()->GetMap()->GetCreature(guid);

    if(!pCreature || !receiver_str || !text)
    {
        return false;
    }

    uint64 receiver_guid= atol(receiver_str);

    // check online security
    if (HasLowerSecurity(sObjectMgr.GetPlayer(receiver_guid), 0))
        return false;

    pCreature->MonsterWhisper(text,receiver_guid);

    return true;
}
//----------------------------------------------------------

// global announce
bool ChatHandler::HandleAnnounceCommand(const char* args)
{
    if(!*args)
        return false;

    sWorld.SendWorldText(LANG_SYSTEMMESSAGE,args);
    return true;
}

// Nameannounce
bool ChatHandler::HandleNameAnnounceCommand(const char* args)
{
    int32 strid = 0;

    if(!*args)
        return false;

    switch(m_session->GetSecurity())
    {
      case SEC_MODERATOR:
        strid = LANG_SYSTEMMESSAGE_MODERATOR;
        break;
      case SEC_GAMEMASTER:
        strid = LANG_SYSTEMMESSAGE_GAMEMASTER;
        break;
      case SEC_ADMINISTRATOR:
        strid = LANG_SYSTEMMESSAGE_ADMINISTRATOR;
        break;
      default:
        return false;
    }

    sWorld.SendWorldText(strid, m_session->GetPlayerName(), args);

    return true;
}

//notification player at the screen
bool ChatHandler::HandleNotifyCommand(const char* args)
{
    if(!*args)
        return false;

    std::string str = GetMangosString(LANG_GLOBAL_NOTIFY);
    str += args;

    WorldPacket data(SMSG_NOTIFICATION, (str.size()+1));
    data << str;
    sWorld.SendGlobalMessage(&data);

    return true;
}

//Enable\Dissable GM Mode
bool ChatHandler::HandleGMCommand(const char* args)
{
    if(!*args)
    {
        if(m_session->GetPlayer()->isGameMaster())
            m_session->SendNotification(LANG_GM_ON);
        else
            m_session->SendNotification(LANG_GM_OFF);
        return true;
    }

    std::string argstr = (char*)args;

    if (argstr == "on")
    {
        m_session->GetPlayer()->SetGameMaster(true);
        m_session->SendNotification(LANG_GM_ON);
        #ifdef _DEBUG_VMAPS
        VMAP::IVMapManager *vMapManager = VMAP::VMapFactory::createOrGetVMapManager();
        vMapManager->processCommand("stoplog");
        #endif
        return true;
    }

    if (argstr == "off")
    {
        m_session->GetPlayer()->SetGameMaster(false);
        m_session->SendNotification(LANG_GM_OFF);
        #ifdef _DEBUG_VMAPS
        VMAP::IVMapManager *vMapManager = VMAP::VMapFactory::createOrGetVMapManager();
        vMapManager->processCommand("startlog");
        #endif
        return true;
    }

    SendSysMessage(LANG_USE_BOL);
    SetSentErrorMessage(true);
    return false;
}

// Enables or disables hiding of the staff badge
bool ChatHandler::HandleGMChatCommand(const char* args)
{
    if(!*args)
    {
        if(m_session->GetPlayer()->isGMChat())
            m_session->SendNotification(LANG_GM_CHAT_ON);
        else
            m_session->SendNotification(LANG_GM_CHAT_OFF);
        return true;
    }

    std::string argstr = (char*)args;

    if (argstr == "on")
    {
        m_session->GetPlayer()->SetGMChat(true);
        m_session->SendNotification(LANG_GM_CHAT_ON);
        return true;
    }

    if (argstr == "off")
    {
        m_session->GetPlayer()->SetGMChat(false);
        m_session->SendNotification(LANG_GM_CHAT_OFF);
        return true;
    }

    SendSysMessage(LANG_USE_BOL);
    SetSentErrorMessage(true);
    return false;
}

//Enable\Dissable Invisible mode
bool ChatHandler::HandleGMVisibleCommand(const char* args)
{
    if (!*args)
    {
        PSendSysMessage(LANG_YOU_ARE, m_session->GetPlayer()->isGMVisible() ?  GetMangosString(LANG_VISIBLE) : GetMangosString(LANG_INVISIBLE));
        return true;
    }

    std::string argstr = (char*)args;

    if (argstr == "on")
    {
        m_session->GetPlayer()->SetGMVisible(true);
        m_session->SendNotification(LANG_INVISIBLE_VISIBLE);
        return true;
    }

    if (argstr == "off")
    {
        m_session->SendNotification(LANG_INVISIBLE_INVISIBLE);
        m_session->GetPlayer()->SetGMVisible(false);
        return true;
    }

    SendSysMessage(LANG_USE_BOL);
    SetSentErrorMessage(true);
    return false;
}



bool ChatHandler::HandleGPSCommand(const char* args)
{
    WorldObject *obj = NULL;
    if (*args)
    {
        uint64 guid = extractGuidFromLink((char*)args);
        if(guid)
            obj = (WorldObject*)m_session->GetPlayer()->GetObjectByTypeMask(guid, TYPEMASK_CREATURE_OR_GAMEOBJECT);

        if(!obj)
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }
    }
    else
    {
        obj = getSelectedUnit();

        if(!obj)
        {
            SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
            SetSentErrorMessage(true);
            return false;
        }
    }
    CellPair cell_val = MaNGOS::ComputeCellPair(obj->GetPositionX(), obj->GetPositionY());
    Cell cell(cell_val);

    uint32 zone_id, area_id;
    obj->GetZoneAndAreaId(zone_id,area_id);

    MapEntry const* mapEntry = sMapStore.LookupEntry(obj->GetMapId());
    AreaTableEntry const* zoneEntry = GetAreaEntryByAreaID(zone_id);
    AreaTableEntry const* areaEntry = GetAreaEntryByAreaID(area_id);

    float zone_x = obj->GetPositionX();
    float zone_y = obj->GetPositionY();

    if (!Map2ZoneCoordinates(zone_x, zone_y, zone_id))
    {
        zone_x = 0;
        zone_y = 0;
    }

    Map const *map = obj->GetMap();
    float ground_z = map->GetHeight(obj->GetPositionX(), obj->GetPositionY(), MAX_HEIGHT);
    float floor_z = map->GetHeight(obj->GetPositionX(), obj->GetPositionY(), obj->GetPositionZ());

    GridPair p = MaNGOS::ComputeGridPair(obj->GetPositionX(), obj->GetPositionY());

    int gx=63-p.x_coord;
    int gy=63-p.y_coord;

    uint32 have_map = Map::ExistMap(obj->GetMapId(),gx,gy) ? 1 : 0;
    uint32 have_vmap = Map::ExistVMap(obj->GetMapId(),gx,gy) ? 1 : 0;

    PSendSysMessage(LANG_MAP_POSITION,
        obj->GetMapId(), (mapEntry ? mapEntry->name[GetSessionDbcLocale()] : "<unknown>" ),
        zone_id, (zoneEntry ? zoneEntry->area_name[GetSessionDbcLocale()] : "<unknown>" ),
        area_id, (areaEntry ? areaEntry->area_name[GetSessionDbcLocale()] : "<unknown>" ),
        obj->GetPhaseMask(),
        obj->GetPositionX(), obj->GetPositionY(), obj->GetPositionZ(), obj->GetOrientation(),
        cell.GridX(), cell.GridY(), cell.CellX(), cell.CellY(), obj->GetInstanceId(),
        zone_x, zone_y, ground_z, floor_z, have_map, have_vmap );

    DEBUG_LOG("Player %s GPS call for %s '%s' (%s: %u):",
        m_session ? GetNameLink().c_str() : GetMangosString(LANG_CONSOLE_COMMAND),
        (obj->GetTypeId() == TYPEID_PLAYER ? "player" : "creature"), obj->GetName(),
        (obj->GetTypeId() == TYPEID_PLAYER ? "GUID" : "Entry"), (obj->GetTypeId() == TYPEID_PLAYER ? obj->GetGUIDLow(): obj->GetEntry()) );

    DEBUG_LOG(GetMangosString(LANG_MAP_POSITION),
        obj->GetMapId(), (mapEntry ? mapEntry->name[sWorld.GetDefaultDbcLocale()] : "<unknown>" ),
        zone_id, (zoneEntry ? zoneEntry->area_name[sWorld.GetDefaultDbcLocale()] : "<unknown>" ),
        area_id, (areaEntry ? areaEntry->area_name[sWorld.GetDefaultDbcLocale()] : "<unknown>" ),
        obj->GetPhaseMask(),
        obj->GetPositionX(), obj->GetPositionY(), obj->GetPositionZ(), obj->GetOrientation(),
        cell.GridX(), cell.GridY(), cell.CellX(), cell.CellY(), obj->GetInstanceId(),
        zone_x, zone_y, ground_z, floor_z, have_map, have_vmap );

    LiquidData liquid_status;
    ZLiquidStatus res = map->getLiquidStatus(obj->GetPositionX(), obj->GetPositionY(), obj->GetPositionZ(), MAP_ALL_LIQUIDS, &liquid_status);
    if (res)
    {
        PSendSysMessage(LANG_LIQUID_STATUS, liquid_status.level, liquid_status.depth_level, liquid_status.type, res);
    }
    return true;
}

//Summon Player
bool ChatHandler::HandleNamegoCommand(const char* args)
{
    Player* target;
    uint64 target_guid;
    std::string target_name;
    if (!extractPlayerTarget((char*)args,&target,&target_guid,&target_name))
        return false;

    Player* _player = m_session->GetPlayer();
    if (target == _player || target_guid == _player->GetGUID())
    {
        PSendSysMessage(LANG_CANT_TELEPORT_SELF);
        SetSentErrorMessage(true);
        return false;
    }

    if (target)
    {
        std::string nameLink = playerLink(target_name);
        // check online security
        if (HasLowerSecurity(target, 0))
            return false;

        if (target->IsBeingTeleported())
        {
            PSendSysMessage(LANG_IS_TELEPORTED, nameLink.c_str());
            SetSentErrorMessage(true);
            return false;
        }

        Map* pMap = m_session->GetPlayer()->GetMap();

        if (pMap->IsBattleGroundOrArena())
        {
            // only allow if gm mode is on
            if (!target->isGameMaster())
            {
                PSendSysMessage(LANG_CANNOT_GO_TO_BG_GM,nameLink.c_str());
                SetSentErrorMessage(true);
                return false;
            }
            // if both players are in different bgs
            else if (target->GetBattleGroundId() && m_session->GetPlayer()->GetBattleGroundId() != target->GetBattleGroundId())
            {
                PSendSysMessage(LANG_CANNOT_GO_TO_BG_FROM_BG,nameLink.c_str());
                SetSentErrorMessage(true);
                return false;
            }
            // all's well, set bg id
            // when porting out from the bg, it will be reset to 0
            target->SetBattleGroundId(m_session->GetPlayer()->GetBattleGroundId(), m_session->GetPlayer()->GetBattleGroundTypeId());
            // remember current position as entry point for return at bg end teleportation
            if (!target->GetMap()->IsBattleGroundOrArena())
                target->SetBattleGroundEntryPoint();
        }
        else if (pMap->IsDungeon())
        {
            Map* cMap = target->GetMap();
            if (cMap->Instanceable() && cMap->GetInstanceId() != pMap->GetInstanceId())
            {
                // cannot summon from instance to instance
                PSendSysMessage(LANG_CANNOT_SUMMON_TO_INST,nameLink.c_str());
                SetSentErrorMessage(true);
                return false;
            }

            // we are in instance, and can summon only player in our group with us as lead
            if (!m_session->GetPlayer()->GetGroup() || !target->GetGroup() ||
                (target->GetGroup()->GetLeaderGUID() != m_session->GetPlayer()->GetGUID()) ||
                (m_session->GetPlayer()->GetGroup()->GetLeaderGUID() != m_session->GetPlayer()->GetGUID()))
                // the last check is a bit excessive, but let it be, just in case
            {
                PSendSysMessage(LANG_CANNOT_SUMMON_TO_INST,nameLink.c_str());
                SetSentErrorMessage(true);
                return false;
            }
        }

        PSendSysMessage(LANG_SUMMONING, nameLink.c_str(),"");
        if (needReportToTarget(target))
            ChatHandler(target).PSendSysMessage(LANG_SUMMONED_BY, playerLink(_player->GetName()).c_str());

        // stop flight if need
        if (target->isInFlight())
        {
            target->GetMotionMaster()->MovementExpired();
            target->m_taxi.ClearTaxiDestinations();
        }
        // save only in non-flight case
        else
            target->SaveRecallPosition();

        // before GM
        float x,y,z;
        m_session->GetPlayer()->GetClosePoint(x,y,z,target->GetObjectSize());
        target->TeleportTo(m_session->GetPlayer()->GetMapId(),x,y,z,target->GetOrientation());
    }
    else
    {
        // check offline security
        if (HasLowerSecurity(NULL, target_guid))
            return false;

        std::string nameLink = playerLink(target_name);

        PSendSysMessage(LANG_SUMMONING, nameLink.c_str(),GetMangosString(LANG_OFFLINE));

        // in point where GM stay
        Player::SavePositionInDB(m_session->GetPlayer()->GetMapId(),
            m_session->GetPlayer()->GetPositionX(),
            m_session->GetPlayer()->GetPositionY(),
            m_session->GetPlayer()->GetPositionZ(),
            m_session->GetPlayer()->GetOrientation(),
            m_session->GetPlayer()->GetZoneId(),
            target_guid);
    }

    return true;
}

//Teleport to Player
bool ChatHandler::HandleGonameCommand(const char* args)
{
    Player* target;
    uint64 target_guid;
    std::string target_name;
    if (!extractPlayerTarget((char*)args,&target,&target_guid,&target_name))
        return false;

    Player* _player = m_session->GetPlayer();
    if (target == _player || target_guid == _player->GetGUID())
    {
        SendSysMessage(LANG_CANT_TELEPORT_SELF);
        SetSentErrorMessage(true);
        return false;
    }


    if (target)
    {
        // check online security
        if (HasLowerSecurity(target, 0))
            return false;

        std::string chrNameLink = playerLink(target_name);

        Map* cMap = target->GetMap();
        if (cMap->IsBattleGroundOrArena())
        {
            // only allow if gm mode is on
            if (!_player->isGameMaster())
            {
                PSendSysMessage(LANG_CANNOT_GO_TO_BG_GM,chrNameLink.c_str());
                SetSentErrorMessage(true);
                return false;
            }
            // if both players are in different bgs
            else if (_player->GetBattleGroundId() && _player->GetBattleGroundId() != target->GetBattleGroundId())
            {
                PSendSysMessage(LANG_CANNOT_GO_TO_BG_FROM_BG,chrNameLink.c_str());
                SetSentErrorMessage(true);
                return false;
            }
            // all's well, set bg id
            // when porting out from the bg, it will be reset to 0
            _player->SetBattleGroundId(target->GetBattleGroundId(), target->GetBattleGroundTypeId());
            // remember current position as entry point for return at bg end teleportation
            if (!_player->GetMap()->IsBattleGroundOrArena())
                _player->SetBattleGroundEntryPoint();
        }
        else if(cMap->IsDungeon())
        {
            // we have to go to instance, and can go to player only if:
            //   1) we are in his group (either as leader or as member)
            //   2) we are not bound to any group and have GM mode on
            if (_player->GetGroup())
            {
                // we are in group, we can go only if we are in the player group
                if (_player->GetGroup() != target->GetGroup())
                {
                    PSendSysMessage(LANG_CANNOT_GO_TO_INST_PARTY,chrNameLink.c_str());
                    SetSentErrorMessage(true);
                    return false;
                }
            }
            else
            {
                // we are not in group, let's verify our GM mode
                if (!_player->isGameMaster())
                {
                    PSendSysMessage(LANG_CANNOT_GO_TO_INST_GM,chrNameLink.c_str());
                    SetSentErrorMessage(true);
                    return false;
                }
            }

            // if the player or the player's group is bound to another instance
            // the player will not be bound to another one
            InstancePlayerBind *pBind = _player->GetBoundInstance(target->GetMapId(), target->GetDifficulty(cMap->IsRaid()));
            if (!pBind)
            {
                Group *group = _player->GetGroup();
                // if no bind exists, create a solo bind
                InstanceGroupBind *gBind = group ? group->GetBoundInstance(target) : NULL;
                // if no bind exists, create a solo bind
                if (!gBind)
                    if (InstanceSave *save = sInstanceSaveMgr.GetInstanceSave(target->GetInstanceId()))
                        _player->BindToInstance(save, !save->CanReset());
            }

            if(cMap->IsRaid())
                _player->SetRaidDifficulty(target->GetRaidDifficulty());
            else
                _player->SetDungeonDifficulty(target->GetDungeonDifficulty());
        }

        PSendSysMessage(LANG_APPEARING_AT, chrNameLink.c_str());
        if (needReportToTarget(target))
            ChatHandler(target).PSendSysMessage(LANG_APPEARING_TO, GetNameLink().c_str());

        // stop flight if need
        if (_player->isInFlight())
        {
            _player->GetMotionMaster()->MovementExpired();
            _player->m_taxi.ClearTaxiDestinations();
        }
        // save only in non-flight case
        else
            _player->SaveRecallPosition();

        // to point to see at target with same orientation
        float x,y,z;
        target->GetContactPoint(_player,x,y,z);

        _player->TeleportTo(target->GetMapId(), x, y, z, _player->GetAngle(target), TELE_TO_GM_MODE);
    }
    else
    {
        // check offline security
        if (HasLowerSecurity(NULL, target_guid))
            return false;

        std::string nameLink = playerLink(target_name);

        PSendSysMessage(LANG_APPEARING_AT, nameLink.c_str());

        // to point where player stay (if loaded)
        float x,y,z,o;
        uint32 map;
        bool in_flight;
        if (!Player::LoadPositionFromDB(map,x,y,z,o,in_flight,target_guid))
            return false;

        // stop flight if need
        if (_player->isInFlight())
        {
            _player->GetMotionMaster()->MovementExpired();
            _player->m_taxi.ClearTaxiDestinations();
        }
        // save only in non-flight case
        else
            _player->SaveRecallPosition();

        _player->TeleportTo(map, x, y, z,_player->GetOrientation());
    }

    return true;
}

// Teleport player to last position
bool ChatHandler::HandleRecallCommand(const char* args)
{
    Player* target;
    if(!extractPlayerTarget((char*)args,&target))
        return false;

    // check online security
    if (HasLowerSecurity(target, 0))
        return false;

    if (target->IsBeingTeleported())
    {
        PSendSysMessage(LANG_IS_TELEPORTED, GetNameLink(target).c_str());
        SetSentErrorMessage(true);
        return false;
    }

    // stop flight if need
    if(target->isInFlight())
    {
        target->GetMotionMaster()->MovementExpired();
        target->m_taxi.ClearTaxiDestinations();
    }

    target->TeleportTo(target->m_recallMap, target->m_recallX, target->m_recallY, target->m_recallZ, target->m_recallO);
    return true;
}

//Edit Player HP
bool ChatHandler::HandleModifyHPCommand(const char* args)
{
    if(!*args)
        return false;

    //    char* pHp = strtok((char*)args, " ");
    //    if (!pHp)
    //        return false;

    //    char* pHpMax = strtok(NULL, " ");
    //    if (!pHpMax)
    //        return false;

    //    int32 hpm = atoi(pHpMax);
    //    int32 hp = atoi(pHp);

    int32 hp = atoi((char*)args);
    int32 hpm = atoi((char*)args);

    if (hp <= 0 || hpm <= 0 || hpm < hp)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Player *chr = getSelectedPlayer();
    if (chr == NULL)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(chr, 0))
        return false;

    PSendSysMessage(LANG_YOU_CHANGE_HP, GetNameLink(chr).c_str(), hp, hpm);
    if (needReportToTarget(chr))
        ChatHandler(chr).PSendSysMessage(LANG_YOURS_HP_CHANGED, GetNameLink().c_str(), hp, hpm);

    chr->SetMaxHealth( hpm );
    chr->SetHealth( hp );

    return true;
}

//Edit Player Mana
bool ChatHandler::HandleModifyManaCommand(const char* args)
{
    if(!*args)
        return false;

    // char* pmana = strtok((char*)args, " ");
    // if (!pmana)
    //     return false;

    // char* pmanaMax = strtok(NULL, " ");
    // if (!pmanaMax)
    //     return false;

    // int32 manam = atoi(pmanaMax);
    // int32 mana = atoi(pmana);
    int32 mana = atoi((char*)args);
    int32 manam = atoi((char*)args);

    if (mana <= 0 || manam <= 0 || manam < mana)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Player *chr = getSelectedPlayer();
    if (chr == NULL)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(chr, 0))
        return false;

    PSendSysMessage(LANG_YOU_CHANGE_MANA, GetNameLink(chr).c_str(), mana, manam);
    if (needReportToTarget(chr))
        ChatHandler(chr).PSendSysMessage(LANG_YOURS_MANA_CHANGED, GetNameLink().c_str(), mana, manam);

    chr->SetMaxPower(POWER_MANA,manam );
    chr->SetPower(POWER_MANA, mana );

    return true;
}

//Edit Player Energy
bool ChatHandler::HandleModifyEnergyCommand(const char* args)
{
    if(!*args)
        return false;

    // char* pmana = strtok((char*)args, " ");
    // if (!pmana)
    //     return false;

    // char* pmanaMax = strtok(NULL, " ");
    // if (!pmanaMax)
    //     return false;

    // int32 manam = atoi(pmanaMax);
    // int32 mana = atoi(pmana);

    int32 energy = atoi((char*)args)*10;
    int32 energym = atoi((char*)args)*10;

    if (energy <= 0 || energym <= 0 || energym < energy)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Player *chr = getSelectedPlayer();
    if (!chr)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(chr, 0))
        return false;

    PSendSysMessage(LANG_YOU_CHANGE_ENERGY, GetNameLink(chr).c_str(), energy/10, energym/10);
    if (needReportToTarget(chr))
        ChatHandler(chr).PSendSysMessage(LANG_YOURS_ENERGY_CHANGED, GetNameLink().c_str(), energy/10, energym/10);

    chr->SetMaxPower(POWER_ENERGY,energym );
    chr->SetPower(POWER_ENERGY, energy );

    DETAIL_LOG(GetMangosString(LANG_CURRENT_ENERGY),chr->GetMaxPower(POWER_ENERGY));

    return true;
}

//Edit Player Rage
bool ChatHandler::HandleModifyRageCommand(const char* args)
{
    if(!*args)
        return false;

    // char* pmana = strtok((char*)args, " ");
    // if (!pmana)
    //     return false;

    // char* pmanaMax = strtok(NULL, " ");
    // if (!pmanaMax)
    //     return false;

    // int32 manam = atoi(pmanaMax);
    // int32 mana = atoi(pmana);

    int32 rage = atoi((char*)args)*10;
    int32 ragem = atoi((char*)args)*10;

    if (rage <= 0 || ragem <= 0 || ragem < rage)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Player *chr = getSelectedPlayer();
    if (chr == NULL)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(chr, 0))
        return false;

    PSendSysMessage(LANG_YOU_CHANGE_RAGE, GetNameLink(chr).c_str(), rage/10, ragem/10);
    if (needReportToTarget(chr))
        ChatHandler(chr).PSendSysMessage(LANG_YOURS_RAGE_CHANGED, GetNameLink().c_str(), rage/10, ragem/10);

    chr->SetMaxPower(POWER_RAGE,ragem );
    chr->SetPower(POWER_RAGE, rage );

    return true;
}

// Edit Player Runic Power
bool ChatHandler::HandleModifyRunicPowerCommand(const char* args)
{
    if(!*args)
        return false;

    int32 rune = atoi((char*)args)*10;
    int32 runem = atoi((char*)args)*10;

    if (rune <= 0 || runem <= 0 || runem < rune)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Player *chr = getSelectedPlayer();
    if (chr == NULL)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage(LANG_YOU_CHANGE_RUNIC_POWER, GetNameLink(chr).c_str(), rune/10, runem/10);
    if (needReportToTarget(chr))
        ChatHandler(chr).PSendSysMessage(LANG_YOURS_RUNIC_POWER_CHANGED, GetNameLink().c_str(), rune/10, runem/10);

    chr->SetMaxPower(POWER_RUNIC_POWER,runem );
    chr->SetPower(POWER_RUNIC_POWER, rune );

    return true;
}

//Edit Player Faction
bool ChatHandler::HandleModifyFactionCommand(const char* args)
{
    if(!*args)
        return false;

    char* pfactionid = extractKeyFromLink((char*)args,"Hfaction");

    Creature* chr = getSelectedCreature();
    if(!chr)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    if(!pfactionid)
    {
        if(chr)
        {
            uint32 factionid = chr->getFaction();
            uint32 flag      = chr->GetUInt32Value(UNIT_FIELD_FLAGS);
            uint32 npcflag   = chr->GetUInt32Value(UNIT_NPC_FLAGS);
            uint32 dyflag    = chr->GetUInt32Value(UNIT_DYNAMIC_FLAGS);
            PSendSysMessage(LANG_CURRENT_FACTION,chr->GetGUIDLow(),factionid,flag,npcflag,dyflag);
        }
        return true;
    }

    if( !chr )
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    uint32 factionid = atoi(pfactionid);
    uint32 flag;

    char *pflag = strtok(NULL, " ");
    if (!pflag)
        flag = chr->GetUInt32Value(UNIT_FIELD_FLAGS);
    else
        flag = atoi(pflag);

    char* pnpcflag = strtok(NULL, " ");

    uint32 npcflag;
    if(!pnpcflag)
        npcflag   = chr->GetUInt32Value(UNIT_NPC_FLAGS);
    else
        npcflag = atoi(pnpcflag);

    char* pdyflag = strtok(NULL, " ");

    uint32  dyflag;
    if(!pdyflag)
        dyflag   = chr->GetUInt32Value(UNIT_DYNAMIC_FLAGS);
    else
        dyflag = atoi(pdyflag);

    if(!sFactionTemplateStore.LookupEntry(factionid))
    {
        PSendSysMessage(LANG_WRONG_FACTION, factionid);
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage(LANG_YOU_CHANGE_FACTION, chr->GetGUIDLow(),factionid,flag,npcflag,dyflag);

    chr->setFaction(factionid);
    chr->SetUInt32Value(UNIT_FIELD_FLAGS,flag);
    chr->SetUInt32Value(UNIT_NPC_FLAGS,npcflag);
    chr->SetUInt32Value(UNIT_DYNAMIC_FLAGS,dyflag);

    return true;
}

//Edit Player Spell
bool ChatHandler::HandleModifySpellCommand(const char* args)
{
    if(!*args) return false;
    char* pspellflatid = strtok((char*)args, " ");
    if (!pspellflatid)
        return false;

    char* pop = strtok(NULL, " ");
    if (!pop)
        return false;

    char* pval = strtok(NULL, " ");
    if (!pval)
        return false;

    uint16 mark;

    char* pmark = strtok(NULL, " ");

    uint8 spellflatid = atoi(pspellflatid);
    uint8 op   = atoi(pop);
    uint16 val = atoi(pval);
    if(!pmark)
        mark = 65535;
    else
        mark = atoi(pmark);

    Player *chr = getSelectedPlayer();
    if (chr == NULL)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(chr, 0))
        return false;

    PSendSysMessage(LANG_YOU_CHANGE_SPELLFLATID, spellflatid, val, mark, GetNameLink(chr).c_str());
    if (needReportToTarget(chr))
        ChatHandler(chr).PSendSysMessage(LANG_YOURS_SPELLFLATID_CHANGED, GetNameLink().c_str(), spellflatid, val, mark);

    WorldPacket data(SMSG_SET_FLAT_SPELL_MODIFIER, (1+1+2+2));
    data << uint8(spellflatid);
    data << uint8(op);
    data << uint16(val);
    data << uint16(mark);
    chr->GetSession()->SendPacket(&data);

    return true;
}

//Edit Player TP
bool ChatHandler::HandleModifyTalentCommand (const char* args)
{
    if (!*args)
        return false;

    int tp = atoi((char*)args);
    if (tp < 0)
        return false;

    Unit* target = getSelectedUnit();
    if(!target)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    if(target->GetTypeId()==TYPEID_PLAYER)
    {
        // check online security
        if (HasLowerSecurity((Player*)target, 0))
            return false;

        ((Player*)target)->SetFreeTalentPoints(tp);
        ((Player*)target)->SendTalentsInfoData(false);
        return true;
    }
    else if(((Creature*)target)->isPet())
    {
        Unit *owner = target->GetOwner();
        if(owner && owner->GetTypeId() == TYPEID_PLAYER && ((Pet *)target)->IsPermanentPetFor((Player*)owner))
        {
            // check online security
            if (HasLowerSecurity((Player*)owner, 0))
                return false;

            ((Pet *)target)->SetFreeTalentPoints(tp);
            ((Player*)owner)->SendTalentsInfoData(true);
            return true;
        }
    }

    SendSysMessage(LANG_NO_CHAR_SELECTED);
    SetSentErrorMessage(true);
    return false;
}

//Enable On\OFF all taxi paths
bool ChatHandler::HandleTaxiCheatCommand(const char* args)
{
    if (!*args)
    {
        SendSysMessage(LANG_USE_BOL);
        SetSentErrorMessage(true);
        return false;
    }

    std::string argstr = (char*)args;

    Player *chr = getSelectedPlayer();
    if (!chr)
    {
        chr=m_session->GetPlayer();
    }

    // check online security
    else if (HasLowerSecurity(chr, 0))
        return false;

    if (argstr == "on")
    {
        chr->SetTaxiCheater(true);
        PSendSysMessage(LANG_YOU_GIVE_TAXIS, GetNameLink(chr).c_str());
        if (needReportToTarget(chr))
            ChatHandler(chr).PSendSysMessage(LANG_YOURS_TAXIS_ADDED, GetNameLink().c_str());
        return true;
    }

    if (argstr == "off")
    {
        chr->SetTaxiCheater(false);
        PSendSysMessage(LANG_YOU_REMOVE_TAXIS, GetNameLink(chr).c_str());
        if (needReportToTarget(chr))
            ChatHandler(chr).PSendSysMessage(LANG_YOURS_TAXIS_REMOVED, GetNameLink().c_str());

        return true;
    }

    SendSysMessage(LANG_USE_BOL);
    SetSentErrorMessage(true);
    return false;
}

//Edit Player Aspeed
bool ChatHandler::HandleModifyASpeedCommand(const char* args)
{
    if (!*args)
        return false;

    float modSpeed = (float)atof((char*)args);

    if (modSpeed > 10 || modSpeed < 0.1)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Player *chr = getSelectedPlayer();
    if (chr == NULL)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(chr, 0))
        return false;

    std::string chrNameLink = GetNameLink(chr);

    if(chr->isInFlight())
    {
        PSendSysMessage(LANG_CHAR_IN_FLIGHT,chrNameLink.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage(LANG_YOU_CHANGE_ASPEED, modSpeed, chrNameLink.c_str());
    if (needReportToTarget(chr))
        ChatHandler(chr).PSendSysMessage(LANG_YOURS_ASPEED_CHANGED, GetNameLink().c_str(), modSpeed);

    chr->UpdateSpeed(MOVE_WALK,   true, modSpeed);
    chr->UpdateSpeed(MOVE_RUN,    true, modSpeed);
    chr->UpdateSpeed(MOVE_SWIM,   true, modSpeed);
    //chr->UpdateSpeed(MOVE_TURN,   true, modSpeed);
    chr->UpdateSpeed(MOVE_FLIGHT, true, modSpeed);
    return true;
}

//Edit Player Speed
bool ChatHandler::HandleModifySpeedCommand(const char* args)
{
    if (!*args)
        return false;

    float modSpeed = (float)atof((char*)args);

    if (modSpeed > 10 || modSpeed < 0.1)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Player *chr = getSelectedPlayer();
    if (chr == NULL)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(chr, 0))
        return false;

    std::string chrNameLink = GetNameLink(chr);

    if(chr->isInFlight())
    {
        PSendSysMessage(LANG_CHAR_IN_FLIGHT,chrNameLink.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage(LANG_YOU_CHANGE_SPEED, modSpeed, chrNameLink.c_str());
    if (needReportToTarget(chr))
        ChatHandler(chr).PSendSysMessage(LANG_YOURS_SPEED_CHANGED, GetNameLink().c_str(), modSpeed);

    chr->UpdateSpeed(MOVE_RUN, true, modSpeed);

    return true;
}

//Edit Player Swim Speed
bool ChatHandler::HandleModifySwimCommand(const char* args)
{
    if (!*args)
        return false;

    float modSpeed = (float)atof((char*)args);

    if (modSpeed > 10.0f || modSpeed < 0.01f)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Player *chr = getSelectedPlayer();
    if (chr == NULL)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(chr, 0))
        return false;

    std::string chrNameLink = GetNameLink(chr);

    if(chr->isInFlight())
    {
        PSendSysMessage(LANG_CHAR_IN_FLIGHT,chrNameLink.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage(LANG_YOU_CHANGE_SWIM_SPEED, modSpeed, chrNameLink.c_str());
    if (needReportToTarget(chr))
        ChatHandler(chr).PSendSysMessage(LANG_YOURS_SWIM_SPEED_CHANGED, GetNameLink().c_str(), modSpeed);

    chr->UpdateSpeed(MOVE_SWIM, true, modSpeed);

    return true;
}

//Edit Player Walk Speed
bool ChatHandler::HandleModifyBWalkCommand(const char* args)
{
    if (!*args)
        return false;

    float modSpeed = (float)atof((char*)args);

    if (modSpeed > 10.0f || modSpeed < 0.1f)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Player *chr = getSelectedPlayer();
    if (chr == NULL)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(chr, 0))
        return false;

    std::string chrNameLink = GetNameLink(chr);

    if(chr->isInFlight())
    {
        PSendSysMessage(LANG_CHAR_IN_FLIGHT,chrNameLink.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage(LANG_YOU_CHANGE_BACK_SPEED, modSpeed, chrNameLink.c_str());
    if (needReportToTarget(chr))
        ChatHandler(chr).PSendSysMessage(LANG_YOURS_BACK_SPEED_CHANGED, GetNameLink().c_str(), modSpeed);

    chr->UpdateSpeed(MOVE_RUN_BACK, true, modSpeed);

    return true;
}

//Edit Player Fly
bool ChatHandler::HandleModifyFlyCommand(const char* args)
{
    if (!*args)
        return false;

    float modSpeed = (float)atof((char*)args);

    if (modSpeed > 10.0f || modSpeed < 0.1f)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Player *chr = getSelectedPlayer();
    if (chr == NULL)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(chr, 0))
        return false;

    PSendSysMessage(LANG_YOU_CHANGE_FLY_SPEED, modSpeed, GetNameLink(chr).c_str());
    if (needReportToTarget(chr))
        ChatHandler(chr).PSendSysMessage(LANG_YOURS_FLY_SPEED_CHANGED, GetNameLink().c_str(), modSpeed);

    chr->UpdateSpeed(MOVE_FLIGHT, true, modSpeed);

    return true;
}

//Edit Player Scale
bool ChatHandler::HandleModifyScaleCommand(const char* args)
{
    if (!*args)
        return false;

    float Scale = (float)atof((char*)args);
    if (Scale > 10.0f || Scale <= 0.0f)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Unit *target = getSelectedUnit();
    if (target == NULL)
    {
        SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    if (target->GetTypeId()==TYPEID_PLAYER)
    {
        // check online security
        if (HasLowerSecurity((Player*)target, 0))
            return false;

        PSendSysMessage(LANG_YOU_CHANGE_SIZE, Scale, GetNameLink((Player*)target).c_str());
        if (needReportToTarget((Player*)target))
            ChatHandler((Player*)target).PSendSysMessage(LANG_YOURS_SIZE_CHANGED, GetNameLink().c_str(), Scale);
    }

    target->SetFloatValue(OBJECT_FIELD_SCALE_X, Scale);

    return true;
}

//Enable Player mount
bool ChatHandler::HandleModifyMountCommand(const char* args)
{
    if(!*args)
        return false;

    uint16 mId = 1147;
    float speed = (float)15;
    uint32 num = 0;

    num = atoi((char*)args);
    switch(num)
    {
        case 1:
            mId=14340;
            break;
        case 2:
            mId=4806;
            break;
        case 3:
            mId=6471;
            break;
        case 4:
            mId=12345;
            break;
        case 5:
            mId=6472;
            break;
        case 6:
            mId=6473;
            break;
        case 7:
            mId=10670;
            break;
        case 8:
            mId=10719;
            break;
        case 9:
            mId=10671;
            break;
        case 10:
            mId=10672;
            break;
        case 11:
            mId=10720;
            break;
        case 12:
            mId=14349;
            break;
        case 13:
            mId=11641;
            break;
        case 14:
            mId=12244;
            break;
        case 15:
            mId=12242;
            break;
        case 16:
            mId=14578;
            break;
        case 17:
            mId=14579;
            break;
        case 18:
            mId=14349;
            break;
        case 19:
            mId=12245;
            break;
        case 20:
            mId=14335;
            break;
        case 21:
            mId=207;
            break;
        case 22:
            mId=2328;
            break;
        case 23:
            mId=2327;
            break;
        case 24:
            mId=2326;
            break;
        case 25:
            mId=14573;
            break;
        case 26:
            mId=14574;
            break;
        case 27:
            mId=14575;
            break;
        case 28:
            mId=604;
            break;
        case 29:
            mId=1166;
            break;
        case 30:
            mId=2402;
            break;
        case 31:
            mId=2410;
            break;
        case 32:
            mId=2409;
            break;
        case 33:
            mId=2408;
            break;
        case 34:
            mId=2405;
            break;
        case 35:
            mId=14337;
            break;
        case 36:
            mId=6569;
            break;
        case 37:
            mId=10661;
            break;
        case 38:
            mId=10666;
            break;
        case 39:
            mId=9473;
            break;
        case 40:
            mId=9476;
            break;
        case 41:
            mId=9474;
            break;
        case 42:
            mId=14374;
            break;
        case 43:
            mId=14376;
            break;
        case 44:
            mId=14377;
            break;
        case 45:
            mId=2404;
            break;
        case 46:
            mId=2784;
            break;
        case 47:
            mId=2787;
            break;
        case 48:
            mId=2785;
            break;
        case 49:
            mId=2736;
            break;
        case 50:
            mId=2786;
            break;
        case 51:
            mId=14347;
            break;
        case 52:
            mId=14346;
            break;
        case 53:
            mId=14576;
            break;
        case 54:
            mId=9695;
            break;
        case 55:
            mId=9991;
            break;
        case 56:
            mId=6448;
            break;
        case 57:
            mId=6444;
            break;
        case 58:
            mId=6080;
            break;
        case 59:
            mId=6447;
            break;
        case 60:
            mId=4805;
            break;
        case 61:
            mId=9714;
            break;
        case 62:
            mId=6448;
            break;
        case 63:
            mId=6442;
            break;
        case 64:
            mId=14632;
            break;
        case 65:
            mId=14332;
            break;
        case 66:
            mId=14331;
            break;
        case 67:
            mId=8469;
            break;
        case 68:
            mId=2830;
            break;
        case 69:
            mId=2346;
            break;
        default:
            SendSysMessage(LANG_NO_MOUNT);
            SetSentErrorMessage(true);
            return false;
    }

    Player *chr = getSelectedPlayer();
    if (chr == NULL)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(chr, 0))
        return false;

    PSendSysMessage(LANG_YOU_GIVE_MOUNT, GetNameLink(chr).c_str());
    if (needReportToTarget(chr))
        ChatHandler(chr).PSendSysMessage(LANG_MOUNT_GIVED, GetNameLink().c_str());

    chr->SetUInt32Value(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP);
    chr->Mount(mId);

    WorldPacket data( SMSG_FORCE_RUN_SPEED_CHANGE, (8+4+1+4) );
    data << chr->GetPackGUID();
    data << (uint32)0;
    data << (uint8)0;                                       //new 2.1.0
    data << float(speed);
    chr->SendMessageToSet( &data, true );

    data.Initialize( SMSG_FORCE_SWIM_SPEED_CHANGE, (8+4+4) );
    data << chr->GetPackGUID();
    data << (uint32)0;
    data << float(speed);
    chr->SendMessageToSet( &data, true );

    return true;
}

//Edit Player money
bool ChatHandler::HandleModifyMoneyCommand(const char* args)
{
    if (!*args)
        return false;

    Player *chr = getSelectedPlayer();
    if (chr == NULL)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(chr, 0))
        return false;

    int32 addmoney = atoi((char*)args);

    uint32 moneyuser = chr->GetMoney();

    if (addmoney < 0)
    {
        int32 newmoney = int32(moneyuser) + addmoney;

        DETAIL_LOG(GetMangosString(LANG_CURRENT_MONEY), moneyuser, addmoney, newmoney);
        if (newmoney <= 0 )
        {
            PSendSysMessage(LANG_YOU_TAKE_ALL_MONEY, GetNameLink(chr).c_str());
            if (needReportToTarget(chr))
                ChatHandler(chr).PSendSysMessage(LANG_YOURS_ALL_MONEY_GONE, GetNameLink().c_str());

            chr->SetMoney(0);
        }
        else
        {
            if (newmoney > MAX_MONEY_AMOUNT)
                newmoney = MAX_MONEY_AMOUNT;

            PSendSysMessage(LANG_YOU_TAKE_MONEY, abs(addmoney), GetNameLink(chr).c_str());
            if (needReportToTarget(chr))
                ChatHandler(chr).PSendSysMessage(LANG_YOURS_MONEY_TAKEN, GetNameLink().c_str(), abs(addmoney));
            chr->SetMoney( newmoney );
        }
    }
    else
    {
        PSendSysMessage(LANG_YOU_GIVE_MONEY, addmoney, GetNameLink(chr).c_str());
        if (needReportToTarget(chr))
            ChatHandler(chr).PSendSysMessage(LANG_YOURS_MONEY_GIVEN, GetNameLink().c_str(), addmoney);

        if (addmoney >=MAX_MONEY_AMOUNT)
            chr->SetMoney(MAX_MONEY_AMOUNT);
        else
            chr->ModifyMoney( addmoney );
    }

    DETAIL_LOG(GetMangosString(LANG_NEW_MONEY), moneyuser, addmoney, chr->GetMoney() );

    return true;
}

//Edit Unit field
bool ChatHandler::HandleModifyBitCommand(const char* args)
{
    if( !*args )
        return false;

    Unit *unit = getSelectedUnit();
    if (!unit)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (unit->GetTypeId() == TYPEID_PLAYER && HasLowerSecurity((Player *)unit, 0))
        return false;

    char* pField = strtok((char*)args, " ");
    if (!pField)
        return false;

    char* pBit = strtok(NULL, " ");
    if (!pBit)
        return false;

    uint16 field = atoi(pField);
    uint32 bit   = atoi(pBit);

    if (field < OBJECT_END || field >= unit->GetValuesCount())
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }
    if (bit < 1 || bit > 32)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    if ( unit->HasFlag( field, (1<<(bit-1)) ) )
    {
        unit->RemoveFlag( field, (1<<(bit-1)) );
        PSendSysMessage(LANG_REMOVE_BIT, bit, field);
    }
    else
    {
        unit->SetFlag( field, (1<<(bit-1)) );
        PSendSysMessage(LANG_SET_BIT, bit, field);
    }
    return true;
}

bool ChatHandler::HandleModifyHonorCommand (const char* args)
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

    int32 amount = (uint32)atoi(args);

    target->ModifyHonorPoints(amount);

    PSendSysMessage(LANG_COMMAND_MODIFY_HONOR, GetNameLink(target).c_str(), target->GetHonorPoints());

    return true;
}

bool ChatHandler::HandleTeleCommand(const char * args)
{
    if(!*args)
        return false;

    Player* _player = m_session->GetPlayer();

    // id, or string, or [name] Shift-click form |color|Htele:id|h[name]|h|r
    GameTele const* tele = extractGameTeleFromLink((char*)args);

    if (!tele)
    {
        SendSysMessage(LANG_COMMAND_TELE_NOTFOUND);
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

    _player->TeleportTo(tele->mapId, tele->position_x, tele->position_y, tele->position_z, tele->orientation);
    return true;
}

bool ChatHandler::HandleLookupAreaCommand(const char* args)
{
    if (!*args)
        return false;

    std::string namepart = args;
    std::wstring wnamepart;

    if (!Utf8toWStr (namepart,wnamepart))
        return false;

    uint32 counter = 0;                                     // Counter for figure out that we found smth.

    // converting string that we try to find to lower case
    wstrToLower (wnamepart);

    // Search in AreaTable.dbc
    for (uint32 areaflag = 0; areaflag < sAreaStore.GetNumRows (); ++areaflag)
    {
        AreaTableEntry const *areaEntry = sAreaStore.LookupEntry (areaflag);
        if (areaEntry)
        {
            int loc = GetSessionDbcLocale ();
            std::string name = areaEntry->area_name[loc];
            if (name.empty())
                continue;

            if (!Utf8FitTo (name, wnamepart))
            {
                loc = 0;
                for(; loc < MAX_LOCALE; ++loc)
                {
                    if (loc==GetSessionDbcLocale ())
                        continue;

                    name = areaEntry->area_name[loc];
                    if (name.empty ())
                        continue;

                    if (Utf8FitTo (name, wnamepart))
                        break;
                }
            }

            if (loc < MAX_LOCALE)
            {
                // send area in "id - [name]" format
                std::ostringstream ss;
                if (m_session)
                    ss << areaEntry->ID << " - |cffffffff|Harea:" << areaEntry->ID << "|h[" << name << " " << localeNames[loc]<< "]|h|r";
                else
                    ss << areaEntry->ID << " - " << name << " " << localeNames[loc];

                SendSysMessage (ss.str ().c_str());

                ++counter;
            }
        }
    }

    if (counter == 0)                                      // if counter == 0 then we found nth
        SendSysMessage (LANG_COMMAND_NOAREAFOUND);

    return true;
}

//Find tele in game_tele order by name
bool ChatHandler::HandleLookupTeleCommand(const char * args)
{
    if(!*args)
    {
        SendSysMessage(LANG_COMMAND_TELE_PARAMETER);
        SetSentErrorMessage(true);
        return false;
    }

    char const* str = strtok((char*)args, " ");
    if(!str)
        return false;

    std::string namepart = str;
    std::wstring wnamepart;

    if(!Utf8toWStr(namepart,wnamepart))
        return false;

    // converting string that we try to find to lower case
    wstrToLower( wnamepart );

    std::ostringstream reply;

    GameTeleMap const & teleMap = sObjectMgr.GetGameTeleMap();
    for(GameTeleMap::const_iterator itr = teleMap.begin(); itr != teleMap.end(); ++itr)
    {
        GameTele const* tele = &itr->second;

        if(tele->wnameLow.find(wnamepart) == std::wstring::npos)
            continue;

        if (m_session)
            reply << "  |cffffffff|Htele:" << itr->first << "|h[" << tele->name << "]|h|r\n";
        else
            reply << "  " << itr->first << " " << tele->name << "\n";
    }

    if(reply.str().empty())
        SendSysMessage(LANG_COMMAND_TELE_NOLOCATION);
    else
        PSendSysMessage(LANG_COMMAND_TELE_LOCATION,reply.str().c_str());

    return true;
}

//Enable\Dissable accept whispers (for GM)
bool ChatHandler::HandleWhispersCommand(const char* args)
{
    if(!*args)
    {
        PSendSysMessage(LANG_COMMAND_WHISPERACCEPTING, m_session->GetPlayer()->isAcceptWhispers() ?  GetMangosString(LANG_ON) : GetMangosString(LANG_OFF));
        return true;
    }

    std::string argstr = (char*)args;
    // whisper on
    if (argstr == "on")
    {
        m_session->GetPlayer()->SetAcceptWhispers(true);
        SendSysMessage(LANG_COMMAND_WHISPERON);
        return true;
    }

    // whisper off
    if (argstr == "off")
    {
        m_session->GetPlayer()->SetAcceptWhispers(false);
        SendSysMessage(LANG_COMMAND_WHISPEROFF);
        return true;
    }

    SendSysMessage(LANG_USE_BOL);
    SetSentErrorMessage(true);
    return false;
}

//Save all players in the world
bool ChatHandler::HandleSaveAllCommand(const char* /*args*/)
{
    sObjectAccessor.SaveAllPlayers();
    SendSysMessage(LANG_PLAYERS_SAVED);
    return true;
}

// Jail by WarHead
bool ChatHandler::HandleJailCommand(const char *args)
{
    std::string cname, announce, ban_reason, ban_by;
    time_t localtime;
    localtime = time(NULL);

    char *charname = strtok((char*)args, " ");
    if (charname == NULL)
    {
        SendSysMessage(LANG_JAIL_NONAME);
        return true;
    } else cname = charname;

    char *timetojail = strtok(NULL, " ");
    if (timetojail == NULL)
    {
        SendSysMessage(LANG_JAIL_NOTIME);
        return true;
    }

    uint32 jailtime = (uint32) atoi((char*)timetojail);
    if (jailtime < 1 || jailtime > sObjectMgr.m_jailconf_max_duration)
    {
        PSendSysMessage(LANG_JAIL_VALUE, sObjectMgr.m_jailconf_max_duration);
        return true;
    }

    char *reason = strtok(NULL, "\0");
    std::string jailreason;
    if (reason == NULL || strlen((const char*)reason) < sObjectMgr.m_jailconf_min_reason)
    {
        PSendSysMessage(LANG_JAIL_NOREASON, sObjectMgr.m_jailconf_min_reason);
        return true;
    } else jailreason = reason;

    uint64 GUID = sObjectMgr.GetPlayerGUIDByName(cname.c_str());
    if (GUID == 0)
    {
        SendSysMessage(LANG_JAIL_WRONG_NAME);
        return true;
    }

    //Check security
    uint32 accountid = sObjectMgr.GetPlayerAccountIdByGUID(GUID);
    QueryResult* resultgm = loginDatabase.PQuery("SELECT gmlevel FROM account WHERE id = '%u'",accountid);
    if(resultgm)
    {
        Field* fields = resultgm->Fetch();
        AccountTypes security = (AccountTypes)fields[0].GetUInt32();
        if(!m_session || m_session->GetSecurity() < security)
           {
             SendSysMessage(LANG_YOURS_SECURITY_IS_LOW);
             SetSentErrorMessage(true);
             return true;
           }
        delete resultgm;
    }
 
    Player *chr = sObjectMgr.GetPlayer(GUID);
    if (!chr)
    {
        uint32 jail_guid = GUID_LOPART(GUID);
        std::string jail_char = cname;
        bool jail_isjailed = true;
        uint32 jail_release = localtime + (jailtime * 60 * 60);
        uint32 jail_amnestietime = localtime +(60* 60 * 24 * sObjectMgr.m_jailconf_amnestie);
        std::string jail_reason = jailreason;
        uint32 jail_times = 0;

        CharacterDatabase.BeginTransaction();
        QueryResult *result = CharacterDatabase.PQuery("SELECT * FROM `jail` WHERE `guid`='%u' LIMIT 1", jail_guid);
        CharacterDatabase.CommitTransaction();

        if (!result)
        {
            jail_times = 1;
        }
        else
        {
            Field *fields = result->Fetch();
            jail_times = fields[4].GetUInt32()+1;
        }

        uint32 jail_gmacc = m_session->GetAccountId();
        std::string jail_gmchar = m_session->GetPlayerName();

        CharacterDatabase.BeginTransaction();
        if (!result) CharacterDatabase.PExecute("INSERT INTO `jail` VALUES ('%u','%s','%u','%u','%s','%u','%u','%s',CURRENT_TIMESTAMP,'%u')", jail_guid, jail_char.c_str(), jail_release, jail_amnestietime, jail_reason.c_str(), jail_times, jail_gmacc, jail_gmchar.c_str(), jailtime);
        else CharacterDatabase.PExecute("UPDATE `jail` SET `release`='%u', `amnestietime`='%u',`reason`='%s',`times`='%u',`gmacc`='%u',`gmchar`='%s',`duration`='%u' WHERE `guid`='%u' LIMIT 1", jail_release, jail_amnestietime, jail_reason.c_str(), jail_times, jail_gmacc, jail_gmchar.c_str(), jailtime, jail_guid);
        CharacterDatabase.CommitTransaction();

        PSendSysMessage(LANG_JAIL_WAS_JAILED, cname.c_str(), jailtime);

        announce = GetMangosString(LANG_JAIL_ANNOUNCE1);
        announce += cname;
        announce += GetMangosString(LANG_JAIL_ANNOUNCE2);
        announce += timetojail;
        announce += GetMangosString(LANG_JAIL_ANNOUNCE3);
        announce += m_session->GetPlayerName();
        announce += GetMangosString(LANG_JAIL_ANNOUNCE4);
        announce += jail_reason;

        HandleAnnounceCommand(announce.c_str());

        if (result) delete result;

        if ((sObjectMgr.m_jailconf_max_jails == jail_times) && !sObjectMgr.m_jailconf_ban)
        {
            CharacterDatabase.BeginTransaction();
            QueryResult *result = CharacterDatabase.PQuery("SELECT * FROM `characters` WHERE `guid`='%u' LIMIT 1", GUID_LOPART(GUID));
            CharacterDatabase.CommitTransaction();

            if (!result)
            {
                PSendSysMessage(LANG_NO_PLAYER, cname.c_str());
                return true;
            }

            Field *fields = result->Fetch();

            Player::DeleteFromDB(GUID, fields[1].GetUInt32());

            delete result;
        }
        else if ((sObjectMgr.m_jailconf_max_jails == jail_times) && sObjectMgr.m_jailconf_ban)
        {
            CharacterDatabase.BeginTransaction();
            QueryResult *result = CharacterDatabase.PQuery("SELECT * FROM `characters` WHERE `guid`='%u' LIMIT 1", GUID_LOPART(GUID));
            CharacterDatabase.CommitTransaction();

            if (!result)
            {
                PSendSysMessage(LANG_NO_PLAYER, cname.c_str());
                return true;
            }
            Field *fields = result->Fetch();
            uint32 acc_id = fields[1].GetUInt32();

            loginDatabase.BeginTransaction();
            result = loginDatabase.PQuery("SELECT * FROM `account` WHERE `id`='%u' LIMIT 1", acc_id);
            loginDatabase.CommitTransaction();

            if (!result)
            {
                PSendSysMessage(LANG_NO_PLAYER, cname.c_str());
                return true;
            }
            ban_reason = GetMangosString(LANG_JAIL_BAN_REASON);
            ban_by = GetMangosString(LANG_JAIL_BAN_BY);

            loginDatabase.BeginTransaction();
            loginDatabase.PExecute("INSERT IGNORE INTO `account_banned` (`id`,`bandate`,`bannedby`,`banreason`) VALUES ('%u',UNIX_TIMESTAMP,'%s','%s')", acc_id, ban_by.c_str(), ban_reason.c_str());
            loginDatabase.CommitTransaction();

            delete result;
        }
        return true;
    }

    CharacterDatabase.BeginTransaction();
    QueryResult *result = CharacterDatabase.PQuery("SELECT * FROM `characters` WHERE `guid`='%u' LIMIT 1", chr->GetGUIDLow());
    CharacterDatabase.CommitTransaction();

    if (!result)
    {
        PSendSysMessage(LANG_NO_PLAYER, cname.c_str());
        return true;
    }

    Field *fields = result->Fetch();

    if(chr->GetName() == m_session->GetPlayerName())
    {
        SendSysMessage(LANG_JAIL_NO_JAIL);
        delete result;
        return true;
    }

    chr->SaveToDB();

    chr->m_jail_guid = fields[0].GetUInt32();
    chr->m_jail_char = fields[3].GetCppString();
    chr->m_jail_isjailed = true;
    chr->m_jail_release = localtime + (jailtime * 60 * 60);
    chr->m_jail_amnestietime = localtime +(60* 60 * 24 * sObjectMgr.m_jailconf_amnestie);
    chr->m_jail_reason = jailreason;
    chr->m_jail_times = chr->m_jail_times+1;
    chr->m_jail_gmacc = m_session->GetAccountId();
    chr->m_jail_gmchar = m_session->GetPlayerName();
    chr->m_jail_duration = jailtime;

    chr->_SaveJail();

    PSendSysMessage(LANG_JAIL_WAS_JAILED, fields[3].GetCppString().c_str(), jailtime);
    ChatHandler(chr).PSendSysMessage(LANG_JAIL_YOURE_JAILED, m_session->GetPlayerName(), jailtime);
    ChatHandler(chr).PSendSysMessage(LANG_JAIL_REASON, m_session->GetPlayerName(), jailreason.c_str());

    announce = GetMangosString(LANG_JAIL_ANNOUNCE1);
    announce += fields[3].GetCppString();
    announce += GetMangosString(LANG_JAIL_ANNOUNCE2);
    announce += timetojail;
    announce += GetMangosString(LANG_JAIL_ANNOUNCE3);
    announce += m_session->GetPlayerName();
    announce += GetMangosString(LANG_JAIL_ANNOUNCE4);
    announce += chr->m_jail_reason;

    HandleAnnounceCommand(announce.c_str());

    if (sObjectMgr.m_jailconf_max_jails == chr->m_jail_times)
    {
        chr->GetSession()->KickPlayer();
        chr->DeleteFromDB(fields[0].GetUInt64(), fields[1].GetUInt32());
    }
    else if ((sObjectMgr.m_jailconf_max_jails == chr->m_jail_times) && sObjectMgr.m_jailconf_ban)
    {
        uint32 acc_id = chr->GetSession()->GetAccountId();
        ban_reason = GetMangosString(LANG_JAIL_BAN_REASON);
        ban_by = GetMangosString(LANG_JAIL_BAN_BY);

        loginDatabase.BeginTransaction();
        loginDatabase.PExecute("INSERT IGNORE INTO `account_banned` (`id`,`bandate`,`bannedby`,`banreason`) VALUES ('%u',UNIX_TIMESTAMP,'%s','%s')", acc_id, ban_by.c_str(), ban_reason.c_str());
        loginDatabase.CommitTransaction();

        chr->GetSession()->LogoutPlayer(false);
    }
    else chr->GetSession()->LogoutPlayer(false);

    delete result;
    return true;
}

bool ChatHandler::HandleUnJailCommand(const char *args)
{
    char *charname = strtok((char*)args, " ");
    std::string cname;

    if (charname == NULL) return false;
    else cname = charname;

    uint64 GUID = sObjectMgr.GetPlayerGUIDByName(cname.c_str());
    Player *chr = sObjectMgr.GetPlayer(GUID);

    if (chr)
    {
        if (chr->GetName() == m_session->GetPlayerName())
        {
            SendSysMessage(LANG_JAIL_NO_UNJAIL);
            return true;
        }

        if (chr->m_jail_isjailed)
        {
            chr->m_jail_isjailed = false;
            chr->m_jail_release = 0;
            chr->m_jail_times = chr->m_jail_times-1;

            chr->_SaveJail();

            if (chr->m_jail_times == 0)
            {
                CharacterDatabase.BeginTransaction();
                CharacterDatabase.PQuery("DELETE FROM `jail` WHERE `guid`='%u' LIMIT 1", chr->GetGUIDLow());
                CharacterDatabase.CommitTransaction();
            }

            PSendSysMessage(LANG_JAIL_WAS_UNJAILED, cname.c_str());
            ChatHandler(chr).PSendSysMessage(LANG_JAIL_YOURE_UNJAILED, m_session->GetPlayerName());
            chr->CastSpell(chr,8690,false);
            //chr->GetSession()->LogoutPlayer(false);
        } else PSendSysMessage(LANG_JAIL_CHAR_NOTJAILED, cname.c_str());
        return true;
    }
    else
    {
        CharacterDatabase.BeginTransaction();
        QueryResult *jresult = CharacterDatabase.PQuery("SELECT * FROM `jail` WHERE `guid`='%u' LIMIT 1", GUID_LOPART(GUID));
        CharacterDatabase.CommitTransaction();

        if (!jresult)
        {
            PSendSysMessage(LANG_JAIL_CHAR_NOTJAILED, cname.c_str());
            return true;
        }
        else
        {
            Field *fields = jresult->Fetch();
            uint32 jail_times = fields[4].GetUInt32()-1;

            if (jail_times == 0)
            {
                CharacterDatabase.BeginTransaction();
                CharacterDatabase.PQuery("DELETE FROM `jail` WHERE `guid`='%u' LIMIT 1", fields[0].GetUInt32());
                CharacterDatabase.CommitTransaction();
            }
            else
            {
                CharacterDatabase.BeginTransaction();
                CharacterDatabase.PQuery("UPDATE `jail` SET `release`='0',`times`='%u' WHERE `guid`='%u' LIMIT 1", jail_times, fields[0].GetUInt32());
                CharacterDatabase.CommitTransaction();
            }

            PSendSysMessage(LANG_JAIL_WAS_UNJAILED, cname.c_str());

            delete jresult;
            return true;
        }

    }
    return true;
}

//Send mail by command
bool ChatHandler::HandleSendMailCommand(const char* args)
{
    // format: name "subject text" "mail text"
    Player* target;
    uint64 target_guid;
    std::string target_name;
    if(!extractPlayerTarget((char*)args,&target,&target_guid,&target_name))
        return false;

    char* tail1 = strtok(NULL, "");
    if(!tail1)
        return false;

    char* msgSubject = extractQuotedArg(tail1);
    if (!msgSubject)
        return false;

    char* tail2 = strtok(NULL, "");
    if(!tail2)
        return false;

    char* msgText = extractQuotedArg(tail2);
    if (!msgText)
        return false;

    // msgSubject, msgText isn't NUL after prev. check
    std::string subject = msgSubject;
    std::string text    = msgText;

    // from console show not existed sender
    MailSender sender(MAIL_NORMAL,m_session ? m_session->GetPlayer()->GetGUIDLow() : 0, MAIL_STATIONERY_GM);

    MailDraft(subject, text)
        .SendMailTo(MailReceiver(target,GUID_LOPART(target_guid)),sender);

    std::string nameLink = playerLink(target_name);
    PSendSysMessage(LANG_MAIL_SENT, nameLink.c_str());
    return true;
}

// teleport player to given game_tele.entry
bool ChatHandler::HandleTeleNameCommand(const char * args)
{
    char* nameStr;
    char* teleStr;
    extractOptFirstArg((char*)args,&nameStr,&teleStr);
    if(!teleStr)
        return false;

    Player* target;
    uint64 target_guid;
    std::string target_name;
    if(!extractPlayerTarget(nameStr,&target,&target_guid,&target_name))
        return false;

    // id, or string, or [name] Shift-click form |color|Htele:id|h[name]|h|r
    GameTele const* tele = extractGameTeleFromLink(teleStr);
    if(!tele)
    {
        SendSysMessage(LANG_COMMAND_TELE_NOTFOUND);
        SetSentErrorMessage(true);
        return false;
    }

    if (target)
    {
        // check online security
        if (HasLowerSecurity(target, 0))
            return false;

        std::string chrNameLink = playerLink(target_name);

        if(target->IsBeingTeleported()==true)
        {
            PSendSysMessage(LANG_IS_TELEPORTED, chrNameLink.c_str());
            SetSentErrorMessage(true);
            return false;
        }

        PSendSysMessage(LANG_TELEPORTING_TO, chrNameLink.c_str(),"", tele->name.c_str());
        if (needReportToTarget(target))
            ChatHandler(target).PSendSysMessage(LANG_TELEPORTED_TO_BY, GetNameLink().c_str());

        // stop flight if need
        if(target->isInFlight())
        {
            target->GetMotionMaster()->MovementExpired();
            target->m_taxi.ClearTaxiDestinations();
        }
        // save only in non-flight case
        else
            target->SaveRecallPosition();

        target->TeleportTo(tele->mapId,tele->position_x,tele->position_y,tele->position_z,tele->orientation);
    }
    else
    {
        // check offline security
        if (HasLowerSecurity(NULL, target_guid))
            return false;

        std::string nameLink = playerLink(target_name);

        PSendSysMessage(LANG_TELEPORTING_TO, nameLink.c_str(), GetMangosString(LANG_OFFLINE), tele->name.c_str());
        Player::SavePositionInDB(tele->mapId,tele->position_x,tele->position_y,tele->position_z,tele->orientation,
            sMapMgr.GetZoneId(tele->mapId,tele->position_x,tele->position_y,tele->position_z),target_guid);
    }

    return true;
}

//Teleport group to given game_tele.entry
bool ChatHandler::HandleTeleGroupCommand(const char * args)
{
    if(!*args)
        return false;

    Player *player = getSelectedPlayer();
    if (!player)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(player, 0))
        return false;

    // id, or string, or [name] Shift-click form |color|Htele:id|h[name]|h|r
    GameTele const* tele = extractGameTeleFromLink((char*)args);
    if(!tele)
    {
        SendSysMessage(LANG_COMMAND_TELE_NOTFOUND);
        SetSentErrorMessage(true);
        return false;
    }

    std::string nameLink = GetNameLink(player);

    Group *grp = player->GetGroup();
    if(!grp)
    {
        PSendSysMessage(LANG_NOT_IN_GROUP,nameLink.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    for(GroupReference *itr = grp->GetFirstMember(); itr != NULL; itr = itr->next())
    {
        Player *pl = itr->getSource();

        if(!pl || !pl->GetSession() )
            continue;

        // check online security
        if (HasLowerSecurity(pl, 0))
            return false;

        std::string plNameLink = GetNameLink(pl);

        if(pl->IsBeingTeleported())
        {
            PSendSysMessage(LANG_IS_TELEPORTED, plNameLink.c_str());
            continue;
        }

        PSendSysMessage(LANG_TELEPORTING_TO, plNameLink.c_str(),"", tele->name.c_str());
        if (needReportToTarget(pl))
            ChatHandler(pl).PSendSysMessage(LANG_TELEPORTED_TO_BY, nameLink.c_str());

        // stop flight if need
        if(pl->isInFlight())
        {
            pl->GetMotionMaster()->MovementExpired();
            pl->m_taxi.ClearTaxiDestinations();
        }
        // save only in non-flight case
        else
            pl->SaveRecallPosition();

        pl->TeleportTo(tele->mapId, tele->position_x, tele->position_y, tele->position_z, tele->orientation);
    }

    return true;
}

//Summon group of player
bool ChatHandler::HandleGroupgoCommand(const char* args)
{
    Player* target;
    if(!extractPlayerTarget((char*)args,&target))
        return false;

    // check online security
    if (HasLowerSecurity(target, 0))
        return false;

    Group *grp = target->GetGroup();

    std::string nameLink = GetNameLink(target);

    if(!grp)
    {
        PSendSysMessage(LANG_NOT_IN_GROUP,nameLink.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    Map* gmMap = m_session->GetPlayer()->GetMap();
    bool to_instance =  gmMap->Instanceable();

    // we are in instance, and can summon only player in our group with us as lead
    if ( to_instance && (
        !m_session->GetPlayer()->GetGroup() || (grp->GetLeaderGUID() != m_session->GetPlayer()->GetGUID()) ||
        (m_session->GetPlayer()->GetGroup()->GetLeaderGUID() != m_session->GetPlayer()->GetGUID()) ) )
        // the last check is a bit excessive, but let it be, just in case
    {
        SendSysMessage(LANG_CANNOT_SUMMON_TO_INST);
        SetSentErrorMessage(true);
        return false;
    }

    for(GroupReference *itr = grp->GetFirstMember(); itr != NULL; itr = itr->next())
    {
        Player *pl = itr->getSource();

        if(!pl || pl==m_session->GetPlayer() || !pl->GetSession() )
            continue;

        // check online security
        if (HasLowerSecurity(pl, 0))
            return false;

        std::string plNameLink = GetNameLink(pl);

        if(pl->IsBeingTeleported()==true)
        {
            PSendSysMessage(LANG_IS_TELEPORTED, plNameLink.c_str());
            SetSentErrorMessage(true);
            return false;
        }

        if (to_instance)
        {
            Map* plMap = pl->GetMap();

            if ( plMap->Instanceable() && plMap->GetInstanceId() != gmMap->GetInstanceId() )
            {
                // cannot summon from instance to instance
                PSendSysMessage(LANG_CANNOT_SUMMON_TO_INST,plNameLink.c_str());
                SetSentErrorMessage(true);
                return false;
            }
        }

        PSendSysMessage(LANG_SUMMONING, plNameLink.c_str(),"");
        if (needReportToTarget(pl))
            ChatHandler(pl).PSendSysMessage(LANG_SUMMONED_BY, nameLink.c_str());

        // stop flight if need
        if(pl->isInFlight())
        {
            pl->GetMotionMaster()->MovementExpired();
            pl->m_taxi.ClearTaxiDestinations();
        }
        // save only in non-flight case
        else
            pl->SaveRecallPosition();

        // before GM
        float x,y,z;
        m_session->GetPlayer()->GetClosePoint(x,y,z,pl->GetObjectSize());
        pl->TeleportTo(m_session->GetPlayer()->GetMapId(),x,y,z,pl->GetOrientation());
    }

    return true;
}

bool ChatHandler::HandleGoTaxinodeCommand(const char* args)
{
    Player* _player = m_session->GetPlayer();

    if (!*args)
        return false;

    char* cNodeId = extractKeyFromLink((char*)args,"Htaxinode");
    if (!cNodeId)
        return false;

    int32 i_nodeId = atoi(cNodeId);
    if (!i_nodeId)
        return false;

    TaxiNodesEntry const* node = sTaxiNodesStore.LookupEntry(i_nodeId);
    if (!node)
    {
        PSendSysMessage(LANG_COMMAND_GOTAXINODENOTFOUND,i_nodeId);
        SetSentErrorMessage(true);
        return false;
    }

    if ((node->x == 0.0f && node->y == 0.0f && node->z == 0.0f) ||
        !MapManager::IsValidMapCoord(node->map_id,node->x,node->y,node->z))
    {
        PSendSysMessage(LANG_INVALID_TARGET_COORD,node->x,node->y,node->map_id);
        SetSentErrorMessage(true);
        return false;
    }

    // stop flight if need
    if (_player->isInFlight())
    {
        _player->GetMotionMaster()->MovementExpired();
        _player->m_taxi.ClearTaxiDestinations();
    }
    // save only in non-flight case
    else
        _player->SaveRecallPosition();

    _player->TeleportTo(node->map_id, node->x, node->y, node->z, _player->GetOrientation());
    return true;
}

//teleport at coordinates
bool ChatHandler::HandleGoXYCommand(const char* args)
{
    if(!*args)
        return false;

    Player* _player = m_session->GetPlayer();

    char* px = strtok((char*)args, " ");
    char* py = strtok(NULL, " ");
    char* pmapid = strtok(NULL, " ");

    if (!px || !py)
        return false;

    float x = (float)atof(px);
    float y = (float)atof(py);
    uint32 mapid;
    if (pmapid)
        mapid = (uint32)atoi(pmapid);
    else mapid = _player->GetMapId();

    if(!MapManager::IsValidMapCoord(mapid,x,y))
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

    Map const *map = sMapMgr.CreateBaseMap(mapid);
    float z = std::max(map->GetHeight(x, y, MAX_HEIGHT), map->GetWaterLevel(x, y));

    _player->TeleportTo(mapid, x, y, z, _player->GetOrientation());

    return true;
}

//teleport at coordinates, including Z
bool ChatHandler::HandleGoXYZCommand(const char* args)
{
    if(!*args)
        return false;

    Player* _player = m_session->GetPlayer();

    char* px = strtok((char*)args, " ");
    char* py = strtok(NULL, " ");
    char* pz = strtok(NULL, " ");
    char* pmapid = strtok(NULL, " ");

    if (!px || !py || !pz)
        return false;

    float x = (float)atof(px);
    float y = (float)atof(py);
    float z = (float)atof(pz);
    uint32 mapid;
    if (pmapid)
        mapid = (uint32)atoi(pmapid);
    else
        mapid = _player->GetMapId();

    if(!MapManager::IsValidMapCoord(mapid,x,y,z))
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

    _player->TeleportTo(mapid, x, y, z, _player->GetOrientation());

    return true;
}

//teleport at coordinates
bool ChatHandler::HandleGoZoneXYCommand(const char* args)
{
    if(!*args)
        return false;

    Player* _player = m_session->GetPlayer();

    char* px = strtok((char*)args, " ");
    char* py = strtok(NULL, " ");
    char* tail = strtok(NULL,"");

    char* cAreaId = extractKeyFromLink(tail,"Harea");       // string or [name] Shift-click form |color|Harea:area_id|h[name]|h|r

    if (!px || !py)
        return false;

    float x = (float)atof(px);
    float y = (float)atof(py);

    // prevent accept wrong numeric args
    if ((x==0.0f && *px!='0') || (y==0.0f && *py!='0'))
        return false;

    uint32 areaid = cAreaId ? (uint32)atoi(cAreaId) : _player->GetZoneId();

    AreaTableEntry const* areaEntry = GetAreaEntryByAreaID(areaid);

    if( x<0 || x>100 || y<0 || y>100 || !areaEntry )
    {
        PSendSysMessage(LANG_INVALID_ZONE_COORD,x,y,areaid);
        SetSentErrorMessage(true);
        return false;
    }

    // update to parent zone if exist (client map show only zones without parents)
    AreaTableEntry const* zoneEntry = areaEntry->zone ? GetAreaEntryByAreaID(areaEntry->zone) : areaEntry;

    Map const *map = sMapMgr.CreateBaseMap(zoneEntry->mapid);

    if(map->Instanceable())
    {
        PSendSysMessage(LANG_INVALID_ZONE_MAP,areaEntry->ID,areaEntry->area_name[GetSessionDbcLocale()],map->GetId(),map->GetMapName());
        SetSentErrorMessage(true);
        return false;
    }

    if (!Zone2MapCoordinates(x,y,zoneEntry->ID))
    {
        PSendSysMessage(LANG_INVALID_ZONE_MAP,areaEntry->ID,areaEntry->area_name[GetSessionDbcLocale()],map->GetId(),map->GetMapName());
        SetSentErrorMessage(true);
        return false;
    }

    if(!MapManager::IsValidMapCoord(zoneEntry->mapid,x,y))
    {
        PSendSysMessage(LANG_INVALID_TARGET_COORD,x,y,zoneEntry->mapid);
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

    float z = std::max(map->GetHeight(x, y, MAX_HEIGHT), map->GetWaterLevel(x, y));
    _player->TeleportTo(zoneEntry->mapid, x, y, z, _player->GetOrientation());

    return true;
}

//teleport to grid
bool ChatHandler::HandleGoGridCommand(const char* args)
{
    if(!*args)    return false;
    Player* _player = m_session->GetPlayer();

    char* px = strtok((char*)args, " ");
    char* py = strtok(NULL, " ");
    char* pmapid = strtok(NULL, " ");

    if (!px || !py)
        return false;

    float grid_x = (float)atof(px);
    float grid_y = (float)atof(py);
    uint32 mapid;
    if (pmapid)
        mapid = (uint32)atoi(pmapid);
    else mapid = _player->GetMapId();

    // center of grid
    float x = (grid_x-CENTER_GRID_ID+0.5f)*SIZE_OF_GRIDS;
    float y = (grid_y-CENTER_GRID_ID+0.5f)*SIZE_OF_GRIDS;

    if(!MapManager::IsValidMapCoord(mapid,x,y))
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

    Map const *map = sMapMgr.CreateBaseMap(mapid);
    float z = std::max(map->GetHeight(x, y, MAX_HEIGHT), map->GetWaterLevel(x, y));
    _player->TeleportTo(mapid, x, y, z, _player->GetOrientation());

    return true;
}

bool ChatHandler::HandleModifyDrunkCommand(const char* args)
{
    if(!*args)    return false;

    uint32 drunklevel = (uint32)atoi(args);
    if(drunklevel > 100)
        drunklevel = 100;

    uint16 drunkMod = drunklevel * 0xFFFF / 100;

    m_session->GetPlayer()->SetDrunkValue(drunkMod);

    return true;
}
