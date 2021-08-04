ensure_value_type(1 ^ 2, 1.0, "double")
ensure_value_type(2 ^ 2, 4.0, "double")
ensure_value_type(2.1 ^ 2, 4.41, "double")

ensure_value_type(2 ^ -1, 0.5, "double")
ensure_value_type(3 ^ 3, 27, "double")

ensure_value_type("3" ^ 3, 27, "double")
ensure_value_type("3" ^ "3", 27, "double")
ensure_value_type(3 ^ "3", 27, "double")
