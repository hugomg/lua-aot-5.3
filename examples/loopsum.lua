local function main()
  local a = 0
  for i = 1, 1e9 do
    a = a + i
  end
  return a
end

if magic then
  magic(1, main)
end

print(main())
