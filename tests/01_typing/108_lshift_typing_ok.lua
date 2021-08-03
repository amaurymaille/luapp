ensure_value_type(1 << 1, 2, "int")
ensure_value_type(1 << 2, 4, "int")
ensure_value_type(1 << 3, 8, "int")
ensure_value_type(1 << 4, 16, "int")
ensure_value_type(12 << 2, 48, "int")

ensure_value_type("1" << 1, 2, "int")
ensure_value_type(1 << "1", 2, "int")
ensure_value_type("1" << "1", 2, "int")

ensure_value_type(1.0 << 1, 2, "int")
ensure_value_type("1.0" << 1, 2, "int")
ensure_value_type(1.0 << "1", 2, "int")
ensure_value_type("1.0" << "1", 2, "int")

ensure_value_type(1 << 1.0, 2, "int")
ensure_value_type("1" << 1.0, 2, "int")
ensure_value_type(1 << "1.0", 2, "int")
ensure_value_type("1" << "1.0", 2, "int")

ensure_value_type(1.0 << 1.0, 2, "int")
ensure_value_type("1.0" << 1.0, 2, "int")
ensure_value_type(1.0 << "1.0", 2, "int")
ensure_value_type("1.0" << "1.0", 2, "int")
