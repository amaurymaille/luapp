ensure_value_type(16 >> 4, 1, "int")
ensure_value_type(8 >> 3, 1, "int")
ensure_value_type(4 >> 2, 1, "int")
ensure_value_type(2 >> 1, 1, "int")
ensure_value_type(48 >> 2, 12, "int")

ensure_value_type("2" >> 1, 1, "int")
ensure_value_type(2 >> "1", 1, "int")
ensure_value_type("2" >> "1", 1, "int")

ensure_value_type(2.0 >> 1, 1, "int")
ensure_value_type("2.0" >> 1, 1, "int")
ensure_value_type(2.0 >> "1", 1, "int")
ensure_value_type("2.0" >> "1", 1, "int")

ensure_value_type(2 >> 1.0, 1, "int")
ensure_value_type("2" >> 1.0, 1, "int")
ensure_value_type("2" >> "1.0", 1, "int")
ensure_value_type(2 >> "1.0", 1, "int")

ensure_value_type(2.0 >> 1.0, 1, "int")
ensure_value_type("2.0" >> 1.0, 1, "int")
ensure_value_type(2.0 >> "1.0", 1, "int")
ensure_value_type("2.0" >> "1.0", 1, "int")
