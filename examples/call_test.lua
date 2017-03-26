
local f1, f2, f3

f1 = function()
    return 1 + f2()
end

f2 = function()
    return f3(2)
end

f3 = function(n)
    return 10 * n
end

if magic then
    magic(1, f1)
    magic(2, f2)
end

print(f1())

