local function magic(n)
  local t = {}
  for i=1,n do
    t[i] = i*10
  end
  return t
end

local x = magic(10)
for i=1,#x do
  print(x[i])
end
