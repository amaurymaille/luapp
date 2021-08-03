ensure_value_type(3 / 2, 1.5, "double")
ensure_value_type("3" / 2, 1.5, "double")
ensure_value_type(3 / "2", 1.5, "double")
ensure_value_type("3" / "2", 1.5, "double")

ensure_value_type(3.0 / 2, 1.5, "double")
ensure_value_type("3.0" / 2, 1.5, "double")
ensure_value_type(3.0 / "2", 1.5, "double")
ensure_value_type("3.0" / "2", 1.5, "double")

ensure_value_type(3 / 2.0, 1.5, "double")
ensure_value_type("3" / 2.0, 1.5, "double")
ensure_value_type(3 / "2.0", 1.5, "double")
ensure_value_type("3" / "2.0", 1.5, "double")
