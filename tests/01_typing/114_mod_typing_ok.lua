ensure_value_type(10 % 3, 1, "int")
ensure_value_type("10" % 3, 1, "double")
ensure_value_type(10 % "3", 1, "double")
ensure_value_type("10" % "3", 1, "double")

ensure_value_type(10.0 % 3, 1, "double")
ensure_value_type("10.0" % 3, 1, "double")
ensure_value_type("10.0" % "3", 1, "double")
ensure_value_type(10.0 % "3", 1, "double")

ensure_value_type(10 % 3.0, 1, "double")
ensure_value_type("10" % 3.0, 1, "double")
ensure_value_type("10" % 3.0, 1, "double")
ensure_value_type("10" % "3.0", 1, "double")

ensure_value_type(10.0 % 3.0, 1.0, "double")
ensure_value_type("10.0" % 3.0, 1.0, "double")
ensure_value_type("10.0" % "3.0", 1.0, "double")
ensure_value_type(10.0 % "3.0", 1.0, "double")
