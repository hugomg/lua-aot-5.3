local function magic(n)
    if n <= 1 then
        return 1
    else
        return magic(n-1) + magic(n-2)
    end
end

print(magic(8))
