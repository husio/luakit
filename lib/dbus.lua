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

handlers:add_signal("callback", function (handler, dbus_msg)
    -- echo recived args
    return unpack(dbus_msg.args)
end)
