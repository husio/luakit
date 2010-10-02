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
 * Merge Lua table from top of the stack into table at given index.
 *
 * {x={1=a, 2=b}, y=z..}  =>  {a=b, y=z}
 *
 * Return 0 on success or error code. Always removes table from the top of the
 * stack.
 */
static int
lua_mergetable(lua_State *L, int merge_to)
{
    gint stack_top = lua_gettop(L);

    lua_pushnil(L);
    while (lua_next(L, stack_top)) {
        lua_insert(L, -2);
    };
    if (lua_gettop(L) - stack_top != 2) {
        /* cleanup the stack */
        lua_pop(L, lua_gettop(L) - stack_top);
        return 1;
    }
    lua_settable(L, merge_to);
    /* remove merged table - it should be now empty */
    lua_pop(L, 1);

    return 0;
}

static int
dbus_reply_iter_from_lua(DBusMessageIter *iter, lua_State *L, gint ret_num)
{
    DBusMessageIter subiter, subiter_d;
    const char *v_string;
    gint32 v_int32;
    dbus_bool_t v_bool;
    gint t_index;
    const char *t_sign;

    for (;ret_num >= 0; --ret_num) {
        switch (lua_type(L, -1)) {
        case LUA_TSTRING:
            v_string = lua_tostring(L, -1);
            dbus_message_iter_append_basic(iter,
                    DBUS_TYPE_STRING, &v_string);
            break;
        case LUA_TNUMBER:
            v_int32 = lua_tointeger(L, -1);
            dbus_message_iter_append_basic(iter,
                    DBUS_TYPE_INT32, &v_int32);
            break;
        case LUA_TBOOLEAN:
            v_bool = lua_toboolean(L, -1);
            dbus_message_iter_append_basic(iter,
                    DBUS_TYPE_BOOLEAN, &v_bool);
            break;
        case LUA_TTABLE:
            t_index = lua_gettop(L);
            /* currently only strgin-string signature is supported */
            t_sign = "{ss}";
            dbus_message_iter_open_container(iter,
                    DBUS_TYPE_ARRAY, t_sign, &subiter);
            lua_pushnil(L);
            while (lua_next(L, t_index)) {
                dbus_message_iter_open_container(&subiter,
                        DBUS_TYPE_DICT_ENTRY, NULL, &subiter_d);
                v_string = lua_tostring(L, -1);
                dbus_message_iter_append_basic(&subiter_d,
                        DBUS_TYPE_STRING, &v_string);
                lua_pop(L, 1);
                lua_pushvalue(L, -1);
                v_string = lua_tostring(L, -1);
                dbus_message_iter_append_basic(&subiter_d,
                        DBUS_TYPE_STRING, &v_string);
                lua_pop(L, 1);
                dbus_message_iter_close_container(&subiter, &subiter_d);
            }
            dbus_message_iter_close_container(iter, &subiter);
            break;
        default:
            warn("Unsupported type return: %s",
                    lua_typename(L, lua_type(L, -1)));
            v_string = g_strdup_printf(
                    "cannot_convert:%s", lua_typename(L, lua_type(L, -1)));
            dbus_message_iter_append_basic(iter,
                    DBUS_TYPE_STRING, &v_string);
            g_free((void *)v_string);
        }

        lua_pop(L, 1);
    }

    return 0;
}

static DBusMessage *
dbus_reply_from_lua(DBusMessage *msg, lua_State *L, gint ret_num)
{
    DBusMessage *reply;
    DBusMessageIter iter;

    dbus_message_iter_init(msg, &iter);
    reply = dbus_message_new_method_return(msg);
    dbus_message_iter_init_append(reply, &iter);

    if (dbus_reply_iter_from_lua(&iter, L, ret_num)) {
        dbus_message_unref(reply);
        return NULL;
    }

    return reply;
}

/*
 * Iter using given dbus message iterator object and push values into given
 * Lua stack as table.
 * Return 0 on success or error code.
 */
static int
dbus_message_iter_to_lua(DBusMessageIter *iter, lua_State *L)
{
    DBusMessageIter subiter;
    dbus_bool_t arg_bool;
    const gchar *arg_string;
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
            warn("Ignoring unsupported type: %d",
                    dbus_message_iter_get_arg_type(iter));
            lua_pushinteger(L, t_next);
            lua_pushstring(L, "unsupported_type");
            lua_settable(L, -3);
            ++t_next;
            break;
        }
    } while (dbus_message_iter_next(iter));

    return 0;
}

/*
 * Iter over given dbus message arguments and push them into given Lua state
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
 * Main dbus signals filter. Convert dbus message into Lua table and emit
 * related signal.
 */
static DBusHandlerResult
dbus_signal_filter(DBusConnection *c, DBusMessage *msg, void *data)
{
    lua_State *L = (lua_State *)data;
    gint top;
    DBusMessage *reply = NULL;

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
    top = lua_gettop(L);

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

    if (lua_pcall(L, 3, LUA_MULTRET, 0)) {
        g_fprintf(stderr, "%s\n", lua_tostring(L, -1));
    }

    /* if this is method call, create response based on Lua function call
     * result
     */
    if (!dbus_message_get_no_reply(msg)) {
        /* lua_gettop(L) - top == number of retuner arguments */
        reply = dbus_reply_from_lua(msg, L, lua_gettop(L) - top);
    }

    /* remove dbus.handlers from the stack */
    lua_pop(L, 2);

    if (reply) {
        dbus_connection_send(c, reply, NULL);
        dbus_message_unref(reply);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

/*
 * Initialize dbus environment - build session name, connect to daemon and setup
 * signal filter. Return 0 on success or error code.
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
