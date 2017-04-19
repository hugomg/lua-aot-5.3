
local f1, f2, f3
local obj

f1 = function()
    return 1 + f2()
end

f2 = function()
    return f3(2)
end

f3 = function(n)
    local function f4()
        return n * (obj:foo())
    end
    if magic then magic(4, f4) end
    return f4()
end

obj = {
    x = 10,
    foo = function(self)
        return self.x
    end
}

print(f1())

