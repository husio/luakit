/*
 * dbus.h - dbus support
 *
 *
 */
#ifndef __LUAKIT_DBUS__
#define __LUAKIT_DBUS__

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include "luah.h"

#define LUAKIT_DBUS_MAX_CALLBACKS 1024
#define LUAKIT_DBUS_BASENAME "org.luakit.dbus"
#define LUAKIT_DBUS_BASEPATH "/org/luakit/dbus"

/*
 * TODO
 *
 * Dbus support is optional for every luakit instance
 *
 * Running dbus requires name argument, that will be added to dbus path
 *
 * List of dbus handlers function defined in lua as number indexed table
 *
 */

static DBusError err;
static DBusConnection *conn = NULL;

/*
 * Main dbus singnals filter. Convert dbus message into lua table and call
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
        dbus_error_free(&err);
        g_error("D-BUS message error: %s\n", err.message);
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
 * Initialize dbus environ - build session name, connect to daemon, setup
 * singnal filter. On any error, call `g_error`.
 */
int
luakit_dbus_init(lua_State *L, const char *name)
{
    char dbus_name[1024];

    memset(dbus_name, '\0', 1024);
    g_snprintf(dbus_name, 1023, "%s.%s", LUAKIT_DBUS_BASENAME, name);

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

    dbus_connection_add_filter(conn, dbus_signal_filter, L, NULL);
    if (dbus_error_is_set(&err)) {
        goto dbus_err;
    }

    /* allow it to work with glib loop */
    dbus_connection_setup_with_g_main(conn, NULL);

    return 0;

dbus_err:
    g_error("D-BUS error: %s\n", err.message);
    dbus_error_free(&err);
    dbus_connection_unref(conn);
    conn = NULL;
    return 1;
}

#endif /* __LUAKIT_DBUS__ */
