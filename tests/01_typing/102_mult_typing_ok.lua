ensure_value_type(2 * 3, 6, "int")
ensure_value_type("2" * 3, 6, "double")
ensure_value_type(2 * "3", 6, "double")
ensure_value_type("2" * "3", 6, "double")

ensure_value_type(2 * 3.0, 6.0, "double")
ensure_value_type(2 * "3.0", 6.0, "double")
ensure_value_type("2" * 3.0, 6.0, "double")
ensure_value_type("2" * "3.0", 6.0, "double")

ensure_value_type(2.0 * 3, 6.0, "double")
ensure_value_type(2.0 * "3", 6.0, "double")
ensure_value_type("2.0" * 3, 6.0, "double")
ensure_value_type("2.0" * "3", 6.0, "double")

ensure_value_type(2 * 5.2, 10.4, "double")
ensure_value_type("2" * 5.2, 10.4, "double")
ensure_value_type(2 * "5.2", 10.4, "double")
ensure_value_type("2" * "5.2", 10.4, "double")
