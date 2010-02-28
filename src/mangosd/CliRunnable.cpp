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

/// \addtogroup mangosd
/// @{
/// \file

#include "Common.h"
#include "Language.h"
#include "Log.h"
#include "World.h"
#include "ScriptCalls.h"
#include "ObjectMgr.h"
#include "WorldSession.h"
#include "Config/ConfigEnv.h"
#include "Util.h"
#include "AccountMgr.h"
#include "CliRunnable.h"
#include "MapManager.h"
#include "Player.h"
#include "Chat.h"

void utf8print(void* arg, const char* str)
{
#if PLATFORM == PLATFORM_WINDOWS
    wchar_t wtemp_buf[6000];
    size_t wtemp_len = 6000-1;
    if(!Utf8toWStr(str,strlen(str),wtemp_buf,wtemp_len))
        return;

    char temp_buf[6000];
    CharToOemBuffW(&wtemp_buf[0],&temp_buf[0],wtemp_len+1);
    printf("%s", temp_buf);
#else
    printf("%s", str);
#endif
}

void commandFinished(void*, bool sucess)
{
    printf("mangos>");
    fflush(stdout);
}

/// Delete a user account and all associated characters in this realm
/// \todo This function has to be enhanced to respect the login/realm split (delete char, delete account chars in realm, delete account chars in realm then delete account
bool ChatHandler::HandleAccountDeleteCommand(const char* args)
{
    if (!*args)
        return false;

    ///- Get the account name from the command line
    char *account_name_str=strtok ((char*)args," ");
    if (!account_name_str)
        return false;

    std::string account_name = account_name_str;
    if (!AccountMgr::normalizeString(account_name))
    {
        PSendSysMessage(LANG_ACCOUNT_NOT_EXIST,account_name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    uint32 account_id = sAccountMgr.GetId(account_name);
    if (!account_id)
    {
        PSendSysMessage(LANG_ACCOUNT_NOT_EXIST,account_name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    /// Commands not recommended call from chat, but support anyway
    /// can delete only for account with less security
    /// This is also reject self apply in fact
    if(HasLowerSecurityAccount (NULL,account_id,true))
        return false;

    AccountOpResult result = sAccountMgr.DeleteAccount(account_id);
    switch(result)
    {
        case AOR_OK:
            PSendSysMessage(LANG_ACCOUNT_DELETED,account_name.c_str());
            break;
        case AOR_NAME_NOT_EXIST:
            PSendSysMessage(LANG_ACCOUNT_NOT_EXIST,account_name.c_str());
            SetSentErrorMessage(true);
            return false;
        case AOR_DB_INTERNAL_ERROR:
            PSendSysMessage(LANG_ACCOUNT_NOT_DELETED_SQL_ERROR,account_name.c_str());
            SetSentErrorMessage(true);
            return false;
        default:
            PSendSysMessage(LANG_ACCOUNT_NOT_DELETED,account_name.c_str());
            SetSentErrorMessage(true);
            return false;
    }

    return true;
}

bool ChatHandler::HandleCharacterDeleteCommand(const char* args)
{
    if(!*args)
        return false;

    char *character_name_str = strtok((char*)args," ");
    if(!character_name_str)
        return false;

    std::string character_name = character_name_str;
    if(!normalizePlayerName(character_name))
        return false;

    uint64 character_guid;
    uint32 account_id;

    Player *player = sObjectMgr.GetPlayer(character_name.c_str());
    if(player)
    {
        character_guid = player->GetGUID();
        account_id = player->GetSession()->GetAccountId();
        player->GetSession()->KickPlayer();
    }
    else
    {
        character_guid = sObjectMgr.GetPlayerGUIDByName(character_name);
        if(!character_guid)
        {
            PSendSysMessage(LANG_NO_PLAYER,character_name.c_str());
            SetSentErrorMessage(true);
            return false;
        }

        account_id = sObjectMgr.GetPlayerAccountIdByGUID(character_guid);
    }

    std::string account_name;
    sAccountMgr.GetName (account_id,account_name);

    Player::DeleteFromDB(character_guid, account_id, true);
    PSendSysMessage(LANG_CHARACTER_DELETED,character_name.c_str(),GUID_LOPART(character_guid),account_name.c_str(), account_id);
    return true;
}

/// Close RA connection
bool ChatHandler::HandleQuitCommand(const char* /*args*/)
{
    // processed in RASocket
    SendSysMessage(LANG_QUIT_WRONG_USE_ERROR);
    return true;
}

/// Exit the realm
bool ChatHandler::HandleServerExitCommand(const char* /*args*/)
{
    SendSysMessage(LANG_COMMAND_EXIT);
    World::StopNow(SHUTDOWN_EXIT_CODE);
    return true;
}

/// Display info on users currently in the realm
bool ChatHandler::HandleAccountOnlineListCommand(const char* /*args*/)
{
    ///- Get the list of accounts ID logged to the realm
    QueryResult *resultDB = CharacterDatabase.Query("SELECT name,account FROM characters WHERE online > 0");
    if (!resultDB)
    {
        SendSysMessage(LANG_ACCOUNT_LIST_EMPTY);
        return true;
    }

    ///- Display the list of account/characters online
    SendSysMessage(LANG_ACCOUNT_LIST_BAR);
    SendSysMessage(LANG_ACCOUNT_LIST_HEADER);
    SendSysMessage(LANG_ACCOUNT_LIST_BAR);

    ///- Circle through accounts
    do
    {
        Field *fieldsDB = resultDB->Fetch();
        std::string name = fieldsDB[0].GetCppString();
        uint32 account = fieldsDB[1].GetUInt32();

        ///- Get the username, last IP and GM level of each account
        // No SQL injection. account is uint32.
        //                                                      0         1        2        3
        QueryResult *resultLogin = loginDatabase.PQuery("SELECT username, last_ip, gmlevel, expansion FROM account WHERE id = '%u'",account);

        if(resultLogin)
        {
            Field *fieldsLogin = resultLogin->Fetch();
            PSendSysMessage(LANG_ACCOUNT_LIST_LINE,
                fieldsLogin[0].GetString(),name.c_str(),fieldsLogin[1].GetString(),fieldsLogin[2].GetUInt32(),fieldsLogin[3].GetUInt32());

            delete resultLogin;
        }
        else
            PSendSysMessage(LANG_ACCOUNT_LIST_ERROR,name.c_str());

    }while(resultDB->NextRow());

    delete resultDB;

    SendSysMessage(LANG_ACCOUNT_LIST_BAR);
    return true;
}

/// Create an account
bool ChatHandler::HandleAccountCreateCommand(const char* args)
{
    if(!*args)
        return false;

    ///- %Parse the command line arguments
    char *szAcc = strtok((char*)args, " ");
    char *szPassword = strtok(NULL, " ");
    if(!szAcc || !szPassword)
        return false;

    // normalized in accmgr.CreateAccount
    std::string account_name = szAcc;
    std::string password = szPassword;

    AccountOpResult result = sAccountMgr.CreateAccount(account_name, password);
    switch(result)
    {
        case AOR_OK:
            PSendSysMessage(LANG_ACCOUNT_CREATED,account_name.c_str());
            break;
        case AOR_NAME_TOO_LONG:
            SendSysMessage(LANG_ACCOUNT_TOO_LONG);
            SetSentErrorMessage(true);
            return false;
        case AOR_NAME_ALREDY_EXIST:
            SendSysMessage(LANG_ACCOUNT_ALREADY_EXIST);
            SetSentErrorMessage(true);
            return false;
        case AOR_DB_INTERNAL_ERROR:
            PSendSysMessage(LANG_ACCOUNT_NOT_CREATED_SQL_ERROR,account_name.c_str());
            SetSentErrorMessage(true);
            return false;
        default:
            PSendSysMessage(LANG_ACCOUNT_NOT_CREATED,account_name.c_str());
            SetSentErrorMessage(true);
            return false;
    }

    return true;
}

/// Set the level of logging
bool ChatHandler::HandleServerSetLogLevelCommand(const char *args)
{
    if(!*args)
        return false;

    char *NewLevel = strtok((char*)args, " ");
    if (!NewLevel)
        return false;

    sLog.SetLogLevel(NewLevel);
    return true;
}

/// @}

#ifdef linux
// Non-blocking keypress detector, when return pressed, return 1, else always return 0
int kb_hit_return()
{
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
}
#endif

/// %Thread start
void CliRunnable::run()
{
    ///- Init new SQL thread for the world database (one connection call enough)
    WorldDatabase.ThreadStart();                                // let thread do safe mySQL requests

    char commandbuf[256];

    ///- Display the list of available CLI functions then beep
    sLog.outString();

    if(sConfig.GetBoolDefault("BeepAtStart", true))
        printf("\a");                                       // \a = Alert

    // print this here the first time
    // later it will be printed after command queue updates
    printf("mangos>");

    ///- As long as the World is running (no World::m_stopEvent), get the command line and handle it
    while (!World::IsStopped())
    {
        fflush(stdout);
        #ifdef linux
        while (!kb_hit_return() && !World::IsStopped())
            // With this, we limit CLI to 10commands/second
            usleep(100);
        if (World::IsStopped())
            break;
        #endif
        char *command_str = fgets(commandbuf,sizeof(commandbuf),stdin);
        if (command_str != NULL)
        {
            for(int x=0;command_str[x];x++)
                if(command_str[x]=='\r'||command_str[x]=='\n')
            {
                command_str[x]=0;
                break;
            }


            if(!*command_str)
            {
                printf("mangos>");
                continue;
            }

            std::string command;
            if(!consoleToUtf8(command_str,command))         // convert from console encoding to utf8
            {
                printf("mangos>");
                continue;
            }

            sWorld.QueueCliCommand(new CliCommandHolder(NULL, command.c_str(), &utf8print, &commandFinished));
        }
        else if (feof(stdin))
        {
            World::StopNow(SHUTDOWN_EXIT_CODE);
        }
    }

    ///- End the database thread
    WorldDatabase.ThreadEnd();                                  // free mySQL thread resources
}
