ensure_value_type(2 + 3, 5, "int")
ensure_value_type("2" + 3, 5.0, "double")
ensure_value_type(2 + "3", 5.0, "double")
ensure_value_type("2" + "3", 5.0, "double")

ensure_value_type(2.0 + 3, 5.0, "double")
ensure_value_type("2.0" + 3, 5.0, "double")
ensure_value_type(2.0 + "3", 5.0, "double")
ensure_value_type("2.0" + "3", 5.0, "double")

ensure_value_type(2 + 3.0, 5.0, "double")
ensure_value_type("2" + 3.0, 5.0, "double")
ensure_value_type(2 + "3.0", 5.0, "double")
ensure_value_type("2" + "3.0", 5.0, "double")

ensure_value_type(2.1 + 3.2, 5.3, "double")
ensure_value_type("2.1" + 3.2, 5.3, "double")
ensure_value_type(2.1 + "3.2", 5.3, "double")
ensure_value_type("2.1" + "3.2", 5.3, "double")
