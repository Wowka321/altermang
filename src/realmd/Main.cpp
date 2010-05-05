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

/// \addtogroup realmd Realm Daemon
/// @{
/// \file

#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "RealmList.h"

#include "Config/ConfigEnv.h"
#include "Log.h"
#include "sockets/ListenSocket.h"
#include "AuthSocket.h"
#include "SystemConfig.h"
#include "revision.h"
#include "revision_nr.h"
#include "revision_sql.h"
#include "Util.h"
#include <openssl/opensslv.h>
#include <openssl/crypto.h>

#ifdef WIN32
#include "ServiceWin32.h"
char serviceName[] = "realmd";
char serviceLongName[] = "MaNGOS realmd service";
char serviceDescription[] = "Massive Network Game Object Server";
/*
 * -1 - not in service mode
 *  0 - stopped
 *  1 - running
 *  2 - paused
 */
int m_ServiceStatus = -1;
#endif

bool StartDB();
void UnhookSignals();
void HookSignals();

bool stopEvent = false;                                     ///< Setting it to true stops the server

DatabaseType loginDatabase;                                 ///< Accessor to the realm server database

/// Print out the usage string for this program on the console.
void usage(const char *prog)
{
    sLog.outString("Usage: \n %s [<options>]\n"
        "    --version                print version and exist\n\r"
        "    -c config_file           use config_file as configuration file\n\r"
        #ifdef WIN32
        "    Running as service functions:\n\r"
        "    --service                run as service\n\r"
        "    -s install               install service\n\r"
        "    -s uninstall             uninstall service\n\r"
        #endif
        ,prog);
}

/// Launch the realm server
extern int main(int argc, char **argv)
{
    ///- Command line parsing to get the configuration file name
    char const* cfg_file = _REALMD_CONFIG;
    int c=1;
    while( c < argc )
    {
        if( strcmp(argv[c],"-c") == 0)
        {
            if( ++c >= argc )
            {
                sLog.outError("Runtime-Error: -c option requires an input argument");
                usage(argv[0]);
                return 1;
            }
            else
                cfg_file = argv[c];
        }

        if( strcmp(argv[c],"--version") == 0)
        {
            printf("%s\n", _FULLVERSION(REVISION_DATE,REVISION_TIME,REVISION_NR,REVISION_ID));
            return 0;
        }

        #ifdef WIN32
        ////////////
        //Services//
        ////////////
        if( strcmp(argv[c],"-s") == 0)
        {
            if( ++c >= argc )
            {
                sLog.outError("Runtime-Error: -s option requires an input argument");
                usage(argv[0]);
                return 1;
            }
            if( strcmp(argv[c],"install") == 0)
            {
                if (WinServiceInstall())
                    sLog.outString("Installing service");
                return 1;
            }
            else if( strcmp(argv[c],"uninstall") == 0)
            {
                if(WinServiceUninstall())
                    sLog.outString("Uninstalling service");
                return 1;
            }
            else
            {
                sLog.outError("Runtime-Error: unsupported option %s",argv[c]);
                usage(argv[0]);
                return 1;
            }
        }
        if( strcmp(argv[c],"--service") == 0)
        {
            WinServiceRun();
        }
        ////
        #endif
        ++c;
    }

    if (!sConfig.SetSource(cfg_file))
    {
        sLog.outError("Could not find configuration file %s.", cfg_file);
        return 1;
    }
    sLog.Initialize();

    sLog.outString( "%s [realm-daemon]", _FULLVERSION(REVISION_DATE,REVISION_TIME,REVISION_NR,REVISION_ID) );
    sLog.outString( "<Ctrl-C> to stop.\n" );
    sLog.outString("Using configuration file %s.", cfg_file);

    ///- Check the version of the configuration file
    uint32 confVersion = sConfig.GetIntDefault("ConfVersion", 0);
    if (confVersion < _REALMDCONFVERSION)
    {
        sLog.outError("*****************************************************************************");
        sLog.outError(" WARNING: Your realmd.conf version indicates your conf file is out of date!");
        sLog.outError("          Please check for updates, as your current default values may cause");
        sLog.outError("          strange behavior.");
        sLog.outError("*****************************************************************************");
        clock_t pause = 3000 + clock();

        while (pause > clock()) {}
    }

    DETAIL_LOG("%s (Library: %s)", OPENSSL_VERSION_TEXT, SSLeay_version(SSLEAY_VERSION));
    if (SSLeay() < 0x009080bfL )
    {
        DETAIL_LOG("WARNING: Outdated version of OpenSSL lib. Logins to server may not work!");
        DETAIL_LOG("WARNING: Minimal required version [OpenSSL 0.9.8k]");
    }

    /// realmd PID file creation
    std::string pidfile = sConfig.GetStringDefault("PidFile", "");
    if(!pidfile.empty())
    {
        uint32 pid = CreatePIDFile(pidfile);
        if( !pid )
        {
            sLog.outError( "Cannot create PID file %s.\n", pidfile.c_str() );
            return 1;
        }

        sLog.outString( "Daemon PID: %u\n", pid );
    }

    ///- Initialize the database connection
    if(!StartDB())
        return 1;

    ///- Get the list of realms for the server
    sRealmList.Initialize(sConfig.GetIntDefault("RealmsStateUpdateDelay", 20));
    if (sRealmList.size() == 0)
    {
        sLog.outError("No valid realms specified.");
        return 1;
    }

    ///- Launch the listening network socket
    port_t rmport = sConfig.GetIntDefault( "RealmServerPort", DEFAULT_REALMSERVER_PORT );
    std::string bind_ip = sConfig.GetStringDefault("BindIP", "0.0.0.0");

    SocketHandler h;
    ListenSocket<AuthSocket> authListenSocket(h);
    if ( authListenSocket.Bind(bind_ip.c_str(),rmport))
    {
        sLog.outError( "MaNGOS realmd can not bind to %s:%d",bind_ip.c_str(), rmport );
        return 1;
    }

    // cleanup query
    //set expired bans to inactive
    loginDatabase.Execute("UPDATE account_banned SET active = 0 WHERE unbandate<=UNIX_TIMESTAMP() AND unbandate<>bandate");
    loginDatabase.Execute("DELETE FROM ip_banned WHERE unbandate<=UNIX_TIMESTAMP() AND unbandate<>bandate");

    h.Add(&authListenSocket);

    ///- Catch termination signals
    HookSignals();

    ///- Handle affinity for multiple processors and process priority on Windows
    #ifdef WIN32
    {
        HANDLE hProcess = GetCurrentProcess();

        uint32 Aff = sConfig.GetIntDefault("UseProcessors", 0);
        if(Aff > 0)
        {
            ULONG_PTR appAff;
            ULONG_PTR sysAff;

            if(GetProcessAffinityMask(hProcess,&appAff,&sysAff))
            {
                ULONG_PTR curAff = Aff & appAff;            // remove non accessible processors

                if(!curAff )
                {
                    sLog.outError("Processors marked in UseProcessors bitmask (hex) %x not accessible for realmd. Accessible processors bitmask (hex): %x",Aff,appAff);
                }
                else
                {
                    if(SetProcessAffinityMask(hProcess,curAff))
                        sLog.outString("Using processors (bitmask, hex): %x", curAff);
                    else
                        sLog.outError("Can't set used processors (hex): %x", curAff);
                }
            }
            sLog.outString();
        }

        bool Prio = sConfig.GetBoolDefault("ProcessPriority", false);

        if(Prio)
        {
            if(SetPriorityClass(hProcess,HIGH_PRIORITY_CLASS))
                sLog.outString("realmd process priority class set to HIGH");
            else
                sLog.outError("ERROR: Can't set realmd process priority class.");
            sLog.outString();
        }
    }
    #endif

    // maximum counter for next ping
    uint32 numLoops = (sConfig.GetIntDefault( "MaxPingTime", 30 ) * (MINUTE * 1000000 / 100000));
    uint32 loopCounter = 0;

    ///- Wait for termination signal
    while (!stopEvent)
    {

        h.Select(0, 100000);

        if( (++loopCounter) == numLoops )
        {
            loopCounter = 0;
            DETAIL_LOG("Ping MySQL to keep connection alive");
            delete loginDatabase.Query("SELECT 1 FROM realmlist LIMIT 1");
        }
#ifdef WIN32
        if (m_ServiceStatus == 0) stopEvent = true;
        while (m_ServiceStatus == 2) Sleep(1000);
#endif
    }

    ///- Wait for the delay thread to exit
    loginDatabase.HaltDelayThread();

    ///- Remove signal handling before leaving
    UnhookSignals();

    sLog.outString( "Halting process..." );
    return 0;
}

/// Handle termination signals
/** Put the global variable stopEvent to 'true' if a termination signal is caught **/
void OnSignal(int s)
{
    switch (s)
    {
        case SIGINT:
        case SIGTERM:
            stopEvent = true;
            break;
        #ifdef _WIN32
        case SIGBREAK:
            stopEvent = true;
            break;
        #endif
    }

    signal(s, OnSignal);
}

/// Initialize connection to the database
bool StartDB()
{
    std::string dbstring = sConfig.GetStringDefault("LoginDatabaseInfo", "");
    if(dbstring.empty())
    {
        sLog.outError("Database not specified");
        return false;
    }

    sLog.outString("Database: %s", dbstring.c_str() );
    if(!loginDatabase.Initialize(dbstring.c_str()))
    {
        sLog.outError("Cannot connect to database");
        return false;
    }

    if(!loginDatabase.CheckRequiredField("realmd_db_version",REVISION_DB_REALMD))
    {
        ///- Wait for already started DB delay threads to end
        loginDatabase.HaltDelayThread();
        return false;
    }

    return true;
}

/// Define hook 'OnSignal' for all termination signals
void HookSignals()
{
    signal(SIGINT, OnSignal);
    signal(SIGTERM, OnSignal);
    #ifdef _WIN32
    signal(SIGBREAK, OnSignal);
    #endif
}

/// Unhook the signals before leaving
void UnhookSignals()
{
    signal(SIGINT, 0);
    signal(SIGTERM, 0);
    #ifdef _WIN32
    signal(SIGBREAK, 0);
    #endif
}

/// @}
