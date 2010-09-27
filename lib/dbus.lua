----------------------------------------------------------------
-- @author Piotr Husiatyński &lt;phusiatynski@gmail.com&gt;   --
----------------------------------------------------------------


-- required by debug - remove later
local pairs = pairs
local print = print


module("dbus")


-- register new handlers here, using message member name as callback keys
handlers = {}


-- dummy debug callback - just show us what you've got
function handlers.show(msg)
    for k, v in pairs(msg) do
        print(k, v)
    end
end

-- main dbus calls handler
function main_handler(mgs)
    local callback = handlers[msg.member];
    if callback then
        callback(msg);
    end
end
