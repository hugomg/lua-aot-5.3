-- (No shebang line. Choose the interpreter by hand!)

local posix = require 'posix'

local progname = arg[0]
local module_filename = nil
local nrep = nil

local function usage(exit_code)
    io.stderr:write(string.format([[
usage: %s FILE [NREP]

FILE pode ser um módulo em formato ".lua" ou ".so"
NREP por padrão é 1
]], progname))

    os.exit(exit_code)
end

local function basename(path)
    local s = path:match("^.*/(.*)$")
    return (s or path)
end

local function split_ext(filename)
    return filename:match("^(.*)%.(.*)$")
end

-- Get arguments
do
    module_filename = arg[1]
    if not module_filename then
        io.stderr:write("error: FILE is a required argument\n")
        usage(1)
    end

    nrep = arg[2]
    if not nrep then
        nrep = 1
    end
end

local module_name, ext = split_ext(basename(module_filename))

if ext ~= "lua" and ext ~= "so" then
    io.stderr:write('error: file extension must be ".lua" or ".so".\n')
    usage(1)
end

local mainfunc
do
    local ok, err
    if ext == "lua" then
        ok, err = loadfile(module_filename)
    elseif ext == "so" then
        ok, err = package.loadlib(module_filename, 'luaopen_' .. module_name)
    else
        assert(false)
    end

    if ok then
        mainfunc = ok
    else
        io.stderr:write('error: ', err, '\n')
        os.exit(1)
    end
end


local function gettime()
  return { posix.clock_gettime('CLOCK_REALTIME') } -- Wall time
end

local function difftime(tend, tbegin)
    local s_end, ns_end = tend[1], tend[2]
    local s_beg, ns_beg = tbegin[1], tbegin[2]

    if ns_end < ns_beg then
        s_end = s_end - 1
        ns_end = ns_end + 1000000000
    end

    local s_diff = s_end - s_beg
    local ns_diff = ns_end - ns_beg

    return {s_diff, ns_diff}
end


local devnull = io.open("/dev/null", "w")
io.output(devnull)

-- print doesn't respect io.output redirection
local oldprint = print
print = function(...) end 

local t1 = gettime()
for i = 1, nrep do
    mainfunc()
end
local t2 = gettime()
local dt = difftime(t2, t1)

print = oldprint

io.output(io.stdout)

print(string.format("time=%d.%09d", dt[1], math.floor(dt[2])))


