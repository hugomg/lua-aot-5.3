
local function dump(t)
    for k,v in pairs(t) do
        print(k,v)
    end
end

dump({a=1, b=2, c=3})
