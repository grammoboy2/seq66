/*
 *  This file is part of seq66.
 *
 *  seq66 is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  seq66 is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with seq66; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * \file          smfunctions.cpp
 *
 *  This module declares/defines a module for managing a generic seq66 session.
 *
 * \library       seq66 application
 * \author        Chris Ahlstrom
 * \date          2020-04-15
 * \updates       2020-04-17
 * \license       GNU GPLv2 or above
 *
 */

#include <cstring>                          /* strlen()                     */

#include "util/basic_macros.h"              /* not_nullptr()                */
#include "sessions/smfunctions.hpp"         /* seq66::smanager()            */

/*
 *  Do not document a namespace; it breaks Doxygen.
 */

namespace seq66
{

/**
 *  See if there is session-manager "present" on the host computer.
 */

std::string
get_session_url (const std::string & env_value)
{
    std::string result;
#if defined _GNU_SOURCE
    char * url = secure_getenv(env_value.c_str());
#else
    char * url = getenv(env_value.c_str());
#endif
    if (not_nullptr(url) && strlen(url) > 0)
        result = std::string(url);

    return result;
}

}           // namespace seq66

/*
 * smfunctions.cpp
 *
 * vim: sw=4 ts=4 wm=4 et ft=cpp
 */

