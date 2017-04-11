-- LLL - Lua Low Level
-- September, 2015
-- Author: Gabriel de Quadros Ligneul
-- Copyright Notice for LLL: see lllcore.h

local function main()
    local a = 0
    for i = 1, 1e8 do
        a = a + i
    end
end

if magic then
    magic(1, main)
end

main()
