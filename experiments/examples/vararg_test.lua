
local function foo(...)
    local a, b, c = ...
    return a + b + c
end

if magic then
    magic(1, foo)
end

print(foo(1,2,3,4))
