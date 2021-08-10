#pragma once

#include <functional>

#include "function_abstraction.h"
#include "interpreter.h"
#include "types.h"

class Environment {
public:
    Environment(Types::Converter const& converter) : _converter(converter) { }

    void run_file(std::string const& file);

    template<typename F>
    void register_c_function(std::string const& name, F&& function) {
        FunctionAbstractionBuilderAbstraction* builder = new CurriedFunctionBuilder(std::move(function));
        builder->set_converter(_converter);
        Types::Function* f = new Types::Function(builder);
        _interpreter.register_global_c_function(name, f);
    }

private:
    Types::Converter _converter;
    Interpreter _interpreter;
};
