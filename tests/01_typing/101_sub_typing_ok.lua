ensure_value_type(2 - 3, -1, "int")
ensure_value_type("2" - 3, -1.0, "double")
ensure_value_type(2 - "3", -1.0, "double")
ensure_value_type("2" - "3", -1.0, "double")

ensure_value_type(2 - 3.0, -1.0, "double")
ensure_value_type("2" - 3.0, -1.0, "double")
ensure_value_type(2 - "3.0", -1.0, "double")
ensure_value_type("2" - "3.0", -1.0, "double")

ensure_value_type(2.0 - 3, -1.0, "double")
ensure_value_type("2.0" - 3, -1.0, "double")
ensure_value_type(2.0 - "3", -1.0, "double")
ensure_value_type("2.0" - "3", -1.0, "double")

ensure_value_type(2.5 - 1.5, 1.0, "double")
ensure_value_type("2.5" - 1.5, 1.0, "double")
ensure_value_type(2.5 - "1.5", 1.0, "double")
ensure_value_type("2.5" - "1.5", 1.0, "double")
