local function fac(n)
  local r = 1
  while n >= 1 do
    r = r * n
    n = n - 1
  end
  return r
end

if magic then
  magic(1, fac)
end

local r = fac(5)
print(r)
