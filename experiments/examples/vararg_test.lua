
local function foo(...)
    local a, b, c = ...
    return a + b + c
end

print(foo(1,2,3,4))
