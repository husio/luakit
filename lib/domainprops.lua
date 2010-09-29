----------------------------------------------------------------
-- Domain domainprops dynamic configuration                   --
-- @author Piotr Husiatyński &lt;phusiatynski@gmail.com&gt;   --
----------------------------------------------------------------


local io = io
local string = string
local pairs = pairs
local ipairs = ipairs
local tostring = tostring
local tonumber = tonumber
local string = string
local error = error

local capi = { luakit = luakit }
local lousy = {
    util = require "lousy.util"
}

module("domainprops")

local domain_props = domain_props or {}
local domain_props_path = capi.luakit.data_dir .. '/domainprops'


--- Convert any value to boolean. 0, "0", false and "false" are being
--- converted to false. Everything else to true.
-- @param value value to convert
local function toboolean(value)
    local false_values = {0, "0", false, "false", nil}

    for _, v in ipairs(false_values) do
        if value == v then return false end
    end
    return true
end

-- all allowed property converters
local property_types = {
    ["enable-scripts"] = toboolean,
    ["enable-plugins"] = toboolean,
    ["enable-private-browsing"] = toboolean,
    ["accept-language"] = tostring,
    ["accept-language-auto"] = toboolean,
    ["accept-policy"] = tonumber,
    ["auto-load-images"] = toboolean,
    ["auto-resize-window"] = toboolean,
    ["auto-shrink-images"] = toboolean,
    ["cursive-font-family"] = tostring,
    ["custom-encoding"] = tostring,
    ["default-encoding"] = tostring,
    ["default-font-family"] = tostring,
    ["default-font-size"] = tonumber,
    ["default-monospace-font-size"] = tonumber,
    ["editable"] = toboolean,
    ["enable-caret-browsing"] = toboolean,
    ["enable-default-context-menu"] = toboolean,
    ["enable-developer-extras"] = toboolean,
    ["enable-dom-paste"] = toboolean,
    ["enable-file-access-from-file-uris"] = toboolean,
    ["enable-html5-database"] = toboolean,
    ["enable-html5-local-storage"] = toboolean,
    ["enable-java-applet"] = toboolean,
    ["enable-offline-web-application-cache"] = toboolean,
    ["enable-page-cache"] = toboolean,
    ["enable-plugins"] = toboolean,
    ["enable-private-browsing"] = toboolean,
    ["enable-scripts"] = toboolean,
    ["enable-site-specific-quirks"] = toboolean,
    ["enable-spatial-navigation"] = toboolean,
    ["enable-spell-checking"] = toboolean,
    ["enable-universal-access-from-file-uris"] = toboolean,
    ["enable-xss-auditor"] = toboolean,
    ["encoding"] = tostring,
    ["enforce-96-dpi"] = toboolean,
    ["fantasy-font-family"] = tostring,
    ["full-content-zoom"] = toboolean,
    ["icon-uri"] = tostring,
    ["idle-timeout"] = tonumber,
    ["javascript-can-access-clipboard"] = toboolean,
    ["javascript-can-open-windows-automatically"] = toboolean,
    ["max-conns"] = tonumber,
    ["max-conns-per-host"] = tonumber,
    ["minimum-font-size"] = tonumber,
    ["minimum-logical-font-size"] = tonumber,
    ["monospace-font-family"] = tostring,
    ["print-backgrounds"] = toboolean,
    ["progress"] = tonumber,
    ["proxy-uri"] = tostring,
    ["resizable-text-areas"] = toboolean,
    ["sans-serif-font-family"] = tostring,
    ["serif-font-family"] = tostring,
    ["spell-checking-languages"] = tostring,
    ["ssl-ca-file"] = tostring,
    ["ssl-strict"] = toboolean,
    ["tab-key-cycles-through-elements"] = tonumber,
    ["timeout"] = tonumber,
    ["title"] = tostring,
    ["transparent"] = toboolean,
    ["use-ntlm"] = toboolean,
    ["user-agent"] = tostring,
    ["user-stylesheet-uri"] = tostring,
    ["zoom-level"] = tonumber,
    ["zoom-step"] = tonumber,
}

--- Load domain properties from file
-- @param path domain properties main directory of nil to use default
function load(path)
    domain_props = domain_props or {}
    path = path or domain_props_path

    local fd = io.popen("ls " .. path)
    for domain in fd:lines() do
        domain_props[domain] = {}
        local fd_domain = path .. "/" .. domain
        for line in io.lines(fd_domain) do
            local name, value = string.match(
                lousy.util.string.strip(line), "^([%w\-]+)%s+(.+)$")
            name = lousy.util.string.strip(name)

            -- ignore invalid properties
            if property_types[name] ~= nil then
                -- call conversion function on fetched value
                local value_conv = property_types[name](value)
                if value_conv == nil then
                    error(string.format(
                           "Bad domain property value: %s - %s", domain, line))
                else
                    domain_props[domain][name] = value_conv
                end
            else
                error(string.format(
                        "Unknown property: %s - %s", domain, line))
            end
        end
    end
    io.close(fd)

    return domain_props
end


--- Save domain properties on disc
-- @param path domain properties main directory of nil to use default
function save(path)
    if not domain_props then load() end
    local path = path or domain_props_path

    lousy.util.mkdir(path)

    -- save every domain props in different file
    for domain, props in pairs(domain_props) do
        local fd_name = path .. '/' .. domain
        local fd = io.open(fd_name, "w")
        for name, value in pairs(props) do
            fd:write(string.format("%s %s\n", name, tostring(value)))
        end
        io.close(fd)
    end
end


--- Set domain property for given domain
--- TODO - 2010-09-29 at 22:50
--- load/save only for single domain file
-- @param domain domain namespace for given property or "all" if global
-- @param property_name name of the property
-- @param value value of the property
-- @param load_file if not nil, update domain properties before save
-- @param save_file if not nil, save to file after setup
function property_set(domain, property_name, value, load_file, save_file)
    if load_file ~= false then load() end

    if property_types[property_name] == nil then
        error("bad property name:" .. property_name)
        return
    end

    if not domain_props[domain] then domain_props[domain] = {} end
    -- always convert to proper type before setup
    domain_props[domain][property_name] = property_types[property_name](value)

    if save_file ~= false then save() end
end

--- Return property value for given domain or nil if does not set
-- @param domain domain namespace for given property
-- @param property_name name of the property
function property_get(domain, property_name)
    if domain_props[domain] then
        return domain_props[domain][property_name]
    end
    return nil
end

--- Return configuration for given domain (empty table if does not exist).
--- Always provide domain specyfic configuration by extending "all"
-- @param domain domain name
function domain_get(domain)
    local all_conf = domain_props.all or {}
    local domain_conf = domain_props[domain] or {}
    -- merge tables, overwriting "all" configuration with domain specyfic
    return lousy.util.table.join(all_conf, domain_conf)
end
