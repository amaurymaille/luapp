ensure_value_type(#"toto", 4, "int")
ensure_value_type(#{}, 0, "int")
ensure_value_type(#{ 2 }, 1, "int")
ensure_value_type(#"", 0, "int")
ensure_value_type(#{ 5, 7, nil, 2 }, 2, "int")
