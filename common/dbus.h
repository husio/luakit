/*
 * dbus.h - dbus support
 *
 * Copyright (C) 2010 Piotr Husiatyński <phusiatynski@gmail.com>
 * Copyright (C) 2010 Gregor Uhlenheuer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LUAKIT_COMMON_DBUS_H
#define LUAKIT_COMMON_DBUS_H

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include "luah.h"
#include "common/util.h"

#define LUAKIT_DBUS_BASENAME "org.luakit.dbus"


int luakit_dbus_init(lua_State *L, const char *name);

#endif
