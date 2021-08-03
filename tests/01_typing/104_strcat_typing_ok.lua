ensure_value_type("a" .. "b", "ab", "string")
ensure_value_type(1 .. "b", "1b", "string")
ensure_value_type("a" .. "1", "a1", "string")
ensure_value_type(1 .. 2, "12", "string")
ensure_value_type(1.2 .. "b", "1.2b", "string")
ensure_value_type("a" .. 1.2, "a1.2", "string")
ensure_value_type(1.2 .. 1.3, "1.21.3", "string")
