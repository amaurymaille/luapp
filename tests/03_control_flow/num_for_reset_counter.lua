local a = 0
for i = 1, 10 do
    a = a + 1
    i = 10
end
ensure_value_type(a, 10, "int")
