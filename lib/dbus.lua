----------------------------------------------------------------
-- @author Piotr Husiatyński &lt;phusiatynski@gmail.com&gt;   --
----------------------------------------------------------------


local require = require

-- required by debug - remove later
local pairs = pairs
local print = print

module("dbus")

local signal = require "lousy.signal"

handlers = signal.setup({})

handlers:add_signal("show", function (handler, dbus_msg)
        for k, v in pairs(dbus_msg) do
            print(k, v)
        end
    end)
