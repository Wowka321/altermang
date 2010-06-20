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

#include "DatabaseEnv.h"
#include "Config/Config.h"

#include <ctime>
#include <iostream>
#include <fstream>

Database::~Database()
{
    /*Delete objects*/
}

bool Database::Initialize(const char *)
{
    // Enable logging of SQL commands (usally only GM commands)
    // (See method: PExecuteLog)
    m_logSQL = sConfig.GetBoolDefault("LogSQL", false);
    m_logsDir = sConfig.GetStringDefault("LogsDir","");
    if(!m_logsDir.empty())
    {
        if((m_logsDir.at(m_logsDir.length()-1)!='/') && (m_logsDir.at(m_logsDir.length()-1)!='\\'))
            m_logsDir.append("/");
    }

    m_pingIntervallms = sConfig.GetIntDefault ("MaxPingTime", 30) * (MINUTE * 1000);
    return true;
}

void Database::ThreadStart()
{
}

void Database::ThreadEnd()
{
}

void Database::escape_string(std::string& str)
{
    if(str.empty())
        return;

    char* buf = new char[str.size()*2+1];
    escape_string(buf,str.c_str(),str.size());
    str = buf;
    delete[] buf;
}

bool Database::PExecuteLog(const char * format,...)
{
    if (!format)
        return false;

    va_list ap;
    char szQuery [MAX_QUERY_LEN];
    va_start(ap, format);
    int res = vsnprintf( szQuery, MAX_QUERY_LEN, format, ap );
    va_end(ap);

    if(res==-1)
    {
        sLog.outError("SQL Query truncated (and not execute) for format: %s",format);
        return false;
    }

    if( m_logSQL )
    {
        time_t curr;
        tm local;
        time(&curr);                                        // get current time_t value
        local=*(localtime(&curr));                          // dereference and assign
        char fName[128];
        sprintf( fName, "%04d-%02d-%02d_logSQL.sql", local.tm_year+1900, local.tm_mon+1, local.tm_mday );

        FILE* log_file;
        std::string logsDir_fname = m_logsDir+fName;
        log_file = fopen(logsDir_fname.c_str(), "a");
        if (log_file)
        {
            fprintf(log_file, "%s;\n", szQuery);
            fclose(log_file);
        }
        else
        {
            // The file could not be opened
            sLog.outError("SQL-Logging is disabled - Log file for the SQL commands could not be openend: %s",fName);
        }
    }

    return Execute(szQuery);
}

void Database::SetResultQueue(SqlResultQueue * queue)
{
    m_queryQueues[ACE_Based::Thread::current()] = queue;

}

QueryResult* Database::PQuery(const char *format,...)
{
    if(!format) return NULL;

    va_list ap;
    char szQuery [MAX_QUERY_LEN];
    va_start(ap, format);
    int res = vsnprintf( szQuery, MAX_QUERY_LEN, format, ap );
    va_end(ap);

    if(res==-1)
    {
        sLog.outError("SQL Query truncated (and not execute) for format: %s",format);
        return false;
    }

    return Query(szQuery);
}

QueryNamedResult* Database::PQueryNamed(const char *format,...)
{
    if(!format) return NULL;

    va_list ap;
    char szQuery [MAX_QUERY_LEN];
    va_start(ap, format);
    int res = vsnprintf( szQuery, MAX_QUERY_LEN, format, ap );
    va_end(ap);

    if(res==-1)
    {
        sLog.outError("SQL Query truncated (and not execute) for format: %s",format);
        return false;
    }

    return QueryNamed(szQuery);
}

bool Database::PExecute(const char * format,...)
{
    if (!format)
        return false;

    va_list ap;
    char szQuery [MAX_QUERY_LEN];
    va_start(ap, format);
    int res = vsnprintf( szQuery, MAX_QUERY_LEN, format, ap );
    va_end(ap);

    if(res==-1)
    {
        sLog.outError("SQL Query truncated (and not execute) for format: %s",format);
        return false;
    }

    return Execute(szQuery);
}

bool Database::DirectPExecute(const char * format,...)
{
    if (!format)
        return false;

    va_list ap;
    char szQuery [MAX_QUERY_LEN];
    va_start(ap, format);
    int res = vsnprintf( szQuery, MAX_QUERY_LEN, format, ap );
    va_end(ap);

    if(res==-1)
    {
        sLog.outError("SQL Query truncated (and not execute) for format: %s",format);
        return false;
    }

    return DirectExecute(szQuery);
}

bool Database::CheckRequiredField( char const* table_name, char const* required_name )
{
    // check required field
    QueryResult* result = PQuery("SELECT %s FROM %s LIMIT 1",required_name,table_name);
    if(result)
    {
        delete result;
        return true;
    }

    // check fail, prepare readabale error message

    // search current required_* field in DB
    const char* db_name;
    if(!strcmp(table_name, "db_version"))
        db_name = "WORLD";
    else if(!strcmp(table_name, "character_db_version"))
        db_name = "CHARACTER";
    else if(!strcmp(table_name, "realmd_db_version"))
        db_name = "REALMD";
    else
        db_name = "UNKNOWN";

    char const* req_sql_update_name = required_name+strlen("required_");

    QueryNamedResult* result2 = PQueryNamed("SELECT * FROM %s LIMIT 1",table_name);
    if(result2)
    {
        QueryFieldNames const& namesMap = result2->GetFieldNames();
        std::string reqName;
        for(QueryFieldNames::const_iterator itr = namesMap.begin(); itr != namesMap.end(); ++itr)
        {
            if(itr->substr(0,9)=="required_")
            {
                reqName = *itr;
                break;
            }
        }

        delete result2;

        std::string cur_sql_update_name = reqName.substr(strlen("required_"),reqName.npos);

        if(!reqName.empty())
        {
            sLog.outErrorDb("The table `%s` in your [%s] database indicates that this database is out of date!",table_name,db_name);
            sLog.outErrorDb("");
            sLog.outErrorDb("  [A] You have: --> `%s.sql`",cur_sql_update_name.c_str());
            sLog.outErrorDb("");
            sLog.outErrorDb("  [B] You need: --> `%s.sql`",req_sql_update_name);
            sLog.outErrorDb("");
            sLog.outErrorDb("You must apply all updates after [A] to [B] to use mangos with this database.");
            sLog.outErrorDb("These updates are included in the sql/updates folder.");
            sLog.outErrorDb("Please read the included [README] in sql/updates for instructions on updating.");
        }
        else
        {
            sLog.outErrorDb("The table `%s` in your [%s] database is missing its version info.",table_name,db_name);
            sLog.outErrorDb("MaNGOS cannot find the version info needed to check that the db is up to date.",table_name,db_name);
            sLog.outErrorDb("");
            sLog.outErrorDb("This revision of MaNGOS requires a database updated to:");
            sLog.outErrorDb("`%s.sql`",req_sql_update_name);
            sLog.outErrorDb("");

            if(!strcmp(db_name, "WORLD"))
                sLog.outErrorDb("Post this error to your database provider forum or find a solution there.");
            else
                sLog.outErrorDb("Reinstall your [%s] database with the included sql file in the sql folder.",db_name);
        }
    }
    else
    {
        sLog.outErrorDb("The table `%s` in your [%s] database is missing or corrupt.",table_name,db_name);
        sLog.outErrorDb("MaNGOS cannot find the version info needed to check that the db is up to date.",table_name,db_name);
        sLog.outErrorDb("");
        sLog.outErrorDb("This revision of mangos requires a database updated to:");
        sLog.outErrorDb("`%s.sql`",req_sql_update_name);
        sLog.outErrorDb("");

        if(!strcmp(db_name, "WORLD"))
            sLog.outErrorDb("Post this error to your database provider forum or find a solution there.");
        else
            sLog.outErrorDb("Reinstall your [%s] database with the included sql file in the sql folder.",db_name);
    }

    return false;
}
