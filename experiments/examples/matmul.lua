-- LLL - Lua Low Level
-- September, 2015
-- Author: Gabriel de Quadros Ligneul
-- Copyright Notice for LLL: see lllcore.h

local SIZE = 250

-- Create the matrices
local A = {}
local B = {}
local C = {}
for i = 1, SIZE do
    A[i] = {}
    B[i] = {}
    C[i] = {}
    for j = 1, SIZE do
        A[i][j] = i + j
        B[i][j] = i - j
        C[i][j] = 0
    end
end

-- Performs the multiplication
for i = 1, SIZE do
    for j = 1, SIZE do
        local val = 0
        for k = 1, SIZE do
            val = val + A[i][k] * B[k][j]
        end
        C[i][j] = val
    end
end
