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

#ifndef __SQLDELAYTHREAD_H
#define __SQLDELAYTHREAD_H

#include "ace/Thread_Mutex.h"
#include "LockedQueue.h"
#include "Threading.h"


class Database;
class SqlOperation;

class SqlDelayThread : public ACE_Based::Runnable
{
    typedef ACE_Based::LockedQueue<SqlOperation*, ACE_Thread_Mutex> SqlQueue;

    private:
        SqlQueue m_sqlQueue;                                ///< Queue of SQL statements
        Database* m_dbEngine;                               ///< Pointer to used Database engine
        volatile bool m_running;

        SqlDelayThread();
    public:
        SqlDelayThread(Database* db);

        ///< Put sql statement to delay queue
        bool Delay(SqlOperation* sql) { m_sqlQueue.add(sql); return true; }

        virtual void Stop();                                ///< Stop event
        virtual void run();                                 ///< Main Thread loop
};
#endif                                                      //__SQLDELAYTHREAD_H
