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

#ifndef MANGOSSERVER_COMMON_H
#define MANGOSSERVER_COMMON_H

// config.h needs to be included 1st
#ifdef HAVE_CONFIG_H
#ifdef PACKAGE
#undef PACKAGE
#endif //PACKAGE
#ifdef PACKAGE_BUGREPORT
#undef PACKAGE_BUGREPORT
#endif //PACKAGE_BUGREPORT
#ifdef PACKAGE_NAME
#undef PACKAGE_NAME
#endif //PACKAGE_NAME
#ifdef PACKAGE_STRING
#undef PACKAGE_STRING
#endif //PACKAGE_STRING
#ifdef PACKAGE_TARNAME
#undef PACKAGE_TARNAME
#endif //PACKAGE_TARNAME
#ifdef PACKAGE_VERSION
#undef PACKAGE_VERSION
#endif //PACKAGE_VERSION
#ifdef VERSION
#undef VERSION
#endif //VERSION
# include "config.h"
#undef PACKAGE
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#undef VERSION
#endif //HAVE_CONFIG_H

#include "Platform/Define.h"

#if COMPILER == COMPILER_MICROSOFT
#  pragma warning(disable:4996)                             // 'function': was declared deprecated
#ifndef __SHOW_STUPID_WARNINGS__
#  pragma warning(disable:4244)                             // 'argument' : conversion from 'type1' to 'type2', possible loss of data
#  pragma warning(disable:4355)                             // 'this' : used in base member initializer list
#endif                                                      // __SHOW_STUPID_WARNINGS__
#endif                                                      // __GNUC__

// must be the first thing to include for it to work
#include "MemoryLeaks.h"

#include "Utilities/UnorderedMap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>

#if defined(__sun__)
#include <ieeefp.h> // finite() on Solaris
#endif

#include <set>
#include <list>
#include <string>
#include <map>
#include <queue>
#include <sstream>
#include <algorithm>

#include "LockedQueue.h"
#include "Threading.h"

#include <ace/Basic_Types.h>
#include <ace/Guard_T.h>
#include <ace/RW_Thread_Mutex.h>
#include <ace/Thread_Mutex.h>

#if PLATFORM == PLATFORM_WINDOWS
#  define FD_SETSIZE 4096
#  include <ace/config-all.h>
// XP winver - needed to compile with standard leak check in MemoryLeaks.h
// uncomment later if needed
//#define _WIN32_WINNT 0x0501
#  include <ws2tcpip.h>
//#undef WIN32_WINNT
#else
#  include <sys/types.h>
#  include <sys/ioctl.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <unistd.h>
#  include <netdb.h>
#endif

#if COMPILER == COMPILER_MICROSOFT

#  include <float.h>

#  define I32FMT "%08I32X"
#  define I64FMT "%016I64X"
#  define snprintf _snprintf
#  define vsnprintf _vsnprintf
#  define finite(X) _finite(X)

#else

#  define stricmp strcasecmp
#  define strnicmp strncasecmp

#  define I32FMT "%08X"
#  if ACE_SIZEOF_LONG == 8
#    define I64FMT "%016lX"
#  else
#    define I64FMT "%016llX"
#  endif /* ACE_SIZEOF_LONG == 8 */

#endif

#define UI64FMTD ACE_UINT64_FORMAT_SPECIFIER
#define UI64LIT(N) ACE_UINT64_LITERAL(N)

#define SI64FMTD ACE_INT64_FORMAT_SPECIFIER
#define SI64LIT(N) ACE_INT64_LITERAL(N)

#define SIZEFMTD ACE_SIZE_T_FORMAT_SPECIFIER

inline float finiteAlways(float f) { return finite(f) ? f : 0.0f; }

#define atol(a) strtoul( a, NULL, 10)

#define STRINGIZE(a) #a

enum TimeConstants
{
    MINUTE = 60,
    HOUR   = MINUTE*60,
    DAY    = HOUR*24,
    MONTH  = DAY*30,
    YEAR   = MONTH*12,
    IN_MILISECONDS = 1000
};

enum AccountTypes
{
    SEC_PLAYER         = 0,
    SEC_MODERATOR      = 1,
    SEC_GAMEMASTER     = 2,
    SEC_ADMINISTRATOR  = 3,
    SEC_CONSOLE        = 4                                  // must be always last in list, accounts must have less security level always also
};

enum LocaleConstant
{
    LOCALE_enUS = 0,                                        // also enGB
    LOCALE_koKR = 1,
    LOCALE_frFR = 2,
    LOCALE_deDE = 3,
    LOCALE_zhCN = 4,
    LOCALE_zhTW = 5,
    LOCALE_esES = 6,
    LOCALE_esMX = 7,
    LOCALE_ruRU = 8
};

#define MAX_LOCALE 9

LocaleConstant GetLocaleByName(const std::string& name);

extern char const* localeNames[MAX_LOCALE];

struct LocaleNameStr
{
    char const* name;
    LocaleConstant locale;
};

// used for iterate all names including alternative
extern LocaleNameStr fullLocaleNameList[];

//operator new[] based version of strdup() function! Release memory by using operator delete[] !
inline char * mangos_strdup(const char * source)
{
    char * dest = new char[strlen(source) + 1];
    strcpy(dest, source);
    return dest;
}

// we always use stdlibc++ std::max/std::min, undefine some not C++ standard defines (Win API and some pother platforms)
#ifdef max
#  undef max
#endif

#ifdef min
#  undef min
#endif

#ifndef M_PI
#  define M_PI          3.14159265358979323846
#endif

#ifndef M_PI_F
#  define M_PI_F        float(M_PI)
#endif

#endif
