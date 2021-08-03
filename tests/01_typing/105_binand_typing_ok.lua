ensure_value_type(0 & 0, 0, "int")
ensure_value_type(0 & 1, 0, "int")
ensure_value_type(1 & 0, 0, "int")
ensure_value_type(1 & 1, 1, "int")

ensure_value_type(12 & 1, 0, "int")
ensure_value_type("12" & 1, 0, "int")
ensure_value_type("12" & "1", 0, "int")
ensure_value_type("12" & "1", 0, "int")

ensure_value_type(12.0 & 1, 0, "int")
ensure_value_type("12.0" & 1, 0, "int")
ensure_value_type(12.0 & "1", 0, "int")
ensure_value_type("12.0" & "1.0", 0, "int")

ensure_value_type(12 & 1.0, 0, "int")
ensure_value_type(12 & "1.0", 0, "int")
ensure_value_type("12" & 1.0, 0, "int")
ensure_value_type("12" & "1.0", 0, "int")

ensure_value_type(12.0 & 1.0, 0, "int")
ensure_value_type("12.0" & 1.0, 0, "int")
ensure_value_type(12.0 & "1.0", 0, "int")
ensure_value_type("12.0" & "1.0", 0, "int")
