#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iosfwd>
#include <iterator>
#include <ranges>
#include <string>
#include <sstream>
#include <type_traits>
#include <variant>

#include "antlr4-runtime.h"
#include "Token.h"

#include <boost/program_options.hpp>

#include "LuaLexer.h"
#include "LuaBaseListener.h"
#include "LuaParser.h"
#include "LuaVisitor.h"

namespace fs = std::filesystem;
namespace po = boost::program_options;

class MyLuaVisitor;

namespace Exceptions {
    class NameAlreadyUsedException : public std::exception {
    public:
        NameAlreadyUsedException(std::string const& name) {
            std::ostringstream err;
            err << "Name " << name << " already defined" << std::endl;

            _error = err.str();
        }

        const char* what() const noexcept {
            return _error.c_str();
        }

    private:
        std::string _error;
    };

    class BadTypeException : public std::exception {
    public:
        BadTypeException(std::string const& expected, std::string const& received, std::string const& context) {
            std::ostringstream err;
            err << "Bad type received " << context << ": expected " << expected << ", got " << received << std::endl;
            _error = err.str();
        }

        const char* what() const noexcept {
            return _error.c_str();
        }

    protected:
        std::string _error;
    };

    class ContextlessBadTypeException : public BadTypeException {
    public:
        ContextlessBadTypeException(std::string const& expected, std::string const& received) : BadTypeException(expected, received, "") {

        }
    };

    class ValueEqualityExpected : public std::exception {
    public:
        ValueEqualityExpected(const std::string& expression, std::string const& expected, std::string const& received) {
            _context = "Expression " + expression + " has value " + received + ", expected " + expected + "\n";
        }

        const char* what() const noexcept {
            return _context.c_str();
        }

    private:
        std::string _context;
    };

    class TypeEqualityExpected : public std::exception {
    public:
        TypeEqualityExpected(const std::string& expression, const std::string& expected, const std::string& received) {
            std::ostringstream error;
            error << "Expression " << expression << " has type " << received << ", expected " << expected << std::endl;
            _error = error.str();
        }

        const char* what() const noexcept {
            return _error.c_str();
        }

    private:
        std::string _error;
    };

    class NilAccess : public std::exception {
    public:
        NilAccess(std::string const& detail) {
            _error = detail + " on nil value";
        }

        const char* what() const noexcept final {
            return _error.c_str();
        }

    private:
        std::string _error;
    };

    class NilDot : public NilAccess {
    public:
        NilDot() : NilAccess("Attempt to use dot") { }
    };

    class BadDotAccess : public std::exception {
    public:
        BadDotAccess(std::string const& type) {
            _error = "Attempt to use dot on " + type;
        }

        const char* what() const noexcept {
            return _error.c_str();
        }

    private:
        std::string _error;
    };

    class CrossedLocal : public std::exception {
    public:
        /*CrossedLocal(std::string const& label, std::string const& local) {
            _error = "goto " + label + " crosses initialization of local " + local + "\n";
        }*/

        CrossedLocal(std::string const& label, std::vector<std::string> const& locals) {
            _error = "goto " + label + " crosses initialization of local" + ((locals.size() != 1) ? "s" : "") + ": ";
            for (std::string const& local: locals) {
                _error += local + " ";
            }
        }

        const char* what() const noexcept {
            return _error.c_str();
        }

    private:
        std::string _error;
    };

    class InvisibleLabel : public std::exception {
    public:
        InvisibleLabel(std::string const& label) {
            _error = "Label "+ label + " is not visible\n";
        }

        const char* what() const noexcept {
            return _error.c_str();
        }
    private:
        std::string _error;
    };
}

namespace Types {
    struct Value;
    struct Function;
    struct Userdata;
    class Table;

    struct Nil {
        bool operator==(const Nil&) const {
            return true;
        }

        bool operator!=(const Nil&) const {
            return false;
        }

        bool operator<(const Nil&) const {
            return true;
        }
    };

    struct Elipsis {
        bool operator==(const Elipsis&) const {
            return false;
        }

        bool operator!=(const Elipsis&) const {
            return true;
        }

        bool operator<(const Elipsis&) const {
            return true;
        }
    };

    typedef std::variant<bool, int, double, std::string, Nil, Elipsis, Function*, Userdata*, Table*> LuaValue;

    struct Function {
        bool operator==(const Function& other) const {
            return this == &other;
        }

        bool operator!=(const Function& other) const {
            return this != &other;
        }
    };

    struct Userdata {
        bool operator==(const Userdata& other) const {
            return this == &other;
        }

        bool operator!=(const Userdata& other) const {
            return this != &other;
        }
    };

    class Table {
    public:
        Table(const std::list<std::pair<Value, Value> > &values);

        bool operator==(const Table& other) const {
            return this == &other;
        }

        bool operator!=(const Table& other) const {
            return this != &other;
        }

        int border() const;
        Value& subscript(Value const&);
        Value& dot(std::string const&);

    private:
        struct FieldSetter {
        public:
            FieldSetter(Table& t, Value const& value) : _value(value), _t(t) { }

            // Can't have nil as key
            void operator()(Nil) { }

            void operator()(int i);

            void operator()(double d);

            void operator()(bool b);

            void operator()(std::string const& s);

            void operator()(Function* f);

            void operator()(Table* t);

            void operator()(Userdata* u);

            // Can't have elipsis as key
            void operator()(Elipsis) { }

        private:
            Value const& _value;
            Table& _t;
        };

        struct FieldGetter {
        public:
            FieldGetter(Table& t) : _t(t) { }

            Value& operator()(Nil);

            Value& operator()(int i);

            Value& operator()(double d);

            Value& operator()(bool b);

            Value& operator()(std::string const& s);

            Value& operator()(Function* f);

            Value& operator()(Table* t);

            Value& operator()(Userdata* u);

            Value& operator()(Elipsis);

        private:
            Table& _t;
        };

        friend class Value;
        friend class FieldSetter;
        friend class FieldGetter;

        std::map<int, Value> _int_fields;
        std::map<double, Value> _double_fields;
        std::vector<Value> _bool_fields;
        std::map<std::string, Value> _string_fields;
        std::map<Function*, Value> _function_fields;
        std::map<Table*, Value> _table_fields;
        std::map<Userdata*, Value> _userdata_fields;

        // std::map<Value*, Value*> _fields;
    };

    template<typename T>
    struct IsReference;

    template<>
    struct IsReference<Nil> : public std::false_type {};

    template<>
    struct IsReference<Elipsis> : public std::false_type {};

    template<>
    struct IsReference<Function*> : public std::true_type {};

    template<>
    struct IsReference<Table*> : public std::true_type {};

    template<>
    struct IsReference<Userdata*> : public std::true_type {};

    template<>
    struct IsReference<int> : public std::false_type {};

    template<>
    struct IsReference<double> : public std::false_type {};

    template<>
    struct IsReference<bool> : public std::false_type {};

    template<>
    struct IsReference<std::string> : public std::false_type {};

    template<typename T>
    constexpr bool IsReferenceV = IsReference<std::decay_t<T>>::value;

    class IsReferenceChecker {
    public:
        template<typename T>
        constexpr bool operator()(T&&) const {
            return IsReferenceV<T>;
        }
    };

    class GC {
    public:
        static GC* instance() {
            static GC instance;
            return &instance;
        }

        void add_reference(LuaValue const& l) {
            _references[l]++;
        }

        void remove_reference(LuaValue& l) {
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

    private:
        GC() { }

        std::map<LuaValue, unsigned int> _references;

        struct Deleter {
        public:
            template<typename T>
            void operator()(T value) { /* std::cout << "Deleting at " << value << std::endl; */ delete value; }

            void operator()(bool) { }
            void operator()(Nil) { }
            void operator()(Elipsis) { }
            void operator()(int) { }
            void operator()(double) { }
            void operator()(std::string const&) { }
        };
    };

    #define sGC Types::GC::instance()

    class Value {
    public:
        Value() {
            _type = Nil();
        }

        Value(Value const& other) : _type(other._type) {
            if (is_refcounted()) {
                sGC->add_reference(_type);
            }
        }

        constexpr bool is_refcounted() const {
            return std::visit(IsReferenceChecker(), _type);
        }

        Value& operator=(const Value& other) {
            _type = other._type;
            if (is_refcounted()) {
                sGC->add_reference(_type);
            }

            return *this;
        }

        ~Value() {
            if (is_refcounted()) {
                sGC->remove_reference(_type);
            }
        }

        static void init() {
            _nil._type = Nil();
            _true._type = true;
            _false._type = false;
            _elipsis._type = Elipsis();
        }

        friend Table::Table(const std::list<std::pair<Value, Value>>&);
        friend Value& Table::subscript(const Value &);
        friend Value& Table::dot(const std::string &);
        friend MyLuaVisitor;

        bool operator==(const Value& other) const {
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

        bool operator!=(const Value& other) const {
            return !(*this == other);
        }

        /* constexpr bool is_reference() const {
            return std::visit(IsReferenceChecker(), _type);
        } */

        constexpr bool has_dot() const {
            return is<Table*>() || is<Userdata*>();
        }

        template<typename T>
        constexpr bool is() const {
            return std::holds_alternative<T>(_type);
        }

        template<typename T>
        T as() const {
            return std::get<T>(_type);
        }

        std::string as_string() const {
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
            } else {
                throw Exceptions::ContextlessBadTypeException("number or string", type_as_string());
            }
        }

        int as_int_weak(bool allow_double = true) const {
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

        double as_double_weak() const {
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

        bool as_bool_weak() const {
            if (is<bool>()) {
                return std::get<bool>(_type);
            } else if (is<Nil>()) {
                return false;
            } else {
                return true;
            }
        }

        template<typename T, typename... Args>
        static void alloc(Value& v, Args&&... args) {
            v._type = new T(std::forward<Args>(args)...);
            sGC->add_reference(v._type);
        }

        // Decision: conversion from string to integer yields double in the
        // Lua 5.3.2 interpreter, I've decided to yield the appropriate type
        // instead.
        Value from_string_to_number(bool force_double = false) const {
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
        static Value make_nil() { return _nil; }

        static Value make_bool(bool b) {
            if (b) {
                return _true;
            } else {
                return _false;
            }
        }

        static Value make_true() { return _true; }

        static Value make_false() { return _false; }

        static Value make_elipsis() { return  _elipsis; }

        static Value make_table(std::list<std::pair<Value, Value>> const& values) {
            Value v;
            alloc<Table>(v, values);
            return v;
        }

        static Value make_string(std::string&& string) {
            Value v;
            v._type = std::move(string);
            return v;
        }

        static Value make_int(int i) {
            Value v;
            v._type = i;
            return v;
        }

        static Value make_double(double d) {
            Value v;
            v._type = d;
            return v;
        }

        template<typename T>
        static Value make(T value) requires (!std::is_same_v<T, Nil> && !std::is_same_v<T, bool> && !std::is_same_v<T, Elipsis>) {
            Value v;
            v._type = value;
            return v;
        }

        std::string type_as_string() const {
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

        std::string value_as_string() const {
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

        Value& subscript(Value const& value) {
            if (is<Table*>()) {
                return as<Table*>()->subscript(value);
            } else if (is<Userdata*>()) {
                return _nil;
            } else {
                throw Exceptions::ContextlessBadTypeException("table or userdata", type_as_string());
            }
        }

        Value& dot(std::string const& name) {
            if (is<Table*>()) {
                return as<Table*>()->dot(name);
            } else if (is<Userdata*>()) {
                return _nil;
            } else {
                throw Exceptions::ContextlessBadTypeException("table or userdata", type_as_string());
            }
        }

        LuaValue& value() {
            return _type;
        }

    private:
        LuaValue _type;

        static Value _nil;
        static Value _elipsis;
        static Value _true;
        static Value _false;
    };

    Value Value::_nil;
    Value Value::_true;
    Value Value::_false;
    Value Value::_elipsis;

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

    Table::Table(const std::list<std::pair<Value, Value>> &values)  : _bool_fields(2) {
        for (auto const& p: values) {
            std::visit(FieldSetter(*this, p.second), p.first._type);
        }
    }

    Value& Table::subscript(Value const& value) {
        return std::visit(Table::FieldGetter(*this), value._type);
    }

    Value& Table::dot(const std::string &name) {
        auto iter = _string_fields.find(name);
        if (iter != _string_fields.end()) {
            return iter->second;
        } else {
            return Value::_nil;
        }
    }

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

    Value& Table::FieldGetter::operator()(int i) { return _t._int_fields[i]; }

    Value& Table::FieldGetter::operator()(double d) { return _t._double_fields[d]; }

    Value& Table::FieldGetter::operator()(bool b) { return _t._bool_fields[b]; }

    Value& Table::FieldGetter::operator()(std::string const& s) { return _t._string_fields[s]; }

    Value& Table::FieldGetter::operator()(Function* f) { return _t._function_fields[f]; }

    Value& Table::FieldGetter::operator()(Table* t) { return _t._table_fields[t]; }

    Value& Table::FieldGetter::operator()(Userdata* u) { return _t._userdata_fields[u]; }

    Value& Table::FieldGetter::operator()(Nil) { throw std::runtime_error("No nil allowed in table"); }

    Value& Table::FieldGetter::operator()(Elipsis) { throw std::runtime_error("No elipsis allowed in table"); }
}

/* Helper class used to detect whether goto statements are legit or not.
 * Gotos are lexically scoped but can't cross functions bodies: you can jump
 * to any label that has been defined in a surrounding block, but you can't
 * escape from the boundary of the function you are (maybe in).
 * Additionnaly, gotos cannot cross the declaration of a local variable.
 */
class GotoListener : public LuaBaseListener {
public:
    GotoListener() {
        _current_context = nullptr;
    }

    void enterChunk(LuaParser::ChunkContext *ctx) {
        _scopes.push_back(Scope());
        _stack_scopes.push(&_scopes.back());
        _current_scope = &_scopes.back();
        _current_scope->_root_context = ctx->block();
    }

    void enterBlock(LuaParser::BlockContext *ctx) {
        // std::cout << ctx->getText() << std::endl;
        _blocks.push(ctx);

        if (_current_context) {
            _current_scope->_scope_elements[_current_context].push_back(make_element(ctx));
            _parents[ctx] = _current_context;
        } else {
            // std::cout << "No context" << std::endl;
        }

        _current_context = _blocks.top();
    }

    void exitBlock(LuaParser::BlockContext *ctx) {
        if (_blocks.top() != ctx) {
            throw std::runtime_error("Unbalanced blocks");
        }

        _blocks.pop();
        if (_blocks.empty()) {
            _current_context = nullptr;
        } else {
            _current_context = _blocks.top();
        }
    }

    void enterStat(LuaParser::StatContext *ctx) {
        if (!ctx->getText().starts_with("local") &&
                !ctx->getText().starts_with("function") &&
                !ctx->getText().starts_with(("local function")) &&
                !ctx->getText().starts_with(("goto"))) {
            return;
        }

        // Goto
        if (ctx->getText().starts_with("goto")) {
            std::string label(ctx->NAME()->toString());
            _current_scope->_scope_elements[_current_context].push_back(make_element(Goto(std::move(label))));
            // Declaration of local variables (including local functions)
        } else if (ctx->getText().starts_with("local")) {
            if (ctx->funcbody()) {
                _current_scope->_scope_elements[_current_context].push_back(make_element(Local(ctx->NAME()->getText())));
                // _current_context = nullptr;
            } else {
                LuaParser::AttnamelistContext* lst = ctx->attnamelist();
                for (auto name: lst->NAME()) {
                    _current_scope->_scope_elements[_current_context].push_back(make_element(Local(name->getText())));
                }
            }
            // Definition of a function
        } else {
            _current_scope->_scope_elements[_current_context].push_back(make_element(Local(ctx->funcname()->getText())));
            // _current_context = nullptr;
        }
    }

    // Beginning of the scope of an inner function
    void enterFuncbody(LuaParser::FuncbodyContext *ctx) override {
        _scopes.push_back(Scope());
        _current_scope = &_scopes.back();
        _stack_scopes.push(&_scopes.back());
        _current_scope->_root_context = ctx->block();
        _current_context = nullptr;
    }

    // End of the scope of an inner function
    void exitFuncbody(LuaParser::FuncbodyContext *) override {
        _stack_scopes.pop();
        if (_stack_scopes.empty()) {
            _current_scope = nullptr;
        } else {
            _current_scope = _stack_scopes.top();
        }
    }

    void enterLabel(LuaParser::LabelContext *ctx) {
        _current_scope->_scope_elements[_current_context].push_back(make_element(Label(ctx->NAME()->getText())));
    }

    void validate() const {
        for (const Scope& scope: _scopes) {
            LuaParser::BlockContext* ctx = scope._root_context;
            std::vector<std::string> seen_labels;
            std::vector<std::pair<std::vector<ScopeElement>::const_iterator, std::vector<ScopeElement>::const_iterator>> previous;
            explore_context(scope, ctx, seen_labels, previous);
        }
    }

private:
    struct Goto {
        std::string _label;
        Goto(std::string&& label) : _label(std::move(label)) { }
    };

    struct Label {
        std::string _name;
        Label(std::string&& name) : _name(std::move(name)) { }
    };

    struct Local {
        std::string _name;
        Local(std::string&& name) : _name(std::move(name)) { }
    };

    typedef std::variant<Goto, Label, Local, LuaParser::BlockContext*> ScopeElement;

    template<typename T>
    ScopeElement make_element(T&& t) {
        ScopeElement e(std::move(t));
        return e;
    }

    /* A scope is the "smallest" block that can be gotoed. For example, the
     * global scope is its own Scope, because no function can goto into the
     * global scope, you can only goto the global scope from the global scope.
     * Conversely, every function defines a new Scope.
     *
     * A Scope can hold several blocks. Each block is defined as per the Lua
     * grammar. You can goto into any block that is on the same level as you
     * , or above, but you can't goto into a block that is below you. You can
     * goto outside of an if block, but you can't goto into an if block.
     */
    struct Scope {
        std::map<LuaParser::BlockContext*, std::vector<ScopeElement>> _scope_elements;
        LuaParser::BlockContext* _root_context;
    };

    void explore_context(Scope const& scope,
                         LuaParser::BlockContext* const ctx,
                         std::vector<std::string> const& previous_labels,
                         std::vector<std::pair<std::vector<ScopeElement>::const_iterator, std::vector<ScopeElement>::const_iterator>> const& previous) const {
        std::vector<std::string> labels(previous_labels);
        std::vector<std::pair<std::vector<ScopeElement>::const_iterator, std::vector<ScopeElement>::const_iterator>> iters(previous);

        auto scope_element_iter_because_const = scope._scope_elements.find(ctx);
        if (scope_element_iter_because_const == scope._scope_elements.end()) {
            throw std::runtime_error("Unable to find context");
        }

        auto vec = scope_element_iter_because_const->second;
        for (auto iter = vec.cbegin(); iter != vec.cend(); ++iter) {
            ScopeElement const& element = *iter;
            if (const Goto* stmt = std::get_if<Goto>(&element)) {
                if (std::find(labels.begin(), labels.end(), stmt->_label) != labels.end()) {
                    // std::cout << "Label " << stmt->_label << " was already seen, everything okay" << std::endl;
                    continue;
                } else {
                    // std::cout << "Label " << stmt->_label << " not yet seen, looking forward" << std::endl;
                    iters.push_back(std::make_pair(iter, vec.cend()));
                    validate_goto(iters, stmt->_label);
                    iters.pop_back();
                }
            } else if (const Label* label = std::get_if<Label>(&element)) {
                labels.push_back(label->_name);
            } else if (LuaParser::BlockContext* const* cctx = std::get_if<LuaParser::BlockContext*>(&element)) {
                iters.push_back(std::make_pair(iter, vec.cend()));
                explore_context(scope, *cctx, labels, iters);
                iters.pop_back();
            }
        }
    }

    void validate_goto(std::vector<std::pair<std::vector<ScopeElement>::const_iterator, std::vector<ScopeElement>::const_iterator>> const& previous,
                       std::string const& search) const {
        bool found = false;
        // std::string suspected_local;
        for (auto iter = previous.rbegin(); iter != previous.rend(); ++iter) {
            auto elements_iter = iter->first;
            auto end_iter = iter->second;
            std::vector<std::string> crossed;

            if (found) {
                break;
            }

            for (; elements_iter != end_iter; ++elements_iter) {
                ScopeElement const& element = *elements_iter;
                if (const Local* local = std::get_if<Local>(&element)) {
                    crossed.push_back(local->_name);
                } else if (const Label* label = std::get_if<Label>(&element)) {
                    if (label->_name == search) {
                        if (!crossed.empty()) {
                            throw Exceptions::CrossedLocal(search, crossed);
                        }
                        found = true;
                        break;
                    }
                }
            }
        }

        if (!found) {
            throw Exceptions::InvisibleLabel(search);
        }
    }

    LuaParser::BlockContext* root_context(Scope const& scope) const {
        auto begin = scope._scope_elements.begin();
        auto iter = _parents.find(begin->first);
        auto copy = iter;

        while (iter != _parents.end()) {
            copy = iter;
            iter = _parents.find(iter->second);
        }

        return copy->second;
    }

    std::map<LuaParser::BlockContext*, LuaParser::BlockContext*> _parents;
    LuaParser::BlockContext* _current_context;
    std::stack<LuaParser::BlockContext*> _blocks;
    std::list<Scope> _scopes;
    std::stack<Scope*> _stack_scopes;
    Scope* _current_scope;
};


class MyLuaVisitor : public LuaVisitor {
public:
    MyLuaVisitor(antlr4::tree::ParseTree* tree) {
        GotoListener listener;
        antlr4::tree::ParseTreeWalker::DEFAULT.walk(&listener, tree);
        listener.validate();
    }

    ~MyLuaVisitor() {

    }

    virtual antlrcpp::Any visitChunk(LuaParser::ChunkContext *context) {
        // std::cout << "Chunk: " << context->getText() << std::endl;
        return visit(context->block());
    }

    virtual antlrcpp::Any visitBlock(LuaParser::BlockContext *context) {
        // std::cout << "Block: " << context->getText() << std::endl;
        _local_values.push_back(ValueStore());
        antlrcpp::Any retval = Types::Value::make_nil();

        for (LuaParser::StatContext* ctx: context->stat()) {
            visit(ctx);
        }

        if (LuaParser::RetstatContext* ctx = context->retstat()) {
            retval = visit(ctx);
        }

        /* for (auto& value: std::views::values(_local_values.top())) {
            value->remove_reference();
        } */

        _local_values.pop_back();

        return retval;
    }

    virtual antlrcpp::Any visitStat(LuaParser::StatContext *context) {
        // std::cout << "Stat: " << context->getText() << std::endl;
        if (context->getText() == ";") {
            ;
        } else if (context->getText() == "break") {
            process_break();
        } else if (context->getText().starts_with("goto")) {
            process_goto(context->label());
        } else if (context->getText().starts_with("do")) {
            visit(context->block()[0]);
        } else if (context->getText().starts_with("while")) {
            process_while(context->exp()[0], context->block()[0]);
        } else if (context->getText().starts_with("repeat")) {
            process_repeat(context->block()[0], context->exp()[0]);
        } else if (context->getText().starts_with("if")) {
            process_if(context);
        } else if (context->getText().starts_with("for")) {
            if (LuaParser::NamelistContext* ctx = context->namelist()) {
                process_for_in(ctx, context->explist(), context->block()[0]);
            } else {
                process_for_loop(context);
            }
        } else if (context->getText().starts_with("function")) {
            process_function(context->funcname(), context->funcbody());
        } else if (context->getText().starts_with("local")) {
            if (LuaParser::AttnamelistContext* ctx = context->attnamelist()) {
                process_local_variables(ctx, context->explist());
            } else {
                process_local_function(context->NAME()->getText(), context->funcbody());
            }
        } else if (LuaParser::VarlistContext* ctx = context->varlist()) {
            process_stat_var_list(ctx, context->explist());
        } else if (LuaParser::FunctioncallContext* ctx = context->functioncall()) {
            visit(ctx);
        } else if (LuaParser::LabelContext* ctx = context->label()) {
            visit(ctx);
        } else {
            throw std::runtime_error("Unknown statement");
        }

        return Var::make(Types::Value::make_nil());
    }

    virtual antlrcpp::Any visitAttnamelist(LuaParser::AttnamelistContext *context) {
        std::vector<antlr4::tree::TerminalNode*> names = context->NAME();
        std::vector<std::string> result;
        std::transform(names.cbegin(), names.cend(), std::back_inserter(result), [](antlr4::tree::TerminalNode* node) {
                           return node->toString();
                       });
        return result;
    }

    virtual antlrcpp::Any visitAttrib(LuaParser::AttribContext *) {
        std::cerr << "Attributes are not supported" << std::endl;
        return Var::make(Types::Value::make_nil());
    }

    virtual antlrcpp::Any visitRetstat(LuaParser::RetstatContext *context) {
        if (LuaParser::ExplistContext* ctx = context->explist()) {
            return visit(ctx);
        } else {
            return Var::make(Types::Value::make_nil());
        }
    }

    virtual antlrcpp::Any visitLabel(LuaParser::LabelContext *context) {
        return nyi("Label");
    }

    virtual antlrcpp::Any visitFuncname(LuaParser::FuncnameContext *context) {
        return nyi("Funcname");
    }

    virtual antlrcpp::Any visitVarlist(LuaParser::VarlistContext *context) {
        _var__contex = Var_Context::VARLIST;
        std::vector<Var> vars;
        for (LuaParser::Var_Context* ctx: context->var_()) {
            vars.push_back(visit(ctx).as<Var>());
        }

        _var__contex = Var_Context::OTHER;
        return vars;
    }

    virtual antlrcpp::Any visitNamelist(LuaParser::NamelistContext *context) {
        return nyi("Namelist");
    }

    virtual antlrcpp::Any visitExplist(LuaParser::ExplistContext *context) {
        std::vector<Var> values;
        for (LuaParser::ExpContext* ctx: context->exp()) {
            values.push_back(visit(ctx).as<Var>());
        }

        return values;
    }

    virtual antlrcpp::Any visitExp(LuaParser::ExpContext *context) {
        if (context->getText() == "nil") {
            return Var::make(Types::Value::make_nil());
        } else if (context->getText() == "true") {
            return Var::make(Types::Value::make_true());
        } else if (context->getText() == "false") {
            return Var::make(Types::Value::make_false());
        } else if (context->getText() == "...") {
            return Var::make(Types::Value::make_elipsis());
        } else if (LuaParser::NumberContext* ctx = context->number()) {
            return visit(ctx);
        } else if (LuaParser::StringContext* ctx = context->string()) {
            return visit(ctx);
        } else if (LuaParser::FunctiondefContext* ctx = context->functiondef()) {
            return visit(ctx);
        } else if (LuaParser::PrefixexpContext* ctx = context->prefixexp()) {
            return visit(ctx);
        } else if (LuaParser::TableconstructorContext* ctx = context->tableconstructor()) {
            return visit(ctx);
        } else if (context->operatorPower()) {
            // Promote both operands (if int or string representing double) to
            // double for exponentiation.
            Var leftV = visit(context->exp()[0]).as<Var>();
            double left = leftV.as_double_weak();

            Var rightV = visit(context->exp()[1]).as<Var>();
            double right = rightV.as_double_weak();

            return Var::make(Types::Value::make_double(std::pow(left, right)));
        } else if (LuaParser::OperatorUnaryContext* ctx = context->operatorUnary()) {
            Var v = visit(context->exp()[0]).as<Var>();
            OperatorUnary op = visit(ctx).as<OperatorUnary>();

            switch (op) {
            case OperatorUnary::BANG:
                if (v.is<std::string>()) {
                    return Var::make(Types::Value::make_int(v.as<std::string>().size()));
                } else {
                    return Var::make(Types::Value::make_int(v.as<Types::Table*>()->border()));
                }

            case OperatorUnary::BIN_NOT: {
                 return Var::make(Types::Value::make_int(~v.as_int_weak()));
            }

            case OperatorUnary::MINUS: {
                std::function<Var(Var const&)> convert = [&](Var const& value) -> Var {
                    Var result;
                    if (value.is<int>()) {
                        result = Var::make(Types::Value::make_int(-value.as<int>()));
                    } else if (value.is<double>()) {
                        result = Var::make(Types::Value::make_double(-value.as<double>()));
                    } else {
                        result = convert(Var::make(value.from_string_to_number(true)));
                    }

                    return result;
                };

                return convert(v);
            }

            case OperatorUnary::NOT: {
                return Var::make(Types::Value::make_bool(!v.as_bool_weak()));
            }

            default:
                throw std::runtime_error("Invalid Unary operator");
            }
        } else if (LuaParser::OperatorMulDivModContext* ctx = context->operatorMulDivMod()) {
            Var leftV = visit(context->exp()[0]).as<Var>();
            Var rightV = visit(context->exp()[1]).as<Var>();

            Var result;

            double left = leftV.as_double_weak();
            double right = rightV.as_double_weak();

            OperatorMulDivMod op = visit(ctx).as<OperatorMulDivMod>();
            switch (op) {
            case OperatorMulDivMod::MUL:
                if (leftV.is<int>() && rightV.is<int>()) {
                    result = Var::make(Types::Value::make_int(leftV.as<int>() * rightV.as<int>()));
                } else {
                    result = Var::make(Types::Value::make_double(left * right));
                }
                break;

            case OperatorMulDivMod::DIV:
                result = Var::make(Types::Value::make_double(left / right));
                break;

            case OperatorMulDivMod::MOD:
                if (leftV.is<int>() && rightV.is<int>()) {
                    result = Var::make(Types::Value::make_int(leftV.as<int>() % rightV.as<int>()));
                } else {
                    result = Var::make(Types::Value::make_double(std::remainder(left, right)));
                }
                break;

            case OperatorMulDivMod::QUOT:
                if (leftV.is<int>() && rightV.is<int>()) {
                    result = Var::make(Types::Value::make_int(std::floor(left / right)));
                } else {
                    result = Var::make(Types::Value::make_double(std::floor(left / right)));
                }
                break;

            default:
                throw std::runtime_error("Invalid MulDivMod operator");
            }

            return result;
        } else if (LuaParser::OperatorAddSubContext* ctx = context->operatorAddSub()) {
            Var leftV = visit(context->exp()[0]).as<Var>();
            Var rightV = visit(context->exp()[1]).as<Var>();

            Var result;

            OperatorAddSub op = visit(ctx);
            if (leftV.is<int>() && rightV.is<int>()) {
                int left = leftV.as<int>();
                int right = rightV.as<int>();

                switch (op) {
                case OperatorAddSub::ADD:
                    result = Var::make(Types::Value::make_int(left + right));
                    break;

                case OperatorAddSub::SUB:
                    result = Var::make(Types::Value::make_int(left - right));
                    break;

                default:
                    throw std::runtime_error("Invalid AddSub operator");
                }
            } else {
                double left = leftV.as_double_weak();
                double right = rightV.as_double_weak();

                switch (op) {
                case OperatorAddSub::ADD:
                    result = Var::make(Types::Value::make_double(left + right));
                    break;

                case OperatorAddSub::SUB:
                    result = Var::make(Types::Value::make_double(left - right));
                    break;

                default:
                    throw std::runtime_error("Invalid AddSub operator");
                }
            }

            return result;
        } else if (context->operatorStrcat()) {
            // Promote numbers to strings if necessary.
            // Note that Lua allows two numbers to concatenate as a string.
            Var leftV = visit(context->exp()[0]).as<Var>();
            Var rightV = visit(context->exp()[1]).as<Var>();

            std::string left = leftV.as_string();
            std::string right = rightV.as_string();

            return Var::make(Types::Value::make_string(left + right));
        } else if (LuaParser::OperatorComparisonContext* ctx = context->operatorComparison()) {
            Var leftV = visit(context->exp()[0]).as<Var>();
            Var rightV = visit(context->exp()[1]).as<Var>();

            double left = leftV.as_double_weak();
            double right = rightV.as_double_weak();

            OperatorComparison op = visit(ctx).as<OperatorComparison>();
            std::function<bool(double, double)> fn;

            switch (op) {
            case OperatorComparison::DIFF:
                fn = [](double x, double y) { return x != y ;};
                break;

            case OperatorComparison::EQ:
                fn = [](double x, double y) { return x == y ;};
                break;

            case OperatorComparison::GREATER_E:
                fn = [](double x, double y) { return x >= y ;};
                break;

            case OperatorComparison::GREATER_S:
                fn = [](double x, double y) { return x > y ;};
                break;

            case OperatorComparison::LOWER_E:
                fn = [](double x, double y) { return x <= y ;};
                break;

            case OperatorComparison::LOWER_S:
                fn = [](double x, double y) { return x < y ;};
                break;

            default:
                throw std::runtime_error("Invalid Comparison operator");
            }

            return Var::make(Types::Value::make_bool(fn(left, right)));
        } else if (context->operatorAnd()) {
            Var leftV = visit(context->exp()[0]).as<Var>();
            bool left = leftV.as_bool_weak();
            Var rightV = visit(context->exp()[1]).as<Var>();
            // bool right = rightV->as_bool_weak();

            // Decrease a single reference counter here because the value is
            // returned by reference.
            if (left) {
                return rightV;
            } else {
                return leftV; // false and nil == false, nil and false == nil
            }
        } else if (context->operatorOr()) {
            Var leftV = visit(context->exp()[0]).as<Var>();
            Var rightV = visit(context->exp()[1]).as<Var>();

            bool left = leftV.as_bool_weak();
            // bool right = rightV.as_bool_weak();

            if (left) {
                return leftV;
            } else {
                return rightV; // false or nil == nil, nil or false == false
            }
        } else if (LuaParser::OperatorBitwiseContext* ctx = context->operatorBitwise()) {
            // Promote exact floats / strings representing exact floats to int
            // for bitwise operations.
            Var leftV = visit(context->exp()[0]).as<Var>();
            Var rightV = visit(context->exp()[1]).as<Var>();

            int left = leftV.as_int_weak();
            int right = rightV.as_int_weak();

            Var result;

            OperatorBitwise op = visit(ctx).as<OperatorBitwise>();
            switch (op) {
            case OperatorBitwise::AND:
                result = Var::make(Types::Value::make_int(left & right));
                break;

            case OperatorBitwise::LSHIFT:
                result = Var::make(Types::Value::make_int(left << right));
                break;

            case OperatorBitwise::OR:
                result = Var::make(Types::Value::make_int(left | right));
                break;

            case OperatorBitwise::RSHIFT:
                result = Var::make(Types::Value::make_int(left >> right));
                break;

            case OperatorBitwise::XOR:
                result = Var::make(Types::Value::make_int(left ^ right));
                break;

            default:
                throw std::runtime_error("Invalid Bitwise operator");
            }

            return result;
        } else {
            throw std::runtime_error("Invalid expression");
        }
    }

    virtual antlrcpp::Any visitPrefixexp(LuaParser::PrefixexpContext *context) {
        Var val = visit(context->varOrExp()).as<Var>();
        return val;
    }

    virtual antlrcpp::Any visitFunctioncall(LuaParser::FunctioncallContext *context) {
        if (!context->varOrExp()->var_()) {
            return nyi("Functioncall#1");
        }

        LuaParser::Var_Context* var_context = context->varOrExp()->var_();
        std::string funcname(var_context->NAME()->getText());

        std::vector<std::string> allowed_names = {
            "ensure_value_type",
            "expect_failure",
            "print",
            "globals",
            "locals",
            "memory"
        };

        if (!std::any_of(allowed_names.begin(), allowed_names.end(), [funcname](const std::string& str) {
            return funcname == str;
        })) {
            return nyi("Functioncall#2");
        }

        if (funcname == "ensure_value_type") {
            std::string expression(context->nameAndArgs()[0]->args()->explist()->exp()[0]->getText());
            Var left = visit(context->nameAndArgs()[0]->args()->explist()->exp()[0]).as<Var>();
            Var middle = visit(context->nameAndArgs()[0]->args()->explist()->exp()[1]).as<Var>();
            Var right = visit(context->nameAndArgs()[0]->args()->explist()->exp()[2]).as<Var>();

            // Do not attempt to perform equality checks on reference types
            if (!middle.is_refcounted() && left != middle) {
                throw Exceptions::ValueEqualityExpected(expression, middle.value_as_string(), left.value_as_string());
            }
            std::string type(std::move(right.as<std::string>()));
            if (type != "int" && type != "double" && type != "string" && type != "table" && type != "bool" && type != "nil") {
                throw std::runtime_error("Invalid type in ensure_type " + type);
            }

            if ((type == "int" && !left.is<int>()) ||
                (type == "double" && !left.is<double>()) ||
                (type == "string" && !left.is<std::string>()) ||
                (type == "table" && !left.is<Types::Table*>()) ||
                (type == "bool" && !left.is<bool>()) ||
                (type == "nil" && !left.is<Types::Nil>())) {
                throw Exceptions::TypeEqualityExpected(expression, type, left.type_as_string());
            }

            std::cout << "Expression " << expression << " has value " << left.value_as_string() << " of type " << left.type_as_string() << " (expected equivalent of " << middle.value_as_string() << " of type " << type << ") => OK" << std::endl;
        } else if (funcname == "expect_failure") {
            std::string expression(context->nameAndArgs()[0]->args()->explist()->exp()[0]->getText());
            try {
                Var left = visit(context->nameAndArgs()[0]->args()->explist()->exp()[0]).as<Var>();
                throw std::runtime_error("Failure expected in expression " + expression);
            } catch (Exceptions::BadTypeException& e) {
                std::cout << "Expression " << expression << " rightfully triggered a type error" << std::endl;
            } catch (std::exception& e) {
                throw;
            }
        } else if (funcname == "print") {
            Var left = visit(context->nameAndArgs()[0]->args()->explist()->exp()[0]).as<Var>();
            std::cout << left.value_as_string() << std::endl;
        } else if (funcname == "globals") {
            std::cout << "Globals: " << std::endl;
            for (ValueStore::value_type const& v: _global_values) {
                std::cout << v.first << ": " << v.second.value_as_string() << std::endl;
            }
            std::cout << std::endl;
        } else if (funcname == "locals") {
            std::cout << "Locals (top block): " << std::endl;
            for (ValueStore::value_type const& v: _local_values.back()) {
                std::cout << v.first << ": " << v.second.value_as_string() << std::endl;
            }
            std::cout << std::endl;
        } else if (funcname == "memory") {
            std::cout << "Globals: " << std::endl;
            for (ValueStore::value_type const& v: _global_values) {
                std::cout << "\t" << v.first << ": " << v.second.value_as_string() << std::endl;
            }
            std::cout << std::endl;

            for (int i = 0; i < _local_values.size(); ++i) {
                std::cout << "Locals (block " << i << "): " << std::endl;
                for (ValueStore::value_type const& v: _local_values[i]) {
                    std::cout << "\t" << v.first << ": " << v.second.value_as_string() << std::endl;
                }
            }
        }

        return Var::make(Types::Value::make_nil());
    }

    virtual antlrcpp::Any visitVarOrExp(LuaParser::VarOrExpContext *context) {
        Var result;
        if (context->var_()) {
            result = visit(context->var_()).as<Var>();
        } else if (context->exp()) {
            result = visit(context->exp()).as<Var>();
        }

        return result;
    }

    virtual antlrcpp::Any visitVar_(LuaParser::Var_Context *context) {
        Var result;
        Var suffix_start;
        Var expr;

        if (context->NAME()) {
            std::string name(context->NAME()->getText());
            if (context->varSuffix().size() == 0) {
                if (_var__contex == Var_Context::VARLIST) {
                    std::pair<Types::Value*, Scope> value = lookup_name(name, false);
                    if (!value.first) {
                        _global_values[name] = Types::Value::_nil;
                        result = Var::make(&(_global_values[name]));
                    } else {
                        result = Var::make(value.first);
                    }
                } else {
                    // result = Var::make(&(_global_values[context->NAME()->getText()]));
                    result = Var::make(lookup_name(name).first);
                }

                return result;
            } else {
                suffix_start = Var::make(lookup_name(name).first);
            }
        } else {
            suffix_start = visit(context->exp()).as<Var>();
        }

        for (LuaParser::VarSuffixContext* ctx: context->varSuffix()) {
            if (!suffix_start.has_dot()) {
                throw Exceptions::BadDotAccess(suffix_start.type_as_string());
            }

            Suffix suffix = visit(ctx).as<Suffix>();
            if (is_error(suffix)) {
                result = Var::make(var_error_t);
                return result;
            } else if (Subscript* subscript = std::get_if<Subscript>(&suffix)) {
                suffix_start = Var::make(&(suffix_start.subscript(subscript->_value.get())));
            } else {
                suffix_start = Var::make(&(suffix_start.dot(std::get<std::string>(suffix))));
            }
        }

        result = suffix_start;
        return result;
    }

    virtual antlrcpp::Any visitVarSuffix(LuaParser::VarSuffixContext *context) {
        Suffix result;
        if (context->nameAndArgs().size() != 0) {
            std::cout << "Can't call function on table / userdata yet" << std::endl;
            result = var_error_t;
        } else {
            if (context->NAME()) {
                result = context->NAME()->getText();
            } else {
                Subscript sub;
                sub._value = visit(context->exp()).as<Var>();
                result = sub;
            }
        }

        return result;
    }

    virtual antlrcpp::Any visitNameAndArgs(LuaParser::NameAndArgsContext *context) {
        return nyi("NameAndArgs");
    }

    virtual antlrcpp::Any visitArgs(LuaParser::ArgsContext *context) {
        return nyi("Args");
    }

    virtual antlrcpp::Any visitFunctiondef(LuaParser::FunctiondefContext *context) {
        return nyi("Functiondef");
    }

    virtual antlrcpp::Any visitFuncbody(LuaParser::FuncbodyContext *context) {
        return nyi("Funcbody");
    }

    virtual antlrcpp::Any visitParlist(LuaParser::ParlistContext *context) {
        return nyi("Parlist");
    }

    virtual antlrcpp::Any visitTableconstructor(LuaParser::TableconstructorContext *context) {
        std::list<std::pair<Types::Value, Types::Value>> values;

        if (LuaParser::FieldlistContext* ctx = context->fieldlist()) {
            values = visit(ctx).as<std::list<std::pair<Types::Value, Types::Value>>>();
        }

        return Var::make(Types::Value::make_table(values));
    }

    virtual antlrcpp::Any visitFieldlist(LuaParser::FieldlistContext *context) {
        std::list<std::pair<Types::Value, Types::Value>> values;
        unsigned int index = 1;
        for (LuaParser::FieldContext* ctx: context->field()) {
            std::pair<std::optional<Var>, Var> value = visit(ctx).as<std::pair<std::optional<Var>, Var>>();
            if (value.first && value.first->is<Types::Nil>()) {
                continue;
            }

            // If an integer key was already specified, and a value without a
            // key appears, use the next integer available as key, even if it
            // means overriding a value.
            if (!value.first) {
                if (value.second.is<Types::Nil>()) {
                    index++;
                    continue;
                }
                Types::Value key = Types::Value::make_int(index++);
                values.push_back(std::make_pair(key, value.second.get()));
            } else {
                values.push_back(std::make_pair(value.first->get(), value.second.get()));
            }
        }

        return values;
    }

    virtual antlrcpp::Any visitField(LuaParser::FieldContext *context) {
        std::string text(context->getText());

        if (text.starts_with("[")) {
            std::cout << "[NYI] Expressions as keys" << std::endl;
            return std::make_pair(std::make_optional(Var::make(Types::Value::make_nil())), Var::make(Types::Value::make_nil()));
        } else if (context->NAME()) {
            return std::make_pair(std::make_optional(Var::make(Types::Value::make_string(context->NAME()->getText()))), visit(context->exp()[0]).as<Var>());
        } else {
            return std::make_pair((std::optional<Var>)std::nullopt, visit(context->exp()[0]).as<Var>());
        }
    }

    virtual antlrcpp::Any visitFieldsep(LuaParser::FieldsepContext *) {
        return nullptr;
    }

    virtual antlrcpp::Any visitOperatorOr(LuaParser::OperatorOrContext *) {
        return nullptr;
    }

    virtual antlrcpp::Any visitOperatorAnd(LuaParser::OperatorAndContext *) {
        return nullptr;
    }

    virtual antlrcpp::Any visitOperatorComparison(LuaParser::OperatorComparisonContext *context) {
        std::string symbol(context->getText());

        if (symbol == "<") {
            return OperatorComparison::LOWER_S;
        } else if (symbol == ">") {
            return OperatorComparison::GREATER_S;
        } else if (symbol == "<=") {
            return OperatorComparison::LOWER_E;
        } else if (symbol == ">=") {
            return OperatorComparison::GREATER_E;
        } else if (symbol == "~=") {
            return OperatorComparison::DIFF;
        } else if (symbol == "==") {
            return OperatorComparison::EQ;
        } else {
            return OperatorComparison::ERROR;
        }
    }

    virtual antlrcpp::Any visitOperatorStrcat(LuaParser::OperatorStrcatContext *) {
        return nullptr;
    }

    virtual antlrcpp::Any visitOperatorAddSub(LuaParser::OperatorAddSubContext *context) {
        std::string symbol(context->getText());

        if (symbol == "+") {
            return OperatorAddSub::ADD;
        } else if (symbol == "-") {
            return OperatorAddSub::SUB;
        } else {
            return OperatorAddSub::ERROR;
        }
    }

    virtual antlrcpp::Any visitOperatorMulDivMod(LuaParser::OperatorMulDivModContext *context) {
        std::string symbol(context->getText());

        if (symbol == "*") {
            return OperatorMulDivMod::MUL;
        } else if (symbol == "/") {
            return OperatorMulDivMod::DIV;
        } else if (symbol == "%") {
            return OperatorMulDivMod::MOD;
        } else if (symbol == "//") {
            return OperatorMulDivMod::QUOT;
        } else {
            return OperatorMulDivMod::ERROR;
        }
    }

    virtual antlrcpp::Any visitOperatorBitwise(LuaParser::OperatorBitwiseContext *context) {
        std::string symbol(context->getText());

        if (symbol == "&") {
            return OperatorBitwise::AND;
        } else if (symbol == "|") {
            return OperatorBitwise::OR;
        } else if (symbol == "~") {
            return OperatorBitwise::XOR;
        } else if (symbol == "<<") {
            return OperatorBitwise::LSHIFT;
        } else if (symbol == ">>") {
            return OperatorBitwise::RSHIFT;
        } else {
            return OperatorBitwise::ERROR;
        }
    }

    virtual antlrcpp::Any visitOperatorUnary(LuaParser::OperatorUnaryContext *context) {
        std::string symbol(context->getText());

        if (symbol == "not") {
            return OperatorUnary::NOT;
        } else if (symbol == "#") {
            return OperatorUnary::BANG;
        } else if (symbol == "-") {
            return OperatorUnary::MINUS;
        } else if (symbol == "~") {
            return OperatorUnary::BIN_NOT;
        } else {
            return OperatorUnary::ERROR;
        }
    }

    virtual antlrcpp::Any visitOperatorPower(LuaParser::OperatorPowerContext *) {
        return nullptr;
    }

    virtual antlrcpp::Any visitNumber(LuaParser::NumberContext *context) {
        if (auto ptr = context->INT()) {
            return Var::make(Types::Value::make_int(std::stoi(ptr->getText())));
        } else if (auto ptr = context->HEX()) {
            return Var::make(Types::Value::make_int(std::stoi(ptr->getText(), nullptr, 16)));
        } else if (auto ptr = context->FLOAT()) {
            return Var::make(Types::Value::make_double(std::stod(ptr->getText())));
        } else if (auto ptr = context->HEX_FLOAT()) {
            std::cout << "[NYI] Hexfloat" << std::endl;
            return Var::make(Types::Value::make_double(std::stod(ptr->getText())));
        } else {
            throw std::runtime_error("Invalid number");
        }
    }

    virtual antlrcpp::Any visitString(LuaParser::StringContext *context) {
        if (auto ptr = context->NORMALSTRING()) {
            std::string text(ptr->getText());
            return Var::make(Types::Value::make_string(text.substr(1, text.size() - 2)));
        } else if (auto ptr = context->CHARSTRING()) {
            std::string text(ptr->getText());
            return Var::make(Types::Value::make_string(text.substr(1, text.size() - 2)));
        } else if (auto ptr = context->LONGSTRING()) {
            std::cout << "[NYI] Longstring" << std::endl;
            return Var::make(Types::Value::make_string(ptr->getText()));
        } else {
            throw std::runtime_error("Invalid string");
        }
    }

private:
    enum class OperatorComparison {
        LOWER_S,
        GREATER_S,
        LOWER_E,
        GREATER_E,
        DIFF,
        EQ,
        ERROR
    };

    enum class OperatorAddSub {
        ADD,
        SUB,
        ERROR
    };

    enum class OperatorMulDivMod {
        MUL,
        DIV,
        MOD,
        QUOT,
        ERROR
    };

    enum class OperatorBitwise {
        AND,
        OR,
        XOR,
        LSHIFT,
        RSHIFT,
        ERROR
    };

    enum class OperatorUnary {
        NOT,
        BANG,
        MINUS,
        BIN_NOT,
        ERROR
    };

    enum class Var_Context {
        VARLIST,
        OTHER
    };

    enum class Scope {
        GLOBAL, // In the global scope (not local)
        LOCAL, // In the current block
        DEPENDANT // Context dependant, for example in a previous block in the
                  // same lexical scope
    };

    Var_Context _var__contex = Var_Context::OTHER;

    class VarError {};
    VarError var_error_t;

    /* A free value (rvalue); a bound value (lvalue); an error */
    typedef std::variant<Types::Value, Types::Value*, VarError> VarElements;
    struct Var {
    public:
        template<typename T>
        T as() const {
            if  (rvalue()) {
                return _rvalue().as<T>();
            } else if  (lvalue()) {
                return _lvalue()->as<T>();
            } else {
                _error();
            }
        }

        Types::Value get() const {
            if (lvalue()) {
                return *_lvalue();
            } else if (rvalue()) {
                return _rvalue();
            } else {
                _error();
            }
        }

        constexpr Types::Value* _lvalue() const {
            return std::get<Types::Value*>(_value);
        }

        constexpr Types::Value const& _rvalue() const {
            return std::get<Types::Value>(_value);
        }

        constexpr Types::Value* _lvalue() {
            return std::get<Types::Value*>(_value);
        }

        constexpr Types::Value& _rvalue() {
            return std::get<Types::Value>(_value);
        }

        constexpr bool lvalue() const {
            return std::holds_alternative<Types::Value*>(_value);
        }

        constexpr bool rvalue() const {
            return std::holds_alternative<Types::Value>(_value);
        }

        void morph() {
            if (!lvalue()) {
                throw std::runtime_error("Cannot morph a Var that doesn't hold an lvalue");
            }

            _value = *_lvalue();
        }

        template<typename T>
        void set(T&& t) {
            _value = std::move(t);
        }

         bool error() const {
            return std::holds_alternative<VarError>(_value);
        }

        template<typename T>
        static Var make(T&& value) {
            Var v;
            v._value = std::move(value);
            return v;
        }

        double as_double_weak() const {
            if  (rvalue()) {
                return _rvalue().as_double_weak();
            } else if  (lvalue()) {
                return _lvalue()->as_double_weak();
            } else {
                _error();
            }
        }

        int as_int_weak(bool allow_double = true) const {
            if  (rvalue()) {
                return _rvalue().as_int_weak(allow_double);
            } else if  (lvalue()) {
                return _lvalue()->as_int_weak(allow_double);
            } else {
                _error();
            }
        }

        bool as_bool_weak() const {
            if  (rvalue()) {
                return _rvalue().as_bool_weak();
            } else if  (lvalue()) {
                return _lvalue()->as_bool_weak();
            } else {
                _error();
            }
        }

        std::string as_string() const {
            if  (rvalue()) {
                return _rvalue().as_string();
            } else if  (lvalue()) {
                return _lvalue()->as_string();
            } else {
                _error();
            }
        }

        Var() { }

        Var(Var const& other) {
            *this = other;
        }

        Var& operator=(Var const& other) {
            if (other.lvalue()) {
                _value = other._lvalue();
            } else if (other.rvalue()) {
                _value = other.get();
            } else {
                _error();
            }

            return *this;
        }

        bool operator==(const Var& other) const {
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

        bool operator!=(const Var& other) const {
            return !(*this == other);
        }

        bool has_dot() const {
            if  (lvalue()) {
                return _lvalue()->has_dot();
            } else if  (rvalue()) {
                return _rvalue().has_dot();
            } else {
                _error();
            }
        }

        template<typename T>
        bool is() const {
            if  (lvalue()) {
                return _lvalue()->is<T>();
            } else if  (rvalue()) {
                return _rvalue().is<T>();
            } else {
                _error();
            }
        }

        bool is_refcounted() const {
            if  (lvalue()) {
                return _lvalue()->is_refcounted();
            } else if (rvalue()) {
                return _rvalue().is_refcounted();
            } else {
                _error();
            }
        }

        Types::Value from_string_to_number(bool force_double = false) const {
            if  (lvalue()) {
                return _lvalue()->from_string_to_number(force_double);
            } else if  (rvalue()) {
                return _rvalue().from_string_to_number(force_double);
            } else {
                _error();
            }
        }

        std::string type_as_string() const {
            if  (lvalue()) {
                return _lvalue()->type_as_string();
            } else if  (rvalue()) {
                return _rvalue().type_as_string();
            } else {
                _error();
            }
        }

        std::string value_as_string() const {
            if  (lvalue()) {
                return _lvalue()->value_as_string();
            } else if  (rvalue()) {
                return _rvalue().value_as_string();
            } else {
                _error();
            }
        }

        Types::Value& subscript(Types::Value const& value) {
            if  (lvalue()) {
                return _lvalue()->subscript(value);
            } else if  (rvalue()) {
                return _rvalue().subscript(value);
            } else {
                _error();
            }
        }

        Types::Value& dot(std::string const& s) {
            if  (lvalue()) {
                return _lvalue()->dot(s);
            } else if  (rvalue()) {
                return _rvalue().dot(s);
            } else {
                _error();
            }
        }

        Types::LuaValue& value() {
            if  (lvalue()) {
                return _lvalue()->value();
            } else if  (rvalue()) {
                return _rvalue().value();
            } else {
                _error();
            }
        }

    private:
        VarElements _value;

        [[noreturn]] void _error() const {
            throw std::runtime_error("Attempted to access an errored value");
        }
    };

    struct Subscript {
        Var _value;
    };

    typedef std::variant<Subscript, std::string, VarError> Suffix;

    template<typename... Args>
    constexpr bool is_error(std::variant<Args...> const& a) {
        return std::holds_alternative<VarError>(a);
    }

    void process_stat_var_list(LuaParser::VarlistContext* varlist, LuaParser::ExplistContext* explist) {
        std::vector<Var> vars = visit(varlist).as<std::vector<Var>>();
        std::vector<Var> exprs = visit(explist).as<std::vector<Var>>();

        for (Var& v: std::views::filter(exprs, [](Var const& v) { return v.lvalue(); })) {
            v.morph();
        }

        for (int i = 0; i < vars.size(); ++i) {
            if (!vars[i].lvalue()) {
                throw std::runtime_error("How the hell did you arrive here ?");
            }

            if (i >= exprs.size()) {
                vars[i]._lvalue()->value() = Types::Nil();
                continue;
            }
            vars[i]._lvalue()->value() = exprs[i].get().value();
        }
    }

    void process_break() {

    }

    void process_goto(LuaParser::LabelContext* label) {

    }

    void process_while(LuaParser::ExpContext* exp, LuaParser::BlockContext* block) {

    }

    void process_repeat(LuaParser::BlockContext* block, LuaParser::ExpContext* exp) {

    }

    void process_if(LuaParser::StatContext* ctx) {

    }

    void process_for_in(LuaParser::NamelistContext* nl, LuaParser::ExplistContext* el, LuaParser::BlockContext* block) {

    }

    void process_for_loop(LuaParser::StatContext* ctx) {

    }

    void process_function(LuaParser::FuncnameContext* name, LuaParser::FuncbodyContext* body) {

    }

    void process_local_variables(LuaParser::AttnamelistContext* al, LuaParser::ExplistContext* el) {
        std::vector<std::string> names = visit(al).as<std::vector<std::string>>();
        std::vector<Var> values;

        if (el) {
             values = visit(el).as<std::vector<Var>>();
        } else {
            values.resize(names.size(), Var::make(Types::Value::make_nil()));
        }

        auto names_iter = names.begin();
        auto values_iter = values.begin();

        for (; names_iter != names.end() && values_iter != values.end(); ++names_iter, ++values_iter) {
            // Apparently Lua allows local a = 12; local a = 12...
            /* if (_local_values.top().find(*names_iter) != _local_values.top().end()) {
                throw Exceptions::NameAlreadyUsedException(*names_iter);
            } */

            _local_values.back()[*names_iter] = values_iter->get();
        }

        if (names_iter != names.end()) {
            for (; names_iter != names.end(); ++names_iter) {
                _local_values.back()[*names_iter] = Types::Value::_nil;
            }
        }
    }

    void process_local_function(std::string const& name, LuaParser::FuncbodyContext* body) {

    }

    Var nyi(std::string const& str) {
        std::cout << "[NYI] " << str << std::endl;
        return Var::make(Types::Value::make_nil());
    }

    std::pair<Types::Value*, Scope> lookup_name(std::string const& name, bool should_throw = true) {
        auto local_iter = _local_values.back().find(name);
        if (local_iter == _local_values.back().end()) {
            auto global_iter = _global_values.find(name);
            if (global_iter == _global_values.end()) {
                if (should_throw) {
                    throw Exceptions::NilDot();
                } else {
                    return std::make_pair(nullptr, Scope::GLOBAL);
                }
            } else {
                return std::make_pair(&(global_iter->second), Scope::GLOBAL);
            }
        } else {
            return std::make_pair(&(local_iter->second), Scope::LOCAL);
        }
    }

    typedef std::map<std::string, Types::Value> ValueStore;

    // Scope processing works like a stack. Each time a new scope is
    // entered, push a new map on top of the stack to store the local
    // Values of the scope. Once the scope is exited, pop this map from
    // the stack.
    std::vector<ValueStore> _local_values;
    ValueStore _global_values;
};

class FailureExpected : public std::exception {
public:
    FailureExpected(std::string const& file) {
        std::ostringstream error;
        error << "Expected failure while running file " << file << std::endl;
        _error = error.str();
    }

    const char* what() const noexcept {
        return _error.c_str();
    }

private:
    std::string _error;
};

void run_test(std::string const& path) {
    std::ifstream stream(path, std::ios::in);

    if (!stream) {
        return;
    }

    /* std::string expected;
    stream >> expected;

    if (expected != "fail" && expected != "success") {
        std::ostringstream error;
        error << "File " << path.string() << " has invalid indicator as first line: " << expected << std::endl;
        throw std::runtime_error(error.str());
    } */

    antlr4::ANTLRInputStream input(stream);
    LuaLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    LuaParser parser(&tokens);
    if (parser.getNumberOfSyntaxErrors()) {
        throw std::runtime_error("Errors encountered while processing file " + path + "\n");
    }

    antlr4::tree::ParseTree* tree = parser.chunk();
    try {
        MyLuaVisitor visitor(tree);
        visitor.visit(tree);
        std::cout << "[OK] " << path << std::endl;
    } catch (std::exception& e) {
        std::ostringstream stream;
        stream << "Caught unexpected exception while processing file: " << path <<
                  std::endl << "What: " << e.what() << std::endl;
        throw std::runtime_error(stream.str());
    }
}

void tests() {
    for (auto& p: fs::recursive_directory_iterator("tests")) {
        if (p.is_directory() || p.path().string()[0] == '.' || p.path().extension() != ".lua") {
            // std::cout << p.path().extension() << std::endl;
            continue;
        }

        auto v = (p.path() | std::views::reverse).begin();
        ++v;

        if (*v == "00_goto")
            continue;
        run_test(p.path().string());
    }
}

class GotoResultExpected : public std::exception {
public:
    GotoResultExpected(const std::string& path, const std::string& expected, const std::string& received) {
        _error = "Goto result of kind " + expected + " was expected in file + " + path + ", received " + received + "\n";
    }

    const char* what() const noexcept {
        return _error.c_str();
    }

private:
    std::string _error;
};

void run_goto_test(std::string const& path) {
    std::ifstream stream(path, std::ios::in);

    if (!stream) {
        return;
    }

    std::string header;
    stream >> header;

    if (header != "success" && header != "crossed" && header != "invisible") {
        throw std::runtime_error("Unknown goto result " + header);
    }

    antlr4::ANTLRInputStream input(stream);
    LuaLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    LuaParser parser(&tokens);
    if (parser.getNumberOfSyntaxErrors()) {
        throw std::runtime_error("Errors encountered while processing file " + path + "\n");
    }

    antlr4::tree::ParseTree* tree = parser.chunk();
    try {
        GotoListener listener;
        antlr4::tree::ParseTreeWalker::DEFAULT.walk(&listener, tree);
        listener.validate();

        if (header != "success") {
            throw GotoResultExpected(path, header, "success");
        }
    } catch (Exceptions::CrossedLocal& crossed) {
        if (header != "crossed") {
            throw GotoResultExpected(path, header, "crossed");
        }
    } catch (Exceptions::InvisibleLabel& invisible) {
        if (header != "invisible") {
            throw GotoResultExpected(path, header, "invisible");
        }
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        throw;
    }

    std::cout << "[OK] " << path << std::endl;
}

void test_goto() {
    for (auto& p: fs::directory_iterator("tests/00_goto")) {
        if (p.path().string()[0] == '.' || p.path().extension() != ".lua") {
            continue;
        }

        run_goto_test(p.path().string());
    }
}

struct CLIArgs {
    bool _test = false;
    std::string _test_file;
    bool _base = false;
    bool _goto = false;
    std::string _goto_file;
};

void parse_args(int argc, char** argv, CLIArgs& args) {
    po::options_description options("All options");
    options.add_options()
            ("help", "Display this help and exit")
            ("test", po::value<std::string>()->implicit_value(""), "Run all tests, or on the given file only")
            ("base", "Run the base file to get the AST")
            ("goto", po::value<std::string>()->implicit_value(""), "Run tests on the goto directory with listener only, or only on the given file");
    po::variables_map vm;
    po::command_line_parser parser(argc, argv);
    parser.options(options);
    po::store(parser.run(), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << options << std::endl;
        std::exit(0);
    }

    if (vm.count("test")) {
        args._test = true;
        args._test_file = vm["test"].as<std::string>();
    }

    if (vm.count("base")) {
        args._base = true;
    }

    if (vm.count("goto")) {
        args._goto = true;
        args._goto_file = vm["goto"].as<std::string>();
    }
}

int main(int argc, char** argv) {
    /* make_test_data<false, void>("parse_test.lua")->run();
    make_test_data<true, Exceptions::ContextlessBadTypeException>("parse_test2.lua")->run();
    make_test_data<false, void>("parse_test3.lua")->run(); */
    Types::Value::init();

    CLIArgs args;
    parse_args(argc, argv, args);

    if (args._base) {
        std::ifstream stream("parse_base.lua");
        antlr4::ANTLRInputStream input(stream);
        LuaLexer lexer(&input);
        antlr4::CommonTokenStream tokens(&lexer);
        LuaParser parser(&tokens);
        if (parser.getNumberOfSyntaxErrors()) {
            throw std::runtime_error("Errors encountered while processing file parse_base.lua \n");
        }

        antlr4::tree::ParseTree* tree = parser.chunk();
        std::cout << tree->toStringTree(&parser, true) << std::endl;

        try {
            MyLuaVisitor visitor(tree);
            visitor.visit(tree);
        } catch (std::exception& e) {
            std::cerr << "Parse base error: " << e.what() << std::endl;
        }
    }

    if (args._goto) {
        if (!args._goto_file.empty()) {
            run_goto_test(args._goto_file);
        } else {
            test_goto();
        }
    }

    if (args._test) {
        if (!args._test_file.empty()) {
            run_test(args._test_file);
        } else {
            tests();
        }
    }

    return 0;
}
