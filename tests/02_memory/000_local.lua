local a = 12
ensure_value_type(a, 12, "int")
memory()

local a, b = 14, 15
ensure_value_type(a, 14, "int")
ensure_value_type(b, 15, "int")
memory()

a, b = b, a
memory()
ensure_value_type(a, 15, "int")
ensure_value_type(b, 14, "int")

a, c = 12, 13
memory()
ensure_value_type(a, 12, "int")
ensure_value_type(c, 13, "int")

local c = 14
memory()
c = 19
memory()
