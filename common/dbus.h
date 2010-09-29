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

#ifndef __LUAKIT_DBUS__
#define __LUAKIT_DBUS__

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include "luah.h"
#include "common/util.h"

#define LUAKIT_DBUS_BASENAME "org.luakit.dbus"


static DBusError err;
static DBusConnection *conn = NULL;


/*
 * Main dbus signals filter. Convert dbus message into lua table and call
 * related callback function if defined.
 */
static DBusHandlerResult
dbus_signal_filter(DBusConnection *c, DBusMessage *msg, void *data)
{
    (void) c;
    lua_State *L = (lua_State *)data;
    const char *arg;

    g_return_val_if_fail(L, DBUS_HANDLER_RESULT_HANDLED);

    /* prepare Lua for dbus.handler call */
    lua_getfield(L, LUA_GLOBALSINDEX, "dbus");
    if (!lua_istable(L, -1)) {
        g_error("dbus module not found\n");
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    lua_getfield(L, -1, "main_handler");
    if (!lua_isfunction(L, lua_gettop(L))) {
        g_error("dbus.main_handler callback not found\n");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID);
    if (dbus_error_is_set(&err)) {
        g_error("D-BUS message error: %s\n", err.message);
        dbus_error_free(&err);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    /* create table with some useful dbus data */
    lua_newtable(L);

    switch(dbus_message_get_type(msg))
    {
      case DBUS_MESSAGE_TYPE_SIGNAL:
        lua_pushliteral(L, "signal");
        break;
      case DBUS_MESSAGE_TYPE_METHOD_CALL:
        lua_pushliteral(L, "method_call");
        break;
      case DBUS_MESSAGE_TYPE_ERROR:
        lua_pushliteral(L, "error");
        break;
      case DBUS_MESSAGE_TYPE_METHOD_RETURN:
        lua_pushliteral(L, "method_return");
        break;
      default:
        lua_pushliteral(L, "unknown");
        break;
    }
    lua_setfield(L, -2, "type");

    lua_pushstring(L, dbus_message_get_interface(msg));
    lua_setfield(L, -2, "interface");
    lua_pushstring(L, dbus_message_get_path(msg));
    lua_setfield(L, -2, "path");
    lua_pushstring(L, dbus_message_get_member(msg));
    lua_setfield(L, -2, "member");
    lua_pushstring(L, arg);
    lua_setfield(L, -2, "arg");

    lua_call(L, 1, 0);

    return DBUS_HANDLER_RESULT_HANDLED;
}

/*
 * Initialize dbus environment - build session name, connect to daemon and setup
 * signal filter. On any error, call `g_error`.
 */
int
luakit_dbus_init(lua_State *L, const char *name)
{
    char *dbus_name, *dbus_matcher;

    dbus_name = g_strdup_printf("%s.%s", LUAKIT_DBUS_BASENAME, name);
    dbus_matcher = g_strdup_printf("type='signal',interface='%s'", dbus_name);

    debug("DBUS name: %s", dbus_name);
    debug("DBUS matcher: %s", dbus_matcher);

    dbus_error_init(&err);

    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if(dbus_error_is_set(&err)) {
        goto dbus_err;
    }

    dbus_connection_set_exit_on_disconnect(conn, FALSE);

    dbus_bus_request_name(conn, dbus_name, 0, &err);
    if(dbus_error_is_set(&err)) {
        goto dbus_err;
    }

    dbus_bus_add_match(conn, dbus_matcher, &err);
    if (dbus_error_is_set(&err)) {
        goto dbus_err;
    }

    dbus_connection_add_filter(conn, dbus_signal_filter, L, NULL);
    if (dbus_error_is_set(&err)) {
        goto dbus_err;
    }

    /* allow it to work with glib loop */
    dbus_connection_setup_with_g_main(conn, NULL);

    g_free(dbus_name);
    g_free(dbus_matcher);
    return 0;

dbus_err:
    g_error("D-BUS error: %s\n", err.message);
    dbus_error_free(&err);
    dbus_connection_unref(conn);
    conn = NULL;
    g_free(dbus_name);
    g_free(dbus_matcher);
    return 1;
}

#endif /* __LUAKIT_DBUS__ */
