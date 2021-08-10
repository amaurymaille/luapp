#include <cmath>

#include "exceptions.h"
#include "types.h"

namespace Types {

// ============================================================================
// Nil

bool Nil::operator==(const Nil&) const {
    return true;
}

bool Nil::operator!=(const Nil&) const {
    return false;
}

bool Nil::operator<(const Nil&) const {
    return true;
}

// ============================================================================
// Elipsis

Elipsis::Elipsis(std::vector<Types::Value> const& values) : _values(values) { }

Elipsis::Elipsis(Elipsis const& other) : _values(other._values) {
    // throw std::runtime_error("Copying elipsis is illegal. I think ?");
}

Elipsis& Elipsis::operator=(Elipsis const& other) {
    _values = other._values;
    return *this;
    // throw std::runtime_error("Copying elipsis is illegal. I think ?");
}

bool Elipsis::operator==(const Elipsis&) const {
    return false;
}

bool Elipsis::operator!=(const Elipsis&) const {
    return true;
}

bool Elipsis::operator<(const Elipsis&) const {
    return true;
}

// ============================================================================
// Function

Function::Function(std::vector<std::string>&& formal_parameters, LuaParser::BlockContext* body) :
    _body(body), _formal_parameters(std::move(formal_parameters)) {

}

Function::~Function() {
    for (Value* v: std::views::values(_closure)) {
        v->remove_reference();
    }
}

bool Function::operator==(const Function& other) const {
    return this == &other;
}

bool Function::operator!=(const Function& other) const {
    return this != &other;
}

void Function::close(std::string const& name, Value* value) {
    if (_closure.find(name) != _closure.end()) {
        throw std::runtime_error("Closing function twice under " + name);
    }

    _closure[name] = value;
    value->add_reference();
}

// ============================================================================
// Userdata

bool Userdata::operator==(const Userdata& other) const {
    return this == &other;
}

bool Userdata::operator!=(const Userdata& other) const {
    return this != &other;
}

// ============================================================================
// Table

Table::Table(const std::list<std::pair<Value, Value>> &values)  : _bool_fields(2) {
    for (auto const& p: values) {
        std::visit(FieldSetter(*this, p.second), p.first._type);
    }
}

bool Table::operator==(const Table& other) const {
    return this == &other;
}

bool Table::operator!=(const Table& other) const {
    return this != &other;
}

int Table::border() const {
    std::set<unsigned int> keys;
    for (int v : std::views::keys(_int_fields) |
         std::views::filter([](int i) {
            return i > 0;
        })) {

        keys.insert(v);
    }

    for (auto iter = keys.begin(); iter != keys.end(); ++iter) {
        auto next = iter;
        std::advance(next, 1);

        if (next != keys.end()) {
            if (*iter == *next - 1) {
                continue;
            } else {
                return *iter;
            }
        } else {
            return *iter;
        }
    }

    return 0;
}

Value& Table::subscript(Value const& value, bool set_nil) {
    return std::visit(Table::FieldGetter(*this, set_nil), value._type);
}

Value& Table::dot(const std::string &name, bool set_nil) {
    auto iter = _string_fields.find(name);
    if (iter != _string_fields.end()) {
        return iter->second;
    } else {
        if (set_nil) {
            _string_fields[name] = Types::Value::_nil;
            return _string_fields[name];
        } else {
            return Value::_nil;
        }
    }
}

void Table::add_field(const std::string &name, const Value &value) {
    _string_fields[name] = value;
}

void Table::add_field(const Value &source, const Value &dst) {
    std::visit(FieldSetter(*this, dst), source._type);
}

// ============================================================================
// Table::FieldSetter

Table::FieldSetter::FieldSetter(Table& t, Value const& value) : _value(value), _t(t) { }

void Table::FieldSetter::operator()(int i) {
    _t._int_fields[i] = _value;
}

void Table::FieldSetter::operator()(double d) {
    _t._double_fields[d] = _value;
}

void Table::FieldSetter::operator()(bool b) {
    _t._bool_fields[b ? 1 : 0] = _value;
}

void Table::FieldSetter::operator()(std::string const& s) {
    _t._string_fields[s] = _value;
}

void Table::FieldSetter::operator()(Function* f) {
    _t._function_fields[f] = _value;
}

void Table::FieldSetter::operator()(Table* t) {
    _t._table_fields[t] = _value;
}

void Table::FieldSetter::operator()(Userdata* u) {
    _t._userdata_fields[u] = _value;
}

// ============================================================================
// Table::FieldGetter

Table::FieldGetter::FieldGetter(Table& t, bool set_nil) : _t(t), _set_nil(set_nil) { }

Value& Table::FieldGetter::operator()(int i) {
    if (_t._int_fields.find(i) == _t._int_fields.end()) {
        if (_set_nil) {
            _t._int_fields[i] = Value::_nil;
            return _t._int_fields[i];
        } else {
            return Value::_nil;
        }
    } else {
        return _t._int_fields[i];
    }
}

Value& Table::FieldGetter::operator()(double d) {
    if (_t._double_fields.find(d) == _t._double_fields.end()) {
        if (_set_nil) {
            _t._double_fields[d] = Value::_nil;
            return _t._double_fields[d];
        } else {
            return Value::_nil;
        }
    } else {
        return _t._double_fields[d];
    }
}

Value& Table::FieldGetter::operator()(bool b) {
    // No need to check set_nil because the values are already copies of nil
    // if not assigned.
    return _t._bool_fields[b];
}

Value& Table::FieldGetter::operator()(std::string const& s) {
    if (_t._string_fields.find(s) == _t._string_fields.end()) {
        if (_set_nil) {
            _t._string_fields[s] = Value::_nil;
            return _t._string_fields[s];
        } else {
            return Value::_nil;
        }
    } else {
        return _t._string_fields[s];
    }
}

Value& Table::FieldGetter::operator()(Function* f) {
    if (_t._function_fields.find(f) == _t._function_fields.end()) {
        if (_set_nil) {
            _t._function_fields[f] = Value::_nil;
            return _t._function_fields[f];
        } else {
            return Value::_nil;
        }
    } else {
        return _t._function_fields[f];
    }
}

Value& Table::FieldGetter::operator()(Table* t) {
    if (_t._table_fields.find(t) == _t._table_fields.end()) {
        if (_set_nil) {
            _t._table_fields[t] = Value::_nil;
            return _t._table_fields[t];
        } else {
            return Value::_nil;
        }
    } else {
        return _t._table_fields[t];
    }
}

Value& Table::FieldGetter::operator()(Userdata* u) {
    if (_t._userdata_fields.find(u) == _t._userdata_fields.end()) {
        if (_set_nil) {
            _t._userdata_fields[u] = Value::_nil;
            return _t._userdata_fields[u];
        } else {
            return Value::_nil;
        }
    } else {
        return _t._userdata_fields[u];
    }
}

Value& Table::FieldGetter::operator()(Nil) { throw std::runtime_error("No nil allowed in table"); }

Value& Table::FieldGetter::operator()(Elipsis) { throw std::runtime_error("No elipsis allowed in table"); }


// ============================================================================
// Value

Value Value::_nil;
Value Value::_true;
Value Value::_false;
// Value Value::_elipsis;

Value::Value() {
    _type = Nil();
}

Value::Value(Value const& other) : _type(other._type) {
    if (is_refcounted()) {
        sGC->add_reference(_type);
    }
}

constexpr bool Value::is_refcounted() const {
    return std::visit(IsReferenceChecker(), _type);
}

Value& Value::operator=(const Value& other) {
    if (this == &Value::_nil || this == &Value::_false || this == &Value::_true) {
        throw std::runtime_error("Cannot change nil, false or true");
    }
    _type = other._type;
    if (is_refcounted()) {
        sGC->add_reference(_type);
    }

    return *this;
}

Value::~Value() {
    if (is_refcounted()) {
        sGC->remove_reference(_type);
    }
}

void Value::init() {
    _nil._type = Nil();
    _true._type = true;
    _false._type = false;
    // _elipsis._type = Elipsis();
}

bool Value::operator==(const Value& other) const {
    if (this == &other) {
        return true;
    }

    if (_type.index() == other._type.index()) {
        if (is<double>()) {
            double diff = std::fabs(as<double>() - other.as<double>());
            double eps (std::numeric_limits<double>::epsilon() * std::max(1.0, std::max(std::fabs(as<double>()), std::fabs(other.as<double>()))));
            return diff <= eps;
        } else if (is<int>()) {
            return as<int>() == other.as<int>();
        } else if (is<bool>()) {
            return as<bool>() == other.as<bool>();
        } else if (is<std::string>()) {
            return as<std::string>() == other.as<std::string>();
        } else if (is<Nil>() || is<Elipsis>()) {
            return true;
        } else if (is<Table*>()) {
            return *as<Table*>() == *other.as<Table*>();
        } else if (is<Userdata*>()) {
            return *as<Userdata*>() == *other.as<Userdata*>();
        } else if (is<Function*>()) {
            return *as<Function*>() == *other.as<Function*>();
        } else {
            throw std::runtime_error("Bad Value type " + type_as_string());
        }
    } else {
        // Equality between ints and doubles is allowed.
        // Conversion from string to int / double is not allowed here.
        if (is<double>() && other.is<int>()) {
            return as<double>() == other.as_double_weak();
        } else if (is<int>() && other.is<double>()) {
            return as<int>() == other.as_int_weak();
        } else if (is<bool>()) {
            return as<bool>() == other.as_bool_weak();
        } else if (other.is<bool>()) {
            return as_bool_weak() == other.as<bool>();
        } else {
            return false;
        }
    }
}

bool Value::operator!=(const Value& other) const {
    return !(*this == other);
}

/* constexpr bool is_reference() const {
    return std::visit(IsReferenceChecker(), _type);
} */

constexpr bool Value::has_dot() const {
    return is<Table*>() || is<Userdata*>();
}

std::string Value::as_string() const {
    if (is<std::string>()) {
        return as<std::string>();
    } else if (is<int>()) {
        std::ostringstream stream;
        stream << as<int>();
        return stream.str();
    } else if (is<double>()) {
        std::ostringstream stream;
        stream << as<double>();
        return stream.str();
    } else if (is<Nil>()) {
        return "nil";
    } else {
        throw Exceptions::ContextlessBadTypeException("number or string", type_as_string());
    }
}

int Value::as_int_weak(bool allow_double) const {
    if (is<int>()) {
        return as<int>();
    } else if (is<double>()) {
        if (!allow_double) {
            throw Exceptions::ContextlessBadTypeException("integer or integer-string", "double");
        }
        double d = as<double>();
        double intpart;
        if (std::modf(d, &intpart) == 0.0) {
            return d;
        } else {
            // Conversion from double to int is not allowed to fail
            throw Exceptions::ContextlessBadTypeException("integer", "double");
        }
    } else if (is<std::string>()) {
        try {
            double d = std::stod(as<std::string>());
            double intpart;
            if (std::modf(d, &intpart) == 0.0) {
                return d;
            } else {
                throw Exceptions::ContextlessBadTypeException("weak integer", "string of double");
            }
        } catch (std::invalid_argument& e) {
            return std::stoi(as<std::string>());
        }
    } else {
        throw Exceptions::ContextlessBadTypeException("weak integer", type_as_string());
    }
}

double Value::as_double_weak() const {
    if (is<double>()) {
        return as<double>();
    } else if (is<int>()) {
        return as<int>();
    } else if (is<std::string>()) {
        return std::stod(as<std::string>());
    } else {
        throw Exceptions::ContextlessBadTypeException("weak double", type_as_string());
    }
}

bool Value::as_bool_weak() const {
    if (is<bool>()) {
        return std::get<bool>(_type);
    } else if (is<Nil>()) {
        return false;
    } else {
        return true;
    }
}

// Decision: conversion from string to integer yields double in the
// Lua 5.3.2 interpreter, I've decided to yield the appropriate type
// instead.
Value Value::from_string_to_number(bool force_double) const {
    if (!is<std::string>()) {
        throw Exceptions::ContextlessBadTypeException("string", type_as_string());
    }

    Value v;
    if (force_double) {
        v._type = as_double_weak();
        return v;
    } else {
        try {
            v._type = as_int_weak();
        } catch (std::invalid_argument& e) {
            v._type = as_double_weak();
        } catch (std::out_of_range& e) {
            throw;
        }
    }

    return v;
}

/// Makers
Value Value::make_nil() {
    return _nil;
}

Value Value::make_bool(bool b) {
    if (b) {
        return _true;
    } else {
        return _false;
    }
}

Value Value::make_true() { return _true; }

Value Value::make_false() { return _false; }

Value Value::make_table(std::list<std::pair<Value, Value>> const& values) {
    Value v;
    alloc<Table>(v, values);
    sGC->add_reference(v._type);
    return v;
}

Value Value::make_string(std::string&& string) {
    Value v;
    v._type = std::move(string);
    return v;
}

Value Value::make_int(int i) {
    Value v;
    v._type = i;
    return v;
}

Value Value::make_double(double d) {
    Value v;
    v._type = d;
    return v;
}

std::string Value::type_as_string() const {
    if (is<Nil>()) {
        return "nil";
    } else if (is<double>()) {
        return "double";
    } else if (is<int>()) {
        return "int";
    } else if (is<std::string>()) {
        return "string";
    } else if (is<Function*>()) {
        return "function";
    } else if (is<Userdata*>()) {
        return "userdata";
    } else if (is<Table*>()) {
        return "table";
    } else if (is<bool>()) {
        return "bool";
    } else {
        return "unknown type";
    }
}

std::string Value::value_as_string() const {
    std::ostringstream result;
    if (is<Nil>()) {
        result << "nil";
    } else if (is<double>()) {
        result << as<double>();
    } else if (is<int>()) {
        result << as<int>();
    } else if (is<std::string>()) {
        result << as<std::string>();
    } else if (is<Function*>()) {
        result << "function: " << as<Function*>();
    } else if (is<Userdata*>()) {
        result << "userdata: " << as<Userdata*>();
    } else if (is<Table*>()) {
        result << "table: " << as<Table*>();
    } else if (is<bool>()) {
        result << std::boolalpha << as<bool>() << std::noboolalpha;
    } else {
        result << "unknown type";
    }

    return result.str();
}

Value& Value::subscript(Value const& value) {
    if (is<Table*>()) {
        return as<Table*>()->subscript(value);
    } else if (is<Userdata*>()) {
        return _nil;
    } else {
        throw Exceptions::ContextlessBadTypeException("table or userdata", type_as_string());
    }
}

Value& Value::dot(std::string const& name) {
    if (is<Table*>()) {
        return as<Table*>()->dot(name);
    } else if (is<Userdata*>()) {
        return _nil;
    } else {
        throw Exceptions::ContextlessBadTypeException("table or userdata", type_as_string());
    }
}

LuaValue& Value::value() {
    return _type;
}

LuaValue const& Value::value() const {
    return _type;
}

void Value::add_reference() {
    ++_references;
}

void Value::remove_reference() {
    if (_references == 0) {
        throw std::runtime_error("Removing reference on object with no references");
    }

    --_references;
    if (_references == 0) {
        delete this;
    }
}

// ============================================================================
// GC

GC* GC::instance() {
    static GC instance;
    return &instance;
}

void GC::add_reference(LuaValue const& l) {
    _references[l]++;
}

void GC::remove_reference(LuaValue& l) {
    if (_references.find(l) == _references.end()) {
        return;
    }

    unsigned int& count = _references[l];
    if (count == 0) {
        throw std::runtime_error("Attempt to remove reference of value without reference");
    }

    --count;
    if (count == 0) {
        std::visit(Deleter(), l);
        _references.erase(l);
    }
}

// ============================================================================
// Var

Value Var::get() const {
    if (lvalue()) {
        return *_lvalue();
    } else if (rvalue()) {
        return _rvalue();
    } else if (list()) {
        return _list()[0];
    } else {
        _error();
    }
}



void Var::morph() {
    if (!lvalue()) {
        throw std::runtime_error("Cannot morph a Var that doesn't hold an lvalue");
    }

    _value = *_lvalue();
}

bool Var::error() const {
    return std::holds_alternative<VarError>(_value);
}

double Var::as_double_weak() const {
    if  (rvalue()) {
        return _rvalue().as_double_weak();
    } else if  (lvalue()) {
        return _lvalue()->as_double_weak();
    } else if (list()) {
        return _list()[0].as_double_weak();
    } else {
        _error();
    }
}

int Var::as_int_weak(bool allow_double) const {
    if  (rvalue()) {
        return _rvalue().as_int_weak(allow_double);
    } else if  (lvalue()) {
        return _lvalue()->as_int_weak(allow_double);
    } else if (list()) {
        return _list()[0].as_int_weak(allow_double);
    } else {
        _error();
    }
}

bool Var::as_bool_weak() const {
    if  (rvalue()) {
        return _rvalue().as_bool_weak();
    } else if  (lvalue()) {
        return _lvalue()->as_bool_weak();
    } else if (list()) {
        return _list()[0].as_bool_weak();
    } else {
        _error();
    }
}

std::string Var::as_string() const {
    if  (rvalue()) {
        return _rvalue().as_string();
    } else if  (lvalue()) {
        return _lvalue()->as_string();
    } else if (list()) {
        return _list()[0].as_string();
    } else {
        _error();
    }
}

Var::Var() { }

Var::Var(Var const& other) {
    *this = other;
}

Var& Var::operator=(Var const& other) {
    if (other.lvalue()) {
        _value = other._lvalue();
    } else if (other.rvalue()) {
        _value = other.get();
    } else if (other.list()) {
        _value = other._list();
    } else {
        _error();
    }

    return *this;
}

bool Var::operator==(const Var& other) const {
    /* if  (!((lvalue() && other.lvalue()) ||
          (rvalue() && other.rvalue())))
        return false;

    if  (lvalue()) {
        return *_lvalue() == *other._lvalue();
    } else if  (rvalue()) {
        return _rvalue() == other._rvalue();
    } else {
        _error();
    } */
    return get() == other.get();
}

bool Var::operator!=(const Var& other) const {
    return !(*this == other);
}

bool Var::has_dot() const {
    if  (lvalue()) {
        return _lvalue()->has_dot();
    } else if  (rvalue()) {
        return _rvalue().has_dot();
    } else if (list()) {
        return _list()[0].has_dot();
    } else {
        _error();
    }
}

bool Var::is_refcounted() const {
    if  (lvalue()) {
        return _lvalue()->is_refcounted();
    } else if (rvalue()) {
        return _rvalue().is_refcounted();
    } else if (list()) {
        return _list()[0].is_refcounted();
    } else {
        _error();
    }
}

Value Var::from_string_to_number(bool force_double) const {
    if  (lvalue()) {
        return _lvalue()->from_string_to_number(force_double);
    } else if  (rvalue()) {
        return _rvalue().from_string_to_number(force_double);
    } else if (list()) {
        return _list()[0].from_string_to_number(force_double);
    } else {
        _error();
    }
}

std::string Var::type_as_string() const {
    if  (lvalue()) {
        return _lvalue()->type_as_string();
    } else if  (rvalue()) {
        return _rvalue().type_as_string();
    } else if (list()) {
        return _list()[0].type_as_string();
    } else {
        _error();
    }
}

std::string Var::value_as_string() const {
    if  (lvalue()) {
        return _lvalue()->value_as_string();
    } else if  (rvalue()) {
        return _rvalue().value_as_string();
    } else if (list()) {
        return _list()[0].value_as_string();
    } else {
        _error();
    }
}

Value& Var::subscript(Value const& value) {
    if  (lvalue()) {
        return _lvalue()->subscript(value);
    } else if  (rvalue()) {
        return _rvalue().subscript(value);
    } else if (list()) {
        return _list()[0].subscript(value);
    } else {
        _error();
    }
}

Value& Var::dot(std::string const& s) {
    if  (lvalue()) {
        return _lvalue()->dot(s);
    } else if  (rvalue()) {
        return _rvalue().dot(s);
    } else if (list()) {
        return _list()[0].dot(s);
    } else {
        _error();
    }
}

LuaValue& Var::value() {
    if  (lvalue()) {
        return _lvalue()->value();
    } else if  (rvalue()) {
        return _rvalue().value();
    } else if (list()) {
        return _list()[0].value();
    } else {
        _error();
    }
}

}
