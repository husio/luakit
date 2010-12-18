----------------------------------------------------------------
-- @author Piotr Husiatyński &lt;phusiatynski@gmail.com&gt;   --
----------------------------------------------------------------


local require = require

-- required by debug - remove later
local pairs = pairs
local print = print
local type = type
local table = table
local tostring = tostring
local string = string
local unpack = unpack
local dbus = dbus

module("dbus")

local signal = require "lousy.signal"

handlers = signal.setup({})

handlers:add_signal("show", function (handler, dbus_msg)
    local function show_table(t, prefix)
        prefix = prefix or ''
        for k, v in pairs(t) do
            if type(v) == "table" then
                if #prefix > 0 then k = string.format("%s.%s", prefix, k) end
                show_table(v, tostring(k))
            else
                if #prefix > 0 then k = string.format("%s.%s", prefix, k) end
                print(string.format('%s = %s', k, tostring(v)))
            end
        end
    end

    print('------- dbus message ----------')
    show_table(dbus_msg)
    print('-------------------------------')
end)

dbus.method_call({
    dest='org.freedesktop.Notifications',
    path='/org/freedesktop/Notifications',
    interface='org.freedesktop.Notifications',
    method='Notify',
    message={
        "app_name",
        0,
        "app_icon",
        "Greetings from luakit!",
        "This is test method call, send strait from luakit, using dbus",
        '',
        '',
        5000
    }
})

-- echo
handlers:add_signal("echo", function (handler, dbus_msg)
    print('echo signal: ', unpack(dbus_msg.args))
    return unpack(dbus_msg.args)
end)


dbus.method_call({
    dest='org.luakit.dbus.luakit',
    path='/org/luakit/dbus/luakit',
    interface='org.luakit.dbus.luakit',
    method='echo',
    callback=function (a, b)
        print("method call callback result: ", a, b)
    end,
    message={42, "Test method call message"}
})

dbus.signal({
    path='/',
    interface='org.luakit.dbus.luakit',
    name='show',
    message={"Test signal from luakit"}
})

