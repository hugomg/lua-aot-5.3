-- LLL - Lua Low Level
-- September, 2015
-- Author: Gabriel de Quadros Ligneul
-- Copyright Notice for LLL: see lllcore.h
--
-- matmul.lua

return function(f)
    if lll then
        lll.setAutoCompileEnable(false)
    end
    if arg[1] == '--lll' then
        assert(lll.compile(f))
        f()
    elseif arg[1] == '--lll-compile-only' then
        assert(lll.compile(f))
    elseif arg[1] == '--dump' then
        assert(lll.compile(f))
        lll.dump(f)
    else
        f()
    end
end
