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

static int lua_mergetable(lua_State *, int);
static int lua_tabletypes(lua_State *, int, int *, int *);
static int lua_tableisarray(lua_State *, int);
static int lua_tablesize(lua_State *, int);
static int lua_dbus_method_call(lua_State *);
static int lua_dbus_signal(lua_State *);
static const char * lua_get_string_from_table(lua_State *, const char *);

static char *dbus_signature_for_lua_table(lua_State *, int);
static int dbus_message_iter_from_lua(DBusMessageIter *, lua_State *, gint);
static int dbus_container_from_lua_table(DBusMessageIter *, lua_State *, int);
static DBusMessage *dbus_message_response_from_lua(DBusMessage *, lua_State *, gint);
static int dbus_message_arguments_from_lua(DBusMessage *, lua_State *);
static int dbus_message_iter_to_lua(DBusMessageIter *, lua_State *);
static int dbus_message_to_lua(DBusMessage *, lua_State *);
static DBusHandlerResult dbus_signal_filter(DBusConnection *, DBusMessage *, void *);
static char dbus_sign_from_lua_type(int type);


/*
 * Return 1 if Lua table at given index is an array, else 0.  It is considered
 * an array if contains only number indexes that begins with 1 and increments
 * with every value.
 */
static int
lua_tableisarray(lua_State *L, int index)
{
    int i;
    int t_index = index;

    if (t_index < 0) {
        /* absolude stack index for given table */
        t_index = lua_gettop(L) - (index + 1);
    }

    lua_pushnil(L);
    /* lua indexes begins with 1 */
    i = 1;
    while (lua_next(L, t_index)) {
        /* pop only value */
        lua_pop(L, 1);
        /* check both key type and it's value */
        if (!lua_isnumber(L, -1) || i != lua_tonumber(L, -1)) {
            /* end of iteration - remove key from the stack */
            lua_pop(L, 1);
            return 0;
        }
        ++i;
    }
    return 1;
}

/*
 * Return size of numbers indexed lua table.
 *
 * This is very simple check, that iterates from 1, till nil value will be
 * found.
 */
static int
lua_tablesize(lua_State *L, int index)
{
    int size, t_index;

    t_index = index;
    if (t_index < 0) {
        /* absolude stack index for given table */
        t_index = lua_gettop(L) - (index + 1);
    }

    for (size=1; !lua_isnil(L, -1); ++size) {
        lua_pushinteger(L, size);
        lua_gettable(L, t_index);
    }
    lua_pop(L, size - 1);

    return size - 2;
}

/*
 * Get lua table key and value types. Returns 0 on succes or error code if key
 * and/or value type cannot be returned (this is for example when table
 * contains keys/values of more than one type)
 */
static int
lua_tabletypes(lua_State *L, int index, int *key_t, int *val_t)
{
    int t_index = index;

    if (t_index < 0) {
        t_index = lua_gettop(L) - (index + 1);
    }
    /* flush both key and value types */
    *key_t = -1;
    *val_t = -1;

    lua_pushnil(L);
    while (lua_next(L, t_index)) {
        /* if type is already initialized and previous type is different than
         * current one, then we cannot return single type value
         */
        if (*val_t != lua_type(L, -1) && *val_t >= 0) {
            lua_pop(L, 2);
            return -1;
        }
        if (*key_t != lua_type(L, -2) && *key_t >= 0) {
            lua_pop(L, 2);
            return -2;
        }

        *val_t = lua_type(L, -1);
        *key_t = lua_type(L, -2);
        lua_pop(L, 1);
    }
    return 0;
}

/*
 * Get dbus signature byte for given Lua type. Returns 0 if given Lua type
 * cannot be mapped to any dbus signature.
 */
static char
dbus_sign_from_lua_type(int type)
{
    switch (type) {
        case LUA_TSTRING:
            return 's';
        case LUA_TNUMBER:
            return 'i';
        case LUA_TBOOLEAN:
            return 'b';
    }
    return '\0';
}

/*
 * Assing 4 byte signature at given sing poiter, based on lua table at given
 * index. Returns valid dbus signature or raises Lua error and returns NULL.
 *
 * If returned value is not NULL, then caller is responsible for returned
 * value memory deallocation.
 */
static char *
dbus_signature_for_lua_table(lua_State *L, int index)
{
    int ret;
    char key_s, val_s;
    int key_t, val_t;

    if ((ret = lua_tabletypes(L, index, &key_t, &val_t))) {
        luaL_error(L, "Given table cannot be mapped: "
                "contains more than one type for  key or value (%d)", ret);
        return NULL;
    }

    key_s = dbus_sign_from_lua_type(key_t);
    val_s = dbus_sign_from_lua_type(val_t);
    if (key_s == 0 || val_s == 0) {
        lua_pushstring(L, "Unknown table signatures");
        lua_error(L);
        return NULL;
    }
    if (lua_tableisarray(L, index)) {
        /* if given table is number indexed, then it's an array */
        return g_strdup_printf("%c", val_s);
    }

    return g_strdup_printf("{%c%c}", key_s, val_s);
}


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

/*
 * Convert Lua table at given index into dbus container. This supports both
 * arrays and dicts.
 */
static int
dbus_container_from_lua_table(DBusMessageIter *iter, lua_State *L, int index)
{
    DBusMessageIter subiter, subiter_d;
    const char *t_sign = NULL;

    if (index < 0) {
        index = lua_gettop(L) - (index + 1);
    }

    t_sign = dbus_signature_for_lua_table(L, index);
    if (t_sign == NULL) {
        warn("Cannot create signature");
        return -1;
    }

    dbus_message_iter_open_container(iter,
            DBUS_TYPE_ARRAY, t_sign, &subiter);
    lua_pushnil(L);
    if (lua_tableisarray(L, index)) {
        while (lua_next(L, index)) {
            /* simple number indexed array */
            dbus_message_iter_from_lua(&subiter, L, 0);
        }
    } else {
        while (lua_next(L, index)) {
            /* dict tyle */
            dbus_message_iter_open_container(&subiter,
                    DBUS_TYPE_DICT_ENTRY, NULL, &subiter_d);
            lua_pushvalue(L, -2);
            dbus_message_iter_from_lua(&subiter_d, L, 0);
            dbus_message_iter_from_lua(&subiter_d, L, 0);
            dbus_message_iter_close_container(&subiter, &subiter_d);
        }
    }
    dbus_message_iter_close_container(iter, &subiter);
    g_free((void *)t_sign);

    return 0;
}

/*
 * Push (ret_num + 1) number of Lua objects from the top of the stack into
 * given dbus message iterator.
 * This function removes (ret_num + 1) objects from the stack
 */
static int
dbus_message_iter_from_lua(DBusMessageIter *iter, lua_State *L, gint ret_num)
{
    const char *v_string;
    gint32 v_int32;
    dbus_bool_t v_bool;
    gint t_index;

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
            dbus_container_from_lua_table(iter, L, t_index);
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

/*
 * Remove (ret_num + 1) Lua objects from the top of the stack and push them
 * into given dbus message structure.
 */
static DBusMessage *
dbus_message_response_from_lua(DBusMessage *msg, lua_State *L, gint ret_num)
{
    DBusMessage *reply;
    DBusMessageIter iter;

    dbus_message_iter_init(msg, &iter);
    reply = dbus_message_new_method_return(msg);
    dbus_message_iter_init_append(reply, &iter);

    if (dbus_message_iter_from_lua(&iter, L, ret_num)) {
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
        reply = dbus_message_response_from_lua(msg, L, lua_gettop(L) - top);
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
 * Return string value related to given key string from lua table that is on
 * the top of the stack.
 *
 * Calls luaL_error when value does not exist or is not a string.
 */
static const char *
lua_get_string_from_table(lua_State *L, const char *key)
{
    const char *value;

    lua_pushstring(L, key);
    lua_gettable(L, -2);
    if (!lua_isstring(L, -1)) {
        luaL_error(L, "String value required for '%s' key.", key);
    }
    value = lua_tostring(L, -1);
    lua_pop(L, 1);

    return value;
}

/*
 * Attach values from top stack Lua array to given D-BUS message
 *
 * Return 0 on success or error code.
 */
static int
dbus_message_arguments_from_lua(DBusMessage *msg, lua_State *L)
{
    int i, t_size, t_index;
    DBusMessageIter iter;

    if (!lua_istable(L, -1) || !lua_tableisarray(L, -1)) {
        warn("Cannot attach arguments to D-BUS message - array not found.");
        return 1;
    }

    t_index = lua_gettop(L);
    t_size = lua_tablesize(L, t_index);

    debug("attaching parameters to dbus message");

    for (i=t_size; i>0; --i) {
        lua_pushinteger(L, i);
        lua_gettable(L, t_index);
    }

    dbus_message_iter_init(msg, &iter);
    dbus_message_iter_init_append(msg, &iter);

    if (dbus_message_iter_from_lua(&iter, L, t_size - 1)) {
        dbus_message_unref(msg);
        warn("Cannot attach parameters to dbus message.");
        return 2;
    }

    return 0;
}

/*
 * Lua function. Make D-BUS methd call.
 *
 * As the only parameter, array should be given. Required keys are:
 * - dest
 * - path
 * - interface
 * - member
 *
 * Optional message parameter should be array with arguments that given member
 * will be called with.
 */
static int
lua_dbus_method_call(lua_State *L)
{
    DBusMessage *msg;
    dbus_uint32_t serial;
    const char *dest, *path, *interface, *method;

    if (!lua_istable(L, -1)) {
        luaL_error(L, "Single array argument required.");
    }

    /* create dbus message using params from single lua array argument */
    dest = lua_get_string_from_table(L, "dest");
    path = lua_get_string_from_table(L, "path");
    interface = lua_get_string_from_table(L, "interface");
    method = lua_get_string_from_table(L, "method");

    msg = dbus_message_new_method_call(dest, path, interface, method);
    if (msg == NULL) {
        luaL_error(L, "Cannot create dbus message.");
    }

    serial = (dbus_uint32_t) g_random_int();
    dbus_message_set_serial(msg, serial);

    /* if exists, attach message arguments to created dbus message */
    lua_pushstring(L, "message");
    lua_gettable(L, -2);
    dbus_message_arguments_from_lua(msg, L);
    lua_pop(L, 1);

    /* send message and get a handle for a reply */
    debug("dbus method call: %d", serial);
    if (!dbus_connection_send(conn, msg, &serial)) {
        dbus_message_unref(msg);
        luaL_error(L, "Out of memmory.");
    }

    dbus_message_unref(msg);
    return 0;
}

/*
 * Lua function. Send D-BUS signal.
 *
 * As the only parameter, array should be given. Required keys are:
 * - path
 * - interface
 * - name
 *
 * Optional message parameter should be array with arguments that given member
 * will be called with.
 */
static int
lua_dbus_signal(lua_State *L)
{
    DBusMessage *msg;
    dbus_uint32_t serial;
    const char *path, *interface, *sig_name;

    if (!lua_istable(L, -1)) {
        luaL_error(L, "Single array argument required.");
    }

    /* create dbus message using params from single lua array argument */
    path = lua_get_string_from_table(L, "path");
    interface = lua_get_string_from_table(L, "interface");
    sig_name = lua_get_string_from_table(L, "name");

    msg = dbus_message_new_signal(path, interface, sig_name);
    if (msg == NULL) {
        luaL_error(L, "Cannot create dbus message.");
    }

    serial = (dbus_uint32_t) g_random_int();
    dbus_message_set_serial(msg, serial);

    /* if exists, attach message arguments to created dbus message */
    lua_pushstring(L, "message");
    lua_gettable(L, -2);
    dbus_message_arguments_from_lua(msg, L);
    lua_pop(L, 1);

    /* send message and get a handle for a reply */
    debug("dbus signal: %d", serial);
    if (!dbus_connection_send(conn, msg, &serial)) {
        dbus_message_unref(msg);
        luaL_error(L, "Out of memmory.");
    }

    dbus_message_unref(msg);
    return 0;
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

    /* initialize lua dbus module */
    lua_newtable(L);

    lua_pushstring(L, "method_call");
    lua_pushcfunction(L, lua_dbus_method_call);
    lua_settable(L, -3);

    lua_pushstring(L, "signal");
    lua_pushcfunction(L, lua_dbus_signal);
    lua_settable(L, -3);

    /* register new table - dbus module */
    lua_setglobal(L, "dbus");

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
