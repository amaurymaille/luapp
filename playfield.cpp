#include <any>
#include <string>
#include <variant>

#include "environment.h"
#include "types.h"

void print_int(int a) {
    std::cout << a << std::endl;
}

void c_print(Types::LuaValue const& value) {
    if (std::holds_alternative<int>(value)) {
        std::cout << "int: " << std::get<int>(value) << std::endl;
    } else if (std::holds_alternative<double>(value)) {
        std::cout << "double: " << std::get<double>(value) << std::endl;
    } else if (std::holds_alternative<Types::Function*>(value)) {
        std::cout << "function: " << std::get<Types::Function*>(value) << std::endl;
    } else if (std::holds_alternative<Types::Table*>(value)) {
        std::cout << "table: " << std::get<Types::Table*>(value) << std::endl;
    } else if (std::holds_alternative<std::string>(value)) {
        std::cout << "string: " << std::get<std::string>(value) << std::endl;
    } else if (std::holds_alternative<bool>(value)) {
        std::cout << "bool: " << std::boolalpha << std::get<bool>(value) << std::noboolalpha << std::endl;
    } else if (std::holds_alternative<Types::Nil>(value)) {
        std::cout << "nil" << std::endl;
    } else {
        std::cout << "Unprocessed type" << std::endl;
    }
}

void value_to_int(Types::Value const& src, std::any& dst) {
    dst = src.as_int_weak();
}

void value_to_bool(Types::Value const& src, std::any& dst) {
    dst = src.as_bool_weak();
}

void value_to_string(Types::Value const& src, std::any& dst) {
    dst = src.as_string();
}

void value_to_function(Types::Value const& src, std::any& dst) {
    dst = src.as<Types::Function*>();
}

void value_to_table(Types::Value const& src, std::any& dst) {
    dst = src.as<Types::Table*>();
}

void value_to_double(Types::Value const& src, std::any& dst) {
    dst = src.as_double_weak();
}

void value_to_nil(Types::Value const& src, std::any& dst) {
    dst = Types::Nil();
}

int main() {
    Types::Converter converter;
    converter.register_conversion<int>(&value_to_int);
    converter.register_conversion<bool>(&value_to_bool);
    converter.register_conversion<double>(&value_to_double);
    converter.register_conversion<Types::Table*>(&value_to_table);
    converter.register_conversion<Types::Function*>(&value_to_function);
    converter.register_conversion<std::string>(&value_to_string);
    converter.register_conversion<Types::Nil>(&value_to_nil);

    Environment env(converter);
    env.register_c_function(std::string("c_print_int"), print_int);
    env.register_c_function("c_print", c_print);
    env.run_file("playfield.lua");

    return 0;
}
