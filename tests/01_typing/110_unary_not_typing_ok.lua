ensure_value_type(not true, false, "bool")
ensure_value_type(not nil, true, "bool")
ensure_value_type(not false, true, "bool")
ensure_value_type(not {}, false, "bool")
ensure_value_type({}, true, "table")
ensure_value_type(true, true, "bool")
ensure_value_type(false, false, "bool")
ensure_value_type(0, true, "int")
ensure_value_type("string", true, "string")
ensure_value_type(0.1, true, "double")
ensure_value_type(nil, false, "nil")