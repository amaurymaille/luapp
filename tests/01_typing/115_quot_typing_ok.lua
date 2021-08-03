ensure_value_type(10 // 3, 3, "int")
ensure_value_type("10" // 3, 3, "double")
ensure_value_type("10" // "3", 3, "double")
ensure_value_type(10 // "3", 3, "double")

ensure_value_type(10.0 // 3, 3.0, "double")
ensure_value_type("10.0" // 3, 3.0, "double")
ensure_value_type("10.0" // "3", 3.0, "double")
ensure_value_type(10.0 // "3", 3.0, "double")

ensure_value_type(10 // 3.0, 3.0, "double")
ensure_value_type("10" // 3.0, 3.0, "double")
ensure_value_type("10" // "3.0", 3.0, "double")
ensure_value_type(10 // "3.0", 3.0, "double")

ensure_value_type(10.0 // 3.0, 3.0, "double")
ensure_value_type("10.0" // 3.0, 3.0, "double")
ensure_value_type("10.0" // "3.0", 3.0, "double")
ensure_value_type(10.0 // "3.0", 3.0, "double")
