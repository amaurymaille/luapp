#pragma once

#include <any>
#include <functional>

#include "exceptions.h"
#include "meta_types.h"
#include "types.h"

class FunctionAbstraction {
public:
    void bind_next(Types::Value value);
    void call();

protected:
    FunctionAbstraction() { }

    std::function<void(Types::Value)> _bind_next_fn;
    std::function<void()> _call_fn;
};

template<typename F>
class CurriedFunction;

template<typename F, typename... Args>
class CurriedFunction<F(Args...)> : public FunctionAbstraction {
private:
    using Next = CurriedFunction<typename Meta::function_reducer_t<F(Args...)>>;
    using T = Meta::first_of_t<Args...>;
    using Signature = F(Args...);

public:
    CurriedFunction(Types::Converter const& converter) : _converter(converter), _next(converter) {
        _bind_next_fn = std::bind_front(&CurriedFunction<F(Args...)>::bind_next, this);
        _call_fn = std::bind_front(&CurriedFunction<F(Args...)>::call, this);
    }

    void bind_next(Types::Value value) {
        if (_bound) {
            _next.bind_next(std::move(value));
        } else {
            std::any result;
            _converter.perform_conversion<T>(value, result);

            _next.init_function(std::bind_front(_function, std::any_cast<T>(result)));
            _bound = true;
        }
    }

    void init_function(std::function<Signature>&& function) {
        _function = std::move(function);
    }

    void call() {
        if (!_bound) {
            throw Exceptions::CLua::UnboundedCall();
        }

        _next.call();
    }

private:
    Types::Converter _converter;
    std::function<Signature> _function;
    Next _next;
    bool _bound = false;
};

template<typename F>
class CurriedFunction<F()> : public FunctionAbstraction {
public:
    CurriedFunction(Types::Converter const&) {

    }

    void bind_next(Types::Value) {
        throw Exceptions::CLua::BindOverflow();
    }

    void call() {
        _function();
    }

    void init_function(std::function<F()>&& function) {
        _function = std::move(function);
    }

private:
    std::function<F()> _function;
};

class FunctionAbstractionBuilderAbstraction {
public:
    void set_converter(Types::Converter const& converter) { _converter = converter; }

    FunctionAbstraction* build() {
        return _build_fn();
    }

protected:
    FunctionAbstractionBuilderAbstraction() { }

    std::function<FunctionAbstraction*()> _build_fn;
    Types::Converter _converter;
};

template<typename F>
class CurriedFunctionBuilder : public FunctionAbstractionBuilderAbstraction {
public:
    CurriedFunctionBuilder(F&& function) : _function(std::move(function)) {
        _build_fn = std::bind_front(&CurriedFunctionBuilder<F>::build, this);
    }

    FunctionAbstraction* build() {
        CurriedFunction<F>* f = new CurriedFunction<F>(_converter);
        f->init_function(_function);
        return f;
    }

private:
    F&& _function;
};
