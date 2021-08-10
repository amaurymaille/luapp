#pragma once

#include <string>
#include <vector>

#include "types.h"

namespace Exceptions {

class string_exception : public std::exception {
public:
    const char* what() const noexcept {
        return _error.c_str();
    }
protected:
    std::string _error;
};

class NameAlreadyUsedException : public string_exception {
public:
    NameAlreadyUsedException(std::string const& name);
};

class BadTypeException : public string_exception {
public:
    BadTypeException(std::string const& expected, std::string const& received, std::string const& context);
};

class ContextlessBadTypeException : public BadTypeException {
public:
    ContextlessBadTypeException(std::string const& expected, std::string const& received);
};

class ValueEqualityExpected : public string_exception {
public:
    ValueEqualityExpected(const std::string& expression, std::string const& expected, std::string const& received);
};

class TypeEqualityExpected : public string_exception {
public:
    TypeEqualityExpected(const std::string& expression, const std::string& expected, const std::string& received);
};

class NilAccess : public string_exception {
public:
    NilAccess(std::string const& detail);
};

class NilDot : public NilAccess {
public:
    NilDot();
};

class BadDotAccess : public string_exception {
public:
    BadDotAccess(std::string const& type);
};

class CrossedLocal : public string_exception {
public:
    CrossedLocal(std::string const& label, std::vector<std::string> const& locals);
};

class InvisibleLabel : public string_exception {
public:
    InvisibleLabel(std::string const& label);
};

class LonelyBreak : public string_exception {
public:
    LonelyBreak(int line);
};

class LabelAlreadyDefined : public string_exception {
public:
    LabelAlreadyDefined(std::string const& label);
};


class StackCorruption : public string_exception {
public:
    StackCorruption(int expected, int received);
};

class BadCall : public string_exception {
public:
    BadCall(std::string const& type);
};

class BadForIn : public string_exception {
public:
    BadForIn();
};

class ForInBadType : public string_exception {
public:
    ForInBadType(std::string const& type);
};

// ============================================================================
// Control-flow exceptions

class Break : public std::exception {
public:
    Break() { }

    const char* what() const noexcept { return ""; }
};

class Goto : public std::exception {
public:
    Goto(std::string const& label) : _label(label) { }

    const char* what() const noexcept { return ""; }

    std::string const& get() const { return _label; }

private:
    std::string const& _label;
};

class Return : public std::exception {
public:
    Return(std::vector<Types::Var>&& values) : _values(std::move(values)) {

    }

    std::vector<Types::Var> const& get() const { return _values; }

    const char* what() const noexcept { return ""; }

private:
    std::vector<Types::Var> _values;
};
}
