ensure_value_type(-2, -2, "int")
ensure_value_type(-2.0, -2.0, "double")
ensure_value_type(-"2", -2.0, "double")
ensure_value_type(-"2.0", -2.0, "double")
-- ensure_value_type(-(-1), 1, "int")
-- ensure_value_type(-(2 + 3), -5, "int")
-- ensure_value_type(-("2" + 3), -5.0, "double")
