/*
 * dbus.c - dbus support
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

#include "dbus.h"



static DBusError err;
static DBusConnection *conn = NULL;


/*
 * Merge table from top of the stack into table at given index.
 *
 * {x={1=a, 2=b}, y=z..} => {a=b, y=z}
 */
static void
lua_mergetable(lua_State *L, int merge_to)
{
    gint npop = 0;
    gint stack_top = lua_gettop(L);

    lua_pushnil(L);
    while (lua_next(L, stack_top)) {
        ++npop;
        lua_insert(L, -2);
    };
    if (npop != 2) {
        fatal("Bad table format");
    }
    lua_settable(L, merge_to);
    /* remove merged table - it should be now empty */
    lua_pop(L, 1);
}

/*
 * Iter over given dbus message iterator object and push values into lua state
 * stack as table. Recursive.
 * Return 0 on success or error code.
 */
static int
dbus_message_iter_to_lua(DBusMessageIter *iter, lua_State *L)
{
    DBusMessageIter subiter;
    dbus_bool_t arg_bool;
    gchar *arg_string;
    gint32 arg_int32;
    /* lua index begins with 1 */
    gint t_next = 1;

    lua_newtable(L);

    do {
        switch(dbus_message_iter_get_arg_type(iter)) {
        case DBUS_TYPE_INVALID:
            break;
        case DBUS_TYPE_BOOLEAN:
            dbus_message_iter_get_basic(iter, &arg_bool);
            lua_pushinteger(L, t_next);
            lua_pushboolean(L, arg_bool);
            lua_settable(L, -3);
            ++t_next;
            break;
        case DBUS_TYPE_STRING:
            dbus_message_iter_get_basic(iter, &arg_string);
            lua_pushinteger(L, t_next);
            lua_pushstring(L, arg_string);
            lua_settable(L, -3);
            ++t_next;
            break;
        case DBUS_TYPE_INT32:
            dbus_message_iter_get_basic(iter, &arg_int32);
            lua_pushinteger(L, t_next);
            lua_pushinteger(L, arg_int32);
            lua_settable(L, -3);
            ++t_next;
            break;
        case DBUS_TYPE_BYTE:
            break;
        case DBUS_TYPE_ARRAY:
            lua_pushinteger(L, t_next);
            dbus_message_iter_recurse(iter, &subiter);
            dbus_message_iter_to_lua(&subiter, L);
            lua_settable(L, -3);
            ++t_next;
            break;
        case DBUS_TYPE_DICT_ENTRY:
            dbus_message_iter_recurse(iter, &subiter);
            dbus_message_iter_to_lua(&subiter, L);
            /* rebuild current table by mering latest table */
            lua_mergetable(L, lua_gettop(L) - 1);
            --t_next;
            break;
        default:
            break;
        }
    } while (dbus_message_iter_next(iter));

    return 0;
}

/*
 * Iter over given dbus message arguments and push them into given lua state
 * stack as table
 * Return 0 on success or error code.
 */
static int
dbus_message_to_lua(DBusMessage *msg, lua_State *L)
{
    DBusMessageIter iter;

    dbus_message_iter_init(msg, &iter);
    return dbus_message_iter_to_lua(&iter, L);
}

/*
 * Main dbus signals filter. Convert dbus message into lua table and call
 * related callback function if defined.
 */
static DBusHandlerResult
dbus_signal_filter(DBusConnection *c, DBusMessage *msg, void *data)
{
    (void) c;
    lua_State *L = (lua_State *)data;

    g_return_val_if_fail(L, DBUS_HANDLER_RESULT_HANDLED);

    /* prepare Lua for dbus.handler:emit_signal call */
    lua_getfield(L, LUA_GLOBALSINDEX, "dbus");
    if (!lua_istable(L, -1)) {
        warn("dbus module not found");
        lua_pop(L, 1);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    lua_getfield(L, -1, "handlers");
    if (!lua_istable(L, lua_gettop(L))) {
        warn("dbus.handlers *table* not found");
        lua_pop(L, 2);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    lua_getfield(L, -1, "emit_signal");
    if (!lua_isfunction(L, lua_gettop(L))) {
        warn("dbus.handler.emit_signal function not found");
        lua_pop(L, 3);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    /* this is method call, so put self on stack */
    lua_getfield(L, -3, "handlers");

    lua_pushstring(L, dbus_message_get_member(msg));

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
    dbus_message_to_lua(msg, L);
    lua_setfield(L, -2, "args");

    if (lua_pcall(L, 3, 0, 0)) {
        g_fprintf(stderr, "%s\n", lua_tostring(L, -1));
    }

    /* remove dbus.handlers from the stack */
    lua_pop(L, 2);

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
