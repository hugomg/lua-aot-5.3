local function fac(n)
  local r = 1
  while n >= 1 do
    r = r * n
    n = n - 1
  end
  return r
end

print(fac(5))
