#include <utility>

#include "function_abstraction.h"

void FunctionAbstraction::bind_next(Types::Value value) {
    _bind_next_fn(value);
}

void FunctionAbstraction::call() {
    _call_fn();
}
