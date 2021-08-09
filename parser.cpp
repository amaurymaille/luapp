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

    class LonelyBreak : public std::exception {
    public:
        LonelyBreak(int line) {
            std::ostringstream error;
            error << "Lonely break found on line " << line  << "\n";
            _error = error.str();
        }

        const char* what() const noexcept {
            return "Lonely break found\n";
        }

    private:
        std::string _error;
    };

    class LabelAlreadyDefined : public std::exception {
    public:
        LabelAlreadyDefined(std::string const& label) {
            _error = "Label " + label + " already defined\n";
        }

        const char* what() const noexcept {
            return _error.c_str();
        }
    private:
        std::string _error;
    };

    class Break : public std::exception {
    public:
        Break() { }

        const char* what() const noexcept { return ""; }
    };

    class StackCorruption : public std::exception {
    public:
        StackCorruption(int expected, int received) {
            std::ostringstream error;
            error << "Stack corruption detected: expected at least " << expected << " frames, got " << received << std::endl;
            _error = error.str();
        }

        const char* what() const noexcept {
            return _error.c_str();
        }

    private:
        std::string _error;
    };

    class Goto : public std::exception {
    public:
        Goto(std::string const& label) : _label(label) { }

        const char* what() const noexcept { return ""; }

        std::string const& get() const { return _label; }

    private:
        std::string const& _label;
    };

    class BadCall : public std::exception {
    public:
        BadCall(std::string const& type) {
            _error = "Attempted to call a function on " + type;
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
    public:
        Elipsis(std::vector<Types::Value> const& values) : _values(values) { }

        Elipsis(Elipsis const& other) : _values(other._values) {
            // throw std::runtime_error("Copying elipsis is illegal. I think ?");
        }

        Elipsis& operator=(Elipsis const& other) {
            _values = other._values;
            return *this;
            // throw std::runtime_error("Copying elipsis is illegal. I think ?");
        }

        bool operator==(const Elipsis&) const {
            return false;
        }

        bool operator!=(const Elipsis&) const {
            return true;
        }

        bool operator<(const Elipsis&) const {
            return true;
        }

        std::vector<Types::Value> const& values() const { return _values; }

    private:
        std::vector<Types::Value> _values;
    };

    typedef std::variant<bool, int, double, std::string, Nil, Elipsis, Function*, Userdata*, Table*> LuaValue;

    class Function {
    public:
        Function(std::vector<std::string>&& formal_parameters, LuaParser::BlockContext* body) :
            _body(body), _formal_parameters(std::move(formal_parameters)) {

        }

        ~Function();

        bool operator==(const Function& other) const {
            return this == &other;
        }

        bool operator!=(const Function& other) const {
            return this != &other;
        }

        void close(std::string const& name, Value* value);
        std::map<std::string, Value*> const& closure() const { return _closure; }
        LuaParser::BlockContext* get_context() const { return _body; }
        std::vector<std::string> const& formal_parameters() const { return _formal_parameters; }

    private:
        // Values under which the function is closed.
        // Context not preserved because only names are required.
        std::map<std::string, Value*> _closure;
        LuaParser::BlockContext* _body;
        std::vector<std::string> _formal_parameters;
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
        void add_field(std::string const& name, Value const& value);
        void add_field(Value const& source, Value const& dst);

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
            // _elipsis._type = Elipsis();
        }

        friend Table::Table(const std::list<std::pair<Value, Value>>&);
        friend void Table::add_field(const Value&, const Value&);
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
        T& as() {
            return std::get<T>(_type);
        }

        template<typename T>
        T const& as() const {
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
            } else if (is<Nil>()) {
                return "nil";
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

        // static Value make_elipsis() { return  _elipsis; }

        static Value make_table(std::list<std::pair<Value, Value>> const& values) {
            Value v;
            alloc<Table>(v, values);
            sGC->add_reference(v._type);
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

        LuaValue const& value() const { return _type; }
        void add_reference() {
            ++_references;
        }

        void remove_reference() {
            if (_references == 0) {
                throw std::runtime_error("Removing reference on object with no references");
            }

            --_references;
            if (_references == 0) {
                delete this;
            }
        }

    private:
        LuaValue _type;

        static Value _nil;
        // static Value _elipsis;
        static Value _true;
        static Value _false;

        unsigned int _references = 1;
    };

    class VarError {};
    VarError var_error_t;

    /* A free value (rvalue); a bound value (lvalue); an error */
    typedef std::variant<Value, Value*, std::vector<Value>, VarError> VarElements;
    struct Var {
    public:
        template<typename T>
        T as() const {
            if  (rvalue()) {
                return _rvalue().as<T>();
            } else if  (lvalue()) {
                return _lvalue()->as<T>();
            } else if (list()) {
                return _list()[0].as<T>();
            } else {
                _error();
            }
        }

        Value get() const {
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

        constexpr Value* _lvalue() const {
            return std::get<Value*>(_value);
        }

        constexpr Value const& _rvalue() const {
            return std::get<Value>(_value);
        }

        constexpr Value* _lvalue() {
            return std::get<Value*>(_value);
        }

        constexpr Value& _rvalue() {
            return std::get<Value>(_value);
        }

        constexpr std::vector<Value>& _list() {
            return std::get<std::vector<Value>>(_value);
        }

        constexpr std::vector<Value> const& _list() const {
            return std::get<std::vector<Value>>(_value);
        }

        constexpr bool lvalue() const {
            return std::holds_alternative<Value*>(_value);
        }

        constexpr bool rvalue() const {
            return std::holds_alternative<Value>(_value);
        }

        constexpr bool list() const {
            return std::holds_alternative<std::vector<Value>>(_value);
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
            } else if (list()) {
                return _list()[0].as_double_weak();
            } else {
                _error();
            }
        }

        int as_int_weak(bool allow_double = true) const {
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

        bool as_bool_weak() const {
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

        std::string as_string() const {
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

        Var() { }

        Var(Var const& other) {
            *this = other;
        }

        Var& operator=(Var const& other) {
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
            } else if (list()) {
                return _list()[0].has_dot();
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
            } else if (list()) {
                return _list()[0].is<T>();
            } else {
                _error();
            }
        }

        bool is_refcounted() const {
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

        Value from_string_to_number(bool force_double = false) const {
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

        std::string type_as_string() const {
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

        std::string value_as_string() const {
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

        Value& subscript(Value const& value) {
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

        Value& dot(std::string const& s) {
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

        LuaValue& value() {
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

    private:
        VarElements _value;

        [[noreturn]] void _error() const {
            throw std::runtime_error("Attempted to access an errored value");
        }
    };

    Value Value::_nil;
    Value Value::_true;
    Value Value::_false;
    // Value Value::_elipsis;

    Function::~Function() {
        for (Value* v: std::views::values(_closure)) {
            v->remove_reference();
        }
    }

    void Function::close(std::string const& name, Value* value) {
        if (_closure.find(name) != _closure.end()) {
            throw std::runtime_error("Closing function twice under " + name);
        }

        _closure[name] = value;
        value->add_reference();
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

    void Table::add_field(const std::string &name, const Value &value) {
        _string_fields[name] = value;
    }

    void Table::add_field(const Value &source, const Value &dst) {
        std::visit(FieldSetter(*this, dst), source._type);
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

namespace Exceptions {
    class Return : public std::exception {
    public:
        Return(std::vector<Types::Var>&& values) : _values(std::move(values)) {
            /* for (Types::Var& v: values) {
                sGC->add_reference(v.value());
            } */
        }

        std::vector<Types::Var> const& get() const { return _values; }

        const char* what() const noexcept { return ""; }

    private:
        std::vector<Types::Var> _values;
    };
}

/* Helper class used to detect whether goto statements are legit or not.
 * Gotos are lexically scoped but can't cross functions bodies: you can jump
 * to any label that has been defined in a surrounding block, but you can't
 * escape from the boundary of the function you are (maybe in).
 * Additionnaly, gotos cannot cross the declaration of a local variable.
 */
class GotoBreakListener : public LuaBaseListener {
public:
    typedef std::multimap<std::string, LuaParser::BlockContext*> BlocksPerLocal;
    typedef std::multimap<LuaParser::BlockContext*, LuaParser::BlockContext*> FunctionParents;

public:
    GotoBreakListener() {
        _current_context = nullptr;
    }

    void enterChunk(LuaParser::ChunkContext *ctx) {
        _scopes.push_back(Scope());
        _stack_scopes.push(&_scopes.back());
        _current_scope = &_scopes.back();
        _current_scope->_root_context = ctx->block();
        /* Mandatory in order for the pointer to appear in the map, otherwise
         * the program would crash during validation because the context would
         * not be found.
         */
        _current_scope->_scope_elements[ctx->block()];
    }

    void enterBlock(LuaParser::BlockContext *ctx) {
        // std::cout << ctx->getText() << std::endl;
        _blocks.push(ctx);

        if (_current_context) {
            _current_scope->_scope_elements[_current_context].push_back(make_element(ctx));
        }

        _current_scope->_scope_elements[ctx];

        _current_context = _blocks.top();

        // Force an empty vector for this context if it has not locals.
        if (_blocks_relations.empty()) {
            _locals_per_block[ctx];
        } else {
            // Any block has access to all the locals of its parent blocks.
            // Special care required for for loops because they don't use local
            // to start declare their variables, and they declare them outside of
            // the block where they are visible. Same applies to functions'
            // parameters which are local to the function but appear before the
            // body block of the function.
            if (_locals_per_block.find(ctx) != _locals_per_block.end()) { // for, params
                auto& source = _locals_per_block[_blocks_relations.back()];
                for (auto& p: source) {
                    _locals_per_block[ctx].insert(std::make_pair(p.first, p.second));
                }
            } else {
                _locals_per_block[ctx] = _locals_per_block[_blocks_relations.back()];
            }
        }
        _blocks_relations.push_back(ctx);
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

        _loop_blocks.erase(ctx);

        _blocks_relations.pop_back();
    }

    void enterStat(LuaParser::StatContext *ctx) {
        if (!ctx->getText().starts_with("local") &&
                !ctx->getText().starts_with("function") &&
                !ctx->getText().starts_with(("local function")) &&
                !ctx->getText().starts_with(("goto")) &&
                !ctx->getText().starts_with("break") &&
                !ctx->getText().starts_with("for") &&
                !ctx->getText().starts_with("repeat") &&
                !ctx->getText().starts_with("while") &&
                !ctx->label()) {
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
                _locals_per_block[_blocks_relations.back()].insert(std::make_pair(ctx->NAME()->getText(), _blocks_relations.back()));
                // _current_context = nullptr;
            } else {
                LuaParser::AttnamelistContext* lst = ctx->attnamelist();
                for (auto name: lst->NAME()) {
                    _current_scope->_scope_elements[_current_context].push_back(make_element(Local(name->getText())));
                    _locals_per_block[_blocks_relations.back()].insert(std::make_pair(name->getText(), _blocks_relations.back()));
                }
            }
            // Definition of a function
        } else if (ctx->getText().starts_with("function")) {
            _current_scope->_scope_elements[_current_context].push_back(make_element(Local(ctx->funcname()->getText())));
            // _current_context = nullptr;
        } else if (ctx->label()) {
            _label_to_context[ctx->label()->NAME()->getText()].push_back(_current_context);
        } else if (ctx->getText().starts_with("for")) {
            _loop_blocks.insert(ctx->block()[0]);
            if (ctx->explist()) {
                LuaParser::NamelistContext* nl = ctx->namelist();
                std::vector<antlr4::tree::TerminalNode*> nodes(nl->NAME());
                auto v = std::views::transform(nodes, [](antlr4::tree::TerminalNode* t) { return t->getText(); });
                for (std::string const& name: v) {
                    _locals_per_block[ctx->block()[0]].insert(std::make_pair(name, ctx->block()[0]));
                }
            } else {
                _locals_per_block[ctx->block()[0]].insert(std::make_pair(ctx->NAME()->getText(), ctx->block()[0]));
            }
        } else if (ctx->getText().starts_with("while")) {
            _loop_blocks.insert(ctx->block()[0]);
        } else if (ctx->getText().starts_with("repeat")) {
            _loop_blocks.insert(ctx->block()[0]);
        } else {
            // break
            if (_loop_blocks.empty()) {
                throw Exceptions::LonelyBreak(ctx->getStart()->getLine());
            }
        }
    }

    // Beginning of the scope of an inner function
    void enterFuncbody(LuaParser::FuncbodyContext *ctx) override {
        if (ctx->parlist()) {
            if (LuaParser::NamelistContext* nl = ctx->parlist()->namelist()) {
                for (antlr4::tree::TerminalNode* node: nl->NAME()) {
                    _locals_per_block[ctx->block()].insert(std::make_pair(node->getText(), ctx->block()));
                }
            }
        }

        for (LuaParser::BlockContext* blk: _blocks_relations) {
            _functions_parents.insert(std::make_pair(ctx->block(), blk));
        }

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

    void validate_gotos() const {
        std::set<LuaParser::BlockContext*> seen_contexts;

        for (const Scope& scope: _scopes) {
            LuaParser::BlockContext* ctx = scope._root_context;
            validate_labels(scope, ctx, seen_contexts);

            std::vector<std::string> seen_labels;
            std::vector<std::pair<std::vector<ScopeElement>::const_iterator, std::vector<ScopeElement>::const_iterator>> previous;
            explore_context(scope, scope._root_context, seen_labels, previous);
        }
    }

    bool is_associated_with_label(LuaParser::BlockContext const* ctx, std::string const& label) const {
        auto iter = _label_to_context.find(label);
        if (iter == _label_to_context.end()) {
            return false;
        } else {
            return std::find(iter->second.begin(), iter->second.end(), ctx) != iter->second.end();
        }
    }

    std::pair<BlocksPerLocal::iterator, BlocksPerLocal::iterator> get_context_for_local(LuaParser::BlockContext const* ctx, std::string const& name) {
        return _locals_per_block[const_cast<LuaParser::BlockContext*>(ctx)].equal_range(name);
    }

    std::pair<FunctionParents::iterator, FunctionParents::iterator> get_parents_of_function(LuaParser::BlockContext* fnctx) {
        return _functions_parents.equal_range(fnctx);
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

    void validate_labels(Scope const& scope, LuaParser::BlockContext* ctx, std::set<LuaParser::BlockContext*>& seen_contexts) const {
        seen_contexts.insert(ctx);
        std::vector<ScopeElement> const& elements = scope._scope_elements.find(ctx)->second;
        std::set<std::string> labels;
        for (ScopeElement const& element: elements) {
            if (Label const* label = std::get_if<Label>(&element)) {
                if (labels.find(label->_name) != labels.end()) {
                    throw Exceptions::LabelAlreadyDefined(label->_name);
                } else {
                    labels.insert(label->_name);
                }
            } else if (LuaParser::BlockContext* const* blk = std::get_if<LuaParser::BlockContext*>(&element)) {
                if (seen_contexts.find(*blk) == seen_contexts.end()) {
                    validate_labels(scope, *blk, seen_contexts);
                }
            }
        }
    }

    /// Used to check whether gotos are legit or not.
    LuaParser::BlockContext* _current_context;
    std::stack<LuaParser::BlockContext*> _blocks;
    std::list<Scope> _scopes;
    std::stack<Scope*> _stack_scopes;
    Scope* _current_scope;

    /// Used to check whether break statements are legit. A break cannot occur
    /// when this is empty.
    std::set<LuaParser::BlockContext*> _loop_blocks;

    /// Used in interpretation of gotos statement, allow a block to know whether
    /// it is associated with a label or not. A block is associated with a label
    /// if jumping to this label ends in this block.
    std::map<std::string, std::vector<LuaParser::BlockContext*>> _label_to_context;

    /// Used to know which block has access to which locals and in which block.
    /// The vector is used to handle cases where a block redefines an already
    /// existing local: accessing this local before redefining it requires access
    /// to the version found in an older context, while accessing it after having
    /// redefined it will fetch its value from the current context.
    std::map<LuaParser::BlockContext*, BlocksPerLocal> _locals_per_block;
    std::vector<LuaParser::BlockContext*> _blocks_relations;

    /// Used to compute the closure of functions: for each function, list the
    /// blocks this function can see.
    FunctionParents _functions_parents;
};


class MyLuaVisitor : public LuaVisitor {
public:
    MyLuaVisitor(antlr4::tree::ParseTree* tree) {
        antlr4::tree::ParseTreeWalker::DEFAULT.walk(&_listener, tree);
        _listener.validate_gotos();
    }

    ~MyLuaVisitor() {
        for (auto& p: _global_values) {
            p.second->remove_reference();
        }

        for (unsigned int i = 0; i < _local_values.size(); ++i) {
            for (auto& p1: _local_values[i]) {
                for (auto& p2: p1.second) {
                    p2.second->remove_reference();
                }
            }
        }
    }

    virtual antlrcpp::Any visitChunk(LuaParser::ChunkContext *context) {
        // std::cout << "Chunk: " << context->getText() << std::endl;
        try {
            _local_values.push_back(decltype(_local_values)::value_type());
            return visit(context->block());
        } catch (Exceptions::Return& ret) {
            return ret.get();
        }
    }

    virtual antlrcpp::Any visitBlock(LuaParser::BlockContext *context) {
        bool coming_from_for = _coming_from_for;
        bool coming_from_funcall = _coming_from_funcall;

        _coming_from_for = false;
        _coming_from_funcall = false;

        if (!coming_from_for && !coming_from_funcall) {
            _blocks.push_back(context);
        }

        antlrcpp::Any retval = Types::Value::make_nil();

        for (unsigned int i = 0; i < context->stat().size(); ) {
            LuaParser::StatContext* ctx = context->stat(i);

            try {
                visit(ctx);
                ++i;
            } catch (Exceptions::Goto& g) {
                stabilize_blocks(context);

                if (_listener.is_associated_with_label(context, g.get())) {
                    ++i; // Move to next statement
                    while (i < context->stat().size()) {
                        ctx = context->stat(i);
                        if (ctx->label() && ctx->label()->getText() == g.get()) {
                            break;
                        }
                        ++i;
                    }
                    ++i; // Move to statement following label
                } else {
                    // Keep going up the AST while trying to find the block in which the label was defined
                    throw;
                }
            }
        }

        if (LuaParser::RetstatContext* ctx = context->retstat()) {
            return visit(ctx); // [[noreturn]]
        }

        if (!coming_from_for) {
            erase_block(context);
        }

        if (_blocks.back() != context) {
            throw std::runtime_error("Unbalanced blocks");
        }

        if (!coming_from_for) {
            _blocks.pop_back();
        }

        return retval;
    }

    virtual antlrcpp::Any visitStat(LuaParser::StatContext *context) {
        // std::cout << "Stat: " << context->getText() << std::endl;
        if (context->getText() == ";") {
            ;
        } else if (context->getText() == "break") {
            process_break();
        } else if (context->getText().starts_with("goto")) {
            process_goto(context->NAME()->getText());
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

        return Types::Var::make(Types::Value::make_nil());
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
        return Types::Var::make(Types::Value::make_nil());
    }

    virtual antlrcpp::Any visitRetstat(LuaParser::RetstatContext *context) {
        std::vector<Types::Var> retval;
        if (LuaParser::ExplistContext* ctx = context->explist()) {
            retval = visit(ctx).as<std::vector<Types::Var>>();
        } else {
            std::vector<Types::Var> v;
            v.push_back(Types::Var::make(Types::Value::make_nil()));
            retval = v;
        }

        throw Exceptions::Return(std::move(retval));
    }

    virtual antlrcpp::Any visitLabel(LuaParser::LabelContext *) {
        return Types::Var::make(Types::Value::make_nil());
    }

    virtual antlrcpp::Any visitFuncname(LuaParser::FuncnameContext *context) {
        std::string full_name(context->getText());
        std::string last_part;
        std::vector<antlr4::tree::TerminalNode*> names(context->NAME());
        Types::Value* source = nullptr;
        Types::Value* table = nullptr;
        for (std::string part: std::views::transform(names, [](antlr4::tree::TerminalNode* node) { return node->getText(); })) {
            if (source == nullptr) {
                source = lookup_name(part, false).first;
                last_part = full_name;
                full_name = full_name.substr(0, part.size());

                if (!source) {
                    if (names.size() != 1) {
                        throw Exceptions::BadDotAccess(source->type_as_string());
                    }

                    break;
                }
            } else {
                if (!source->is<Types::Table*>()) {
                    throw Exceptions::BadDotAccess(source->type_as_string());
                }
                table = source;
                source = &(source->as<Types::Table*>()->dot(part));
                last_part = full_name;
                full_name = full_name.substr(0, part.size() + 1);
            }
        }

        LuaParser::FuncbodyContext* body = dynamic_cast<LuaParser::StatContext*>(context->parent)->funcbody();
        Types::Function* f;
        if (body->parlist()) {
            f = new Types::Function(std::move(visit(body->parlist()).as<std::vector<std::string>>()), body->block());
        } else {
            f = new Types::Function(std::vector<std::string>(), body->block());
        }
        close_function(f, body->block());

        if (source == &Types::Value::_nil) {
            Types::Value func;
            func.value() = f;
            table->as<Types::Table*>()->add_field(last_part, func);
            sGC->add_reference(func.value());
        } else {
            Types::Value* func = new Types::Value();
            func->value() = f;
            _global_values[last_part] = func;
            sGC->add_reference(func->value());
        }

        return Types::Var::make(Types::Value::make_nil());
    }

    virtual antlrcpp::Any visitVarlist(LuaParser::VarlistContext *context) {
        _var__context = Var_Context::VARLIST;
        std::vector<Types::Var> vars;
        for (LuaParser::Var_Context* ctx: context->var_()) {
            vars.push_back(visit(ctx).as<Types::Var>());
        }

        _var__context = Var_Context::OTHER;
        return vars;
    }

    virtual antlrcpp::Any visitNamelist(LuaParser::NamelistContext *context) {
        std::vector<antlr4::tree::TerminalNode*> names(context->NAME());
        std::vector<std::string> retval;

        std::transform(names.begin(), names.end(), std::back_inserter(retval), [](antlr4::tree::TerminalNode* node) { return node->getText(); });

        return retval;
    }

    virtual antlrcpp::Any visitExplist(LuaParser::ExplistContext *context) {
        std::vector<Types::Var> values;
        for (LuaParser::ExpContext* ctx: context->exp()) {
            values.push_back(visit(ctx).as<Types::Var>());
        }

        return values;
    }

    virtual antlrcpp::Any visitExp(LuaParser::ExpContext *context) {
        if (context->getText() == "nil") {
            return Types::Var::make(Types::Value::make_nil());
        } else if (context->getText() == "true") {
            return Types::Var::make(Types::Value::make_true());
        } else if (context->getText() == "false") {
            return Types::Var::make(Types::Value::make_false());
        } else if (context->getText() == "...") {
            return Types::Var::make(_local_values.back()[current_block()]["..."]);
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
            Types::Var leftV = visit(context->exp()[0]).as<Types::Var>();
            double left = leftV.as_double_weak();

            Types::Var rightV = visit(context->exp()[1]).as<Types::Var>();
            double right = rightV.as_double_weak();

            return Types::Var::make(Types::Value::make_double(std::pow(left, right)));
        } else if (LuaParser::OperatorUnaryContext* ctx = context->operatorUnary()) {
            Types::Var v = visit(context->exp()[0]).as<Types::Var>();
            OperatorUnary op = visit(ctx).as<OperatorUnary>();

            switch (op) {
            case OperatorUnary::BANG:
                if (v.is<std::string>()) {
                    return Types::Var::make(Types::Value::make_int(v.as<std::string>().size()));
                } else {
                    return Types::Var::make(Types::Value::make_int(v.as<Types::Table*>()->border()));
                }

            case OperatorUnary::BIN_NOT: {
                 return Types::Var::make(Types::Value::make_int(~v.as_int_weak()));
            }

            case OperatorUnary::MINUS: {
                std::function<Types::Var(Types::Var const&)> convert = [&](Types::Var const& value) -> Types::Var {
                    Types::Var result;
                    if (value.is<int>()) {
                        result = Types::Var::make(Types::Value::make_int(-value.as<int>()));
                    } else if (value.is<double>()) {
                        result = Types::Var::make(Types::Value::make_double(-value.as<double>()));
                    } else {
                        result = convert(Types::Var::make(value.from_string_to_number(true)));
                    }

                    return result;
                };

                return convert(v);
            }

            case OperatorUnary::NOT: {
                return Types::Var::make(Types::Value::make_bool(!v.as_bool_weak()));
            }

            default:
                throw std::runtime_error("Invalid Unary operator");
            }
        } else if (LuaParser::OperatorMulDivModContext* ctx = context->operatorMulDivMod()) {
            Types::Var leftV = visit(context->exp()[0]).as<Types::Var>();
            Types::Var rightV = visit(context->exp()[1]).as<Types::Var>();

            Types::Var result;

            double left = leftV.as_double_weak();
            double right = rightV.as_double_weak();

            OperatorMulDivMod op = visit(ctx).as<OperatorMulDivMod>();
            switch (op) {
            case OperatorMulDivMod::MUL:
                if (leftV.is<int>() && rightV.is<int>()) {
                    result = Types::Var::make(Types::Value::make_int(leftV.as<int>() * rightV.as<int>()));
                } else {
                    result = Types::Var::make(Types::Value::make_double(left * right));
                }
                break;

            case OperatorMulDivMod::DIV:
                result = Types::Var::make(Types::Value::make_double(left / right));
                break;

            case OperatorMulDivMod::MOD:
                if (leftV.is<int>() && rightV.is<int>()) {
                    result = Types::Var::make(Types::Value::make_int(leftV.as<int>() % rightV.as<int>()));
                } else {
                    result = Types::Var::make(Types::Value::make_double(std::remainder(left, right)));
                }
                break;

            case OperatorMulDivMod::QUOT:
                if (leftV.is<int>() && rightV.is<int>()) {
                    result = Types::Var::make(Types::Value::make_int(std::floor(left / right)));
                } else {
                    result = Types::Var::make(Types::Value::make_double(std::floor(left / right)));
                }
                break;

            default:
                throw std::runtime_error("Invalid MulDivMod operator");
            }

            return result;
        } else if (LuaParser::OperatorAddSubContext* ctx = context->operatorAddSub()) {
            Types::Var leftV = visit(context->exp()[0]).as<Types::Var>();
            Types::Var rightV = visit(context->exp()[1]).as<Types::Var>();

            Types::Var result;

            OperatorAddSub op = visit(ctx);
            if (leftV.is<int>() && rightV.is<int>()) {
                int left = leftV.as<int>();
                int right = rightV.as<int>();

                switch (op) {
                case OperatorAddSub::ADD:
                    result = Types::Var::make(Types::Value::make_int(left + right));
                    break;

                case OperatorAddSub::SUB:
                    result = Types::Var::make(Types::Value::make_int(left - right));
                    break;

                default:
                    throw std::runtime_error("Invalid AddSub operator");
                }
            } else {
                double left = leftV.as_double_weak();
                double right = rightV.as_double_weak();

                switch (op) {
                case OperatorAddSub::ADD:
                    result = Types::Var::make(Types::Value::make_double(left + right));
                    break;

                case OperatorAddSub::SUB:
                    result = Types::Var::make(Types::Value::make_double(left - right));
                    break;

                default:
                    throw std::runtime_error("Invalid AddSub operator");
                }
            }

            return result;
        } else if (context->operatorStrcat()) {
            // Promote numbers to strings if necessary.
            // Note that Lua allows two numbers to concatenate as a string.
            Types::Var leftV = visit(context->exp()[0]).as<Types::Var>();
            Types::Var rightV = visit(context->exp()[1]).as<Types::Var>();

            std::string left = leftV.as_string();
            std::string right = rightV.as_string();

            return Types::Var::make(Types::Value::make_string(left + right));
        } else if (LuaParser::OperatorComparisonContext* ctx = context->operatorComparison()) {
            Types::Var leftV = visit(context->exp()[0]).as<Types::Var>();
            Types::Var rightV = visit(context->exp()[1]).as<Types::Var>();

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

            return Types::Var::make(Types::Value::make_bool(fn(left, right)));
        } else if (context->operatorAnd()) {
            Types::Var leftV = visit(context->exp()[0]).as<Types::Var>();
            bool left = leftV.as_bool_weak();
            Types::Var rightV = visit(context->exp()[1]).as<Types::Var>();
            // bool right = rightV->as_bool_weak();

            // Decrease a single reference counter here because the value is
            // returned by reference.
            if (left) {
                return rightV;
            } else {
                return leftV; // false and nil == false, nil and false == nil
            }
        } else if (context->operatorOr()) {
            Types::Var leftV = visit(context->exp()[0]).as<Types::Var>();
            Types::Var rightV = visit(context->exp()[1]).as<Types::Var>();

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
            Types::Var leftV = visit(context->exp()[0]).as<Types::Var>();
            Types::Var rightV = visit(context->exp()[1]).as<Types::Var>();

            int left = leftV.as_int_weak();
            int right = rightV.as_int_weak();

            Types::Var result;

            OperatorBitwise op = visit(ctx).as<OperatorBitwise>();
            switch (op) {
            case OperatorBitwise::AND:
                result = Types::Var::make(Types::Value::make_int(left & right));
                break;

            case OperatorBitwise::LSHIFT:
                result = Types::Var::make(Types::Value::make_int(left << right));
                break;

            case OperatorBitwise::OR:
                result = Types::Var::make(Types::Value::make_int(left | right));
                break;

            case OperatorBitwise::RSHIFT:
                result = Types::Var::make(Types::Value::make_int(left >> right));
                break;

            case OperatorBitwise::XOR:
                result = Types::Var::make(Types::Value::make_int(left ^ right));
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
        Types::Var val = visit(context->varOrExp()).as<Types::Var>();
        std::vector<NameAndArgs> names_and_args;
        for (LuaParser::NameAndArgsContext* ctx: context->nameAndArgs()) {
            names_and_args.push_back(visit(ctx).as<NameAndArgs>());
        }

        return process_names_and_args(val, names_and_args);
    }

    virtual antlrcpp::Any visitFunctioncall(LuaParser::FunctioncallContext *context) {
        if (funcall_test_infrastructure(context)) {
            return Types::Var::make(Types::Value::make_nil());
        }

        Types::Var call_source = visit(context->varOrExp()).as<Types::Var>();
        std::vector<NameAndArgs> names_and_args;
        for (LuaParser::NameAndArgsContext* ctx: context->nameAndArgs()) {
            names_and_args.push_back(visit(ctx).as<NameAndArgs>());
        }

        return process_names_and_args(call_source, names_and_args);
    }

    virtual antlrcpp::Any visitVarOrExp(LuaParser::VarOrExpContext *context) {
        Types::Var result;
        if (context->var_()) {
            result = visit(context->var_()).as<Types::Var>();
        } else if (context->exp()) {
            result = visit(context->exp()).as<Types::Var>();
        }

        return result;
    }

    virtual antlrcpp::Any visitVar_(LuaParser::Var_Context *context) {
        Types::Var result;
        if (context->NAME()) {
            std::string name(context->NAME()->getText());
            auto p = lookup_name(name, false); // Don't throw here

            if (p.first) {
                if (_var__context == Var_Context::VARLIST) {
                    result = Types::Var::make(p.first);
                } else {
                    // Copy, because having pointers is dangerous. Only give
                    // a pointer when needed.
                    result = Types::Var::make(*p.first);
                }
            } else {
                if (_var__context == Var_Context::VARLIST) {
                    Types::Value* new_value = new Types::Value();
                    _global_values[name] = new_value;
                    result = Types::Var::make(new_value);
                } else {
                    result = Types::Var::make(Types::Value::make_nil());
                }
            }
        } else {
            result = visit(context->exp()).as<Types::Var>();
        }

        for (LuaParser::VarSuffixContext* ctx: context->varSuffix()) {
            VarSuffix suffix = visit(ctx).as<VarSuffix>();
            result = process_names_and_args(result, suffix._name_and_args);

            if (Subscript* subscript = std::get_if<Subscript>(&suffix._suffix)) {
                if (!result.is<Types::Table*>()) {
                    throw Exceptions::BadDotAccess(result.type_as_string());
                }

                result = Types::Var::make(&(result.as<Types::Table*>()->subscript(subscript->_value.get())));
            } else if (std::string* str = std::get_if<std::string>(&suffix._suffix)) {
                if (!result.is<Types::Table*>()) {
                    throw Exceptions::BadDotAccess(result.type_as_string());
                }

                result = Types::Var::make(&(result.as<Types::Table*>()->dot(*str)));
            } else {
                throw Exceptions::NilDot();
            }
        }

        return result;
    }

    virtual antlrcpp::Any visitVarSuffix(LuaParser::VarSuffixContext *context) {
        VarSuffix result;

        for (unsigned int i = 0; i < context->nameAndArgs().size(); ++i) {
            result._name_and_args.push_back(visit(context->nameAndArgs(i)).as<NameAndArgs>());
        }

        if (context->NAME()) {
            result._suffix = context->NAME()->getText();
        } else {
            Subscript sub;
            sub._value = visit(context->exp()).as<Types::Var>();
            result._suffix = sub;
        }
        return result;
    }

    virtual antlrcpp::Any visitNameAndArgs(LuaParser::NameAndArgsContext *context) {
        NameAndArgs res;
        if (context->NAME()) {
            res._name = context->NAME()->getText();
        }

        res._args = visit(context->args()).as<Args>();

        return res;
    }

    virtual antlrcpp::Any visitArgs(LuaParser::ArgsContext *context) {
        Args args;

        if (context->explist()) {
            args = visit(context->explist()).as<std::vector<Types::Var>>();
            std::vector<Types::Var>& values = std::get<std::vector<Types::Var>>(args);
            Types::Var& last = values.back();

            // Immediately expand the last argument if it is an elipsis or
            // a list of values. This will help when processing arguments
            // further down the road.
            std::vector<Types::Value> remains;
            if (last.is<Types::Elipsis>()) {
                remains = last.as<Types::Elipsis>().values();
            } else if (last.list()) {
                remains = last._list();
            }

            for (Types::Value const& value: remains) {
                values.push_back(Types::Var::make(value));
            }
        } else if (context->tableconstructor()) {
            TableConstructor constructor;
            constructor._var = visit(context->tableconstructor()).as<Types::Var>();
            args = constructor;
        } else if (context->string()) {
            String string;
            string._var = visit(context->string()).as<Types::Var>();
            args = string;
        } else {
            args = std::vector<Types::Var>();
        }

        return args;
    }

    virtual antlrcpp::Any visitFunctiondef(LuaParser::FunctiondefContext *context) {
        std::vector<std::string> names = visit(context->funcbody()).as<std::vector<std::string>>();
        Types::Function* function = new Types::Function(std::move(names), context->funcbody()->block());
        close_function(function, context->funcbody()->block());
        Types::Value v; v._type = function;
        sGC->add_reference(v._type);
        return Types::Var::make(v);
    }

    virtual antlrcpp::Any visitFuncbody(LuaParser::FuncbodyContext *context) {
        if (LuaParser::ParlistContext* ctx = context->parlist()) {
            return visit(ctx);
        } else {
            return std::vector<std::string>();
        }
    }

    virtual antlrcpp::Any visitParlist(LuaParser::ParlistContext *context) {
        std::vector<std::string> names;
        if (context->getText().starts_with("...")) {
            names.push_back("...");
        } else {
            size_t n = context->getText().size();
            std::vector<std::string> nl = visit(context->namelist()).as<std::vector<std::string>>();
            std::ranges::for_each(nl, [&names](std::string const& name) { names.push_back(name);});

            if (context->getText().size() >= 3) {
                if (context->getText().substr(n - 3, std::string::npos) == "...") {
                    names.push_back("...");
                }
            }
        }

        return names;
    }

    virtual antlrcpp::Any visitTableconstructor(LuaParser::TableconstructorContext *context) {
        std::list<std::pair<Types::Value, Types::Value>> values;

        if (LuaParser::FieldlistContext* ctx = context->fieldlist()) {
            values = visit(ctx).as<std::list<std::pair<Types::Value, Types::Value>>>();
        }

        return Types::Var::make(Types::Value::make_table(values));
    }

    virtual antlrcpp::Any visitFieldlist(LuaParser::FieldlistContext *context) {
        std::list<std::pair<Types::Value, Types::Value>> values;
        unsigned int index = 1;
        for (LuaParser::FieldContext* ctx: context->field()) {
            std::pair<std::optional<Types::Var>, Types::Var> value = visit(ctx).as<std::pair<std::optional<Types::Var>, Types::Var>>();
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
            // std::cout << "[NYI] Expressions as keys" << std::endl;
            return std::make_pair(std::make_optional(visit(context->exp(0)).as<Types::Var>()), visit(context->exp(1)).as<Types::Var>());
        } else if (context->NAME()) {
            return std::make_pair(std::make_optional(Types::Var::make(Types::Value::make_string(context->NAME()->getText()))), visit(context->exp()[0]).as<Types::Var>());
        } else {
            return std::make_pair((std::optional<Types::Var>)std::nullopt, visit(context->exp()[0]).as<Types::Var>());
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
            return Types::Var::make(Types::Value::make_int(std::stoi(ptr->getText())));
        } else if (auto ptr = context->HEX()) {
            return Types::Var::make(Types::Value::make_int(std::stoi(ptr->getText(), nullptr, 16)));
        } else if (auto ptr = context->FLOAT()) {
            return Types::Var::make(Types::Value::make_double(std::stod(ptr->getText())));
        } else if (auto ptr = context->HEX_FLOAT()) {
            std::cout << "[NYI] Hexfloat" << std::endl;
            return Types::Var::make(Types::Value::make_double(std::stod(ptr->getText())));
        } else {
            throw std::runtime_error("Invalid number");
        }
    }

    virtual antlrcpp::Any visitString(LuaParser::StringContext *context) {
        if (auto ptr = context->NORMALSTRING()) {
            std::string text(ptr->getText());
            return Types::Var::make(Types::Value::make_string(text.substr(1, text.size() - 2)));
        } else if (auto ptr = context->CHARSTRING()) {
            std::string text(ptr->getText());
            return Types::Var::make(Types::Value::make_string(text.substr(1, text.size() - 2)));
        } else if (auto ptr = context->LONGSTRING()) {
            std::cout << "[NYI] Longstring" << std::endl;
            return Types::Var::make(Types::Value::make_string(ptr->getText()));
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
        CLOSURE, // In a function's closure
    };

    Var_Context _var__context = Var_Context::OTHER;

    struct TableConstructor {
        Types::Var _var;
    };

    struct String {
        Types::Var _var;
    };


    typedef std::variant<std::vector<Types::Var>, TableConstructor, String> Args;

    class ArgsVisitor {
    public:
        ArgsVisitor(std::vector<Types::Value>& dest) : _dest(dest) { }

        void operator()(std::vector<Types::Var> const& args) {
            std::transform(args.begin(), args.end(), std::back_inserter(_dest), [](Types::Var const& var) { return var.get(); });
        }

        void operator()(TableConstructor const& cons) {
            _dest.push_back(cons._var.get());
        }

        void operator()(String const& string) {
            _dest.push_back(string._var.get());
        }

    private:
        std::vector<Types::Value>& _dest;
    };

    struct NameAndArgs {
        std::optional<std::string> _name = std::nullopt;
        Args _args;
    };

    struct Subscript {
        Types::Var _value;
    };

    typedef std::variant<Subscript, std::string, Types::VarError> Suffix;

    struct VarSuffix {
        std::vector<NameAndArgs> _name_and_args;
        Suffix _suffix;
    };

    template<typename... Args>
    constexpr bool is_error(std::variant<Args...> const& a) {
        return std::holds_alternative<Types::VarError>(a);
    }

    void process_stat_var_list(LuaParser::VarlistContext* varlist, LuaParser::ExplistContext* explist) {
        std::vector<Types::Var> vars = visit(varlist).as<std::vector<Types::Var>>();
        std::vector<Types::Var> exprs = visit(explist).as<std::vector<Types::Var>>();

        for (Types::Var& v: std::views::filter(exprs, [](Types::Var const& v) { return v.lvalue(); })) {
            v.morph();
        }

        for (unsigned int i = 0; i < std::min(vars.size(), exprs.size()); ++i) {
            if (!vars[i].lvalue()) {
                throw std::runtime_error("How the hell did you arrive here ?");
            }

            sGC->remove_reference(vars[i]._lvalue()->value());
            vars[i]._lvalue()->value() = exprs[i].get().value();
            sGC->add_reference(exprs[i].get().value());
        }

        // Adjust for elipsis / value list
        if (exprs.size() < vars.size()) {
            Types::Var& last = exprs.back();
            unsigned int i = exprs.size();
            unsigned int j;
            std::vector<Types::Value> remains;

            if (last.is<Types::Elipsis>() || last.list()) {
                if (last.is<Types::Elipsis>()) {
                    j = 0;
                    --i; // Rewrite the last value because it actually holds
                         // the whole Elipsis, instead of its first value.
                    remains = last.as<Types::Elipsis>().values();
                } else {
                    j = 1;
                    remains = std::move(last._list());
                }
            }

            for (; j < remains.size() && i < vars.size(); ++i, ++j) {
                if (i >= exprs.size()) {
                    sGC->remove_reference(vars[i]._lvalue()->value());
                }
                vars[i]._lvalue()->value() = remains[j].value();
                sGC->add_reference(remains[j].value());
            }

            for (; i < vars.size(); ++i) {
                vars[i]._lvalue()->value() = Types::Nil();
            }
        }
    }

    void process_break() {
        throw Exceptions::Break();
    }

    void process_goto(std::string const& label) {
        throw Exceptions::Goto(label);
    }

    void process_while(LuaParser::ExpContext* exp, LuaParser::BlockContext* block) {
        LuaParser::BlockContext* current = current_block();
        try {
            while (visit(exp).as<Types::Var>().as_bool_weak()) {
                visit(block);
            }
        } catch (Exceptions::Break& brk) {
            stabilize_blocks(current);
        }
    }

    void process_repeat(LuaParser::BlockContext* block, LuaParser::ExpContext* exp) {
        LuaParser::BlockContext* current = current_block();
        try {
            do {
                visit(block);
            } while (!visit(exp).as<Types::Var>().as_bool_weak());
        } catch (Exceptions::Break& brk) {
            stabilize_blocks(current);
        }
    }

    void process_if(LuaParser::StatContext* ctx) {
        std::vector<LuaParser::ExpContext*> conditions = ctx->exp();

        bool found = false;
        for (unsigned int i = 0; i < conditions.size(); ++i) {
            if (visit(conditions[i]).as<Types::Var>().as_bool_weak()) {
                visit(ctx->block()[i]);
                found = true;
                break;
            }
        }

        if (!found) {
            size_t n_blocks = ctx->block().size();
            if (conditions.size() < n_blocks) {
                visit(ctx->block()[n_blocks - 1]);
            }
        }
    }

    void process_for_in(LuaParser::NamelistContext* nl, LuaParser::ExplistContext* el, LuaParser::BlockContext* block) {
        std::vector<std::string> names = visit(nl).as<std::vector<std::string>>();
        std::vector<Types::Var> exprs = visit(el).as<std::vector<Types::Var>>();

        try {

        } catch (Exceptions::Break& brk) {

        }
    }

    void process_for_loop(LuaParser::StatContext* ctx) {
        LuaParser::BlockContext* current = current_block();
        try {
            Types::Value counter = visit(ctx->exp()[0]).as<Types::Var>().get();
            if (!(counter.is<double>() || counter.is<int>())) {
                throw Exceptions::BadTypeException("int or double", counter.type_as_string(), "counter of numeric for");
            }

            Types::Value limit = visit(ctx->exp()[1]).as<Types::Var>().get();
            if (!(limit.is<double>() || limit.is<int>())) {
                throw Exceptions::BadTypeException("int or double", limit.type_as_string(), "limit of numeric for");
            }

            std::string name(ctx->NAME()->getText());
            _local_values.back()[ctx->block(0)][name] = new Types::Value();
            Types::Value* value = _local_values.back()[ctx->block(0)][name];
            value->value() = counter.value();

            Types::Value increment;
            if (ctx->exp().size() == 3) {
                LuaParser::ExpContext* context = ctx->exp()[2];
                increment = visit(context).as<Types::Var>().get();
                if (!(increment.is<double>() || increment.is<int>())) {
                    throw Exceptions::BadTypeException("int or double", increment.type_as_string(), "increment of numeric for");
                }
            } else {
                increment.value() = 1;
            }

            if (increment.is<double>()) {
                if (value->is<int>()) {
                    value->value() = (double)value->as<int>();
                }

                if (counter.is<int>()) {
                    counter.value() = (double)counter.as<int>();
                }
            }

            _blocks.push_back(ctx->block(0));
            if (value->is<int>()) {
                for (; value->as<int>() <= limit.as_double_weak();
                     value->value() = counter.as<int>() + (int)increment.as_double_weak(),
                     counter.value() = counter.as<int>() + (int)increment.as_double_weak()) {
                    _coming_from_for = true;
                    visit(ctx->block()[0]);
                    // We must erase all references to previous local variables,
                    // because they do keep their values between iterations.
                    // However, we must keep the value of the counter because
                    // it does retain its value between iterations. Well...
                    // Not exactly, its value does get "reset", but we do not
                    // remove it from the map because it is easier this way.
                    auto iter = _local_values.back()[ctx->block(0)].begin();
                    while (iter != _local_values.back()[ctx->block(0)].end()) {
                        if (iter->first == name) {
                            ++iter;
                        } else {
                            iter->second->remove_reference();
                            iter = _local_values.back()[ctx->block(0)].erase(iter);
                        }
                    }
                }
            } else {
                for (; value->as<double>() <= limit.as_double_weak();
                     value->value() = counter.as<double>() + increment.as_double_weak(),
                     counter.value() = counter.as<double>() + increment.as_double_weak()) {
                    _coming_from_for = true;
                    visit(ctx->block()[0]);
                    auto iter = _local_values.back()[ctx->block(0)].begin();
                    while (iter != _local_values.back()[ctx->block(0)].end()) {
                        if (iter->first == name) {
                            ++iter;
                        } else {
                            iter->second->remove_reference();
                            iter = _local_values.back()[ctx->block(0)].erase(iter);
                        }
                    }
                }
            }
            _blocks.pop_back();
            erase_block(ctx->block(0));

        } catch (Exceptions::Break& brk) {
            stabilize_blocks(current);
        }
    }

    void process_function(LuaParser::FuncnameContext* name, LuaParser::FuncbodyContext*) {
        visit(name);
    }

    void process_local_variables(LuaParser::AttnamelistContext* al, LuaParser::ExplistContext* el) {
        std::vector<std::string> names = visit(al).as<std::vector<std::string>>();
        std::vector<Types::Var> values;

        if (el) {
             values = visit(el).as<std::vector<Types::Var>>();
        } else {
            values.resize(names.size(), Types::Var::make(Types::Value::make_nil()));
        }

        for (unsigned int i = 0; i < std::min(names.size(), values.size()); ++i) {
            // Apparently Lua allows local a = 12; local a = 12...
            auto it = _local_values.back()[current_block()].find(names[i]);
            if (it == _local_values.back()[current_block()].end()) {
                _local_values.back()[current_block()][names[i]] = new Types::Value();
            }

            _local_values.back()[current_block()][names[i]]->value() = values[i].get().value();
            sGC->add_reference(values[i].get().value());
        }

        // Adjust
        if (values.size() < names.size()) {
            Types::Var& last = values.back();
            unsigned int i = values.size();
            unsigned int j;

            std::vector<Types::Value> remains;

            if (last.is<Types::Elipsis>() || last.list()) {
                if (last.is<Types::Elipsis>()) {
                    remains = last.as<Types::Elipsis>().values();
                    j = 0;
                    --i;
                } else {
                    remains = std::move(last._list());
                    j = 1;
                }
            }

            for (; j < remains.size() && i < names.size(); ++i, ++j) {
                auto it = _local_values.back()[current_block()].find(names[i]);
                if (it == _local_values.back()[current_block()].end()) {
                    _local_values.back()[current_block()][names[i]] = new Types::Value();
                }

                // Remove reference to the elipsis stored in the last named value
                // assigned. This is normally useless as Elipsis is not a
                // refcounted type, but who knows how things may evolve.
                if (i < values.size()) {
                    sGC->remove_reference(_local_values.back()[current_block()][names[i]]->value());
                }

                sGC->add_reference(remains[j].value());
                _local_values.back()[current_block()][names[i]]->value() = remains[j].value();
            }

            for (; i < names.size(); ++i) {
                _local_values.back()[current_block()][names[i]] = new Types::Value();
            }
        }
    }

    void process_local_function(std::string const& name, LuaParser::FuncbodyContext* body) {
        Types::Function* f = new Types::Function(std::move(visit(body->parlist()).as<std::vector<std::string>>()), body->block());
        close_function(f, body->block());
        Types::Value* value = new Types::Value;
        value->_type = f;
        sGC->add_reference(value->value());
        _local_values.back()[current_block()][name] = value;
    }

    Types::Var nyi(std::string const& str) {
        std::cout << "[NYI] " << str << std::endl;
        return Types::Var::make(Types::Value::make_nil());
    }

    std::pair<Types::Value*, Scope> lookup_name(std::string const& name, bool should_throw = true) {
        Scope scope;

        auto contexts = _listener.get_context_for_local(current_block(), name);
        Types::Value* candidate;
        bool found = false;
        for (auto it = contexts.first; it != contexts.second; ++it) {
            LuaParser::BlockContext* ctx = it->second;
            auto value_it = _local_values.back()[ctx].find(name);
            if (value_it != _local_values.back()[ctx].end()) {
                candidate = value_it->second;
                scope = Scope::LOCAL;
                found = true;
            }
        }

        if (!found) {
            if (Types::Function* function = current_function()) {
                auto const& closure = function->closure();
                auto it = closure.find(name);
                if (it != closure.end()) {
                    return std::make_pair(it->second, Scope::CLOSURE);
                }
            }
        }

        if (!found) {
            scope = Scope::GLOBAL;
            auto it = _global_values.find(name);
            if (it == _global_values.end()) {
                if (should_throw) {
                    throw Exceptions::NilDot();
                }
                candidate = nullptr;
            } else {
                candidate = _global_values[name];
            }
        }

        return std::make_pair(candidate, scope);
    }

    LuaParser::BlockContext* current_block() {
        if (_blocks.empty()) {
            return nullptr;
        } else {
            return _blocks.back();
        }
    }

    Types::Function* current_function() {
        if (_functions.empty()) {
            return nullptr;
        } else {
            return _functions.back();
        }
    }

    void stabilize_blocks(LuaParser::BlockContext* context) {
        if (_blocks.back() != context) {
            size_t n = _blocks.size() - 1;
            while (_blocks[n] != context) {
                --n;
            }

            for (unsigned int j = n; j < _blocks.size(); ++j) {
                erase_block(_blocks[j]);
            }

            _blocks.resize(n + 1);
            if (_blocks.back() != context) {
                throw std::runtime_error("Oups");
            }
        }
    }

    void erase_block(LuaParser::BlockContext* context) {
        clear_block(context);
        _local_values.back().erase(context);
    }

    void clear_block(LuaParser::BlockContext* context) {
        auto& data = _local_values.back()[context];
        for (auto& p: data) {
            p.second->remove_reference();
        }
    }

    void close_function(Types::Function* function, LuaParser::BlockContext* body) {
        auto pair = _listener.get_parents_of_function(body);
        for (auto it = pair.first; it != pair.second; ++it) {
            auto& store = _local_values.back()[it->second];
            for (auto& p: store) {
                function->close(p.first, p.second);
            }
        }
    }

    std::vector<Types::Var> call_function(Types::Function* function, std::vector<Types::Value> const& values) {
        LuaParser::BlockContext* ctx = function->get_context();
        LuaParser::BlockContext* current_block = this->current_block();

        _blocks.push_back(ctx);
        _local_values.push_back(decltype(_local_values)::value_type());
        /* for (auto& p: function->closure()) {
            _local_values.back()[p.first] = p.second;
        } */
        _functions.push_back(function);


        unsigned int i = 0;
        for (; i < std::min(values.size(), function->formal_parameters().size()); ++i) {
            Types::Value* value = new Types::Value();
            value->value() = values[i].value();
            sGC->add_reference(values[i].value());
            _local_values.back()[ctx][function->formal_parameters()[i]] = value;
        }

        // Adjust if necessary. If not enough arguments provided, fill with nil.
        // If too many arguments, send the rest as an elipsis if the function
        // has an elipsis as parameter; otherwise discard the rest.
        if (i < function->formal_parameters().size()) {
            for (; i < function->formal_parameters().size(); ++i) {
                _local_values.back()[ctx][function->formal_parameters()[i]] = new Types::Value();
            }
        } else if (i < values.size()) {
            if (function->formal_parameters().back() == "...") {
                Types::Value* elipsis = new Types::Value();
                std::vector<Types::Value> elipsis_values;
                for (; i < values.size(); ++i) {
                    elipsis_values.push_back(values[i]);
                }
                elipsis->value() = Types::Elipsis(elipsis_values);
                _local_values.back()[ctx]["..."] = elipsis;
            }
        }

        _coming_from_funcall = true;
        try {
            visit(ctx);
            _functions.pop_back();
            _local_values.pop_back();
            return std::vector<Types::Var>();
        } catch (Exceptions::Return& e) {
            // If control flow is disrupted by a return statement, blocks may
            // not be in a coherent state. Stabilize them only in this case.
            stabilize_blocks(current_block);
            _functions.pop_back();
            _local_values.pop_back();
            return e.get();
        }
    }

    bool funcall_test_infrastructure(LuaParser::FunctioncallContext* context) {
        if (!context->varOrExp()->var_()) {
            return false;
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
            return false;
        }

        if (funcname == "ensure_value_type") {
            std::string expression(context->nameAndArgs()[0]->args()->explist()->exp()[0]->getText());
            Types::Var left = visit(context->nameAndArgs()[0]->args()->explist()->exp()[0]).as<Types::Var>();
            Types::Var middle = visit(context->nameAndArgs()[0]->args()->explist()->exp()[1]).as<Types::Var>();
            Types::Var right = visit(context->nameAndArgs()[0]->args()->explist()->exp()[2]).as<Types::Var>();

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

            // std::cout << "Expression " << expression << " has value " << left.value_as_string() << " of type " << left.type_as_string() << " (expected equivalent of " << middle.value_as_string() << " of type " << type << ") => OK" << std::endl;
        } else if (funcname == "expect_failure") {
            std::string expression(context->nameAndArgs()[0]->args()->explist()->exp()[0]->getText());
            try {
                Types::Var left = visit(context->nameAndArgs()[0]->args()->explist()->exp()[0]).as<Types::Var>();
                throw std::runtime_error("Failure expected in expression " + expression);
            } catch (Exceptions::BadTypeException& e) {
                std::cout << "Expression " << expression << " rightfully triggered a type error" << std::endl;
            } catch (std::exception& e) {
                throw;
            }
        } else if (funcname == "print") {
            Types::Var left = visit(context->nameAndArgs()[0]->args()->explist()->exp()[0]).as<Types::Var>();
            std::cout << left.value_as_string() << std::endl;
        } else if (funcname == "globals") {
            std::cout << "Globals: " << std::endl;
            for (ValueStore::value_type const& v: _global_values) {
                std::cout << v.first << ": " << v.second->value_as_string() << std::endl;
            }
            std::cout << std::endl;
        } else if (funcname == "locals") {
            std::cout << "Locals (top block): " << std::endl;
            for (ValueStore::value_type const& v: _local_values.back()[current_block()]) {
                std::cout << v.first << ": " << v.second->value_as_string() << std::endl;
            }
            std::cout << std::endl;
        } else if (funcname == "memory") {
            std::cout << "Globals: " << std::endl;
            for (ValueStore::value_type const& v: _global_values) {
                std::cout << "\t" << v.first << ": " << v.second->value_as_string() << std::endl;
            }
            std::cout << std::endl;

            for (unsigned int j = 0; j < _local_values.size(); ++j) {
                std::cout << "Locals (Frame " << j << "): " << std::endl;
                for (unsigned int i = 0; i < _blocks.size(); ++i) {
                    std::cout << "\tBlock " << i << std::endl;
                    for (ValueStore::value_type const& v: _local_values[j][_blocks[i]]) {
                        std::cout << "\t\t" << v.first << ": " << v.second->value_as_string() << std::endl;
                    }
                }
            }
        } else {
            return false;
        }

        return true;
    }

    Types::Var process_names_and_args(Types::Var const& src, std::vector<NameAndArgs> const& names_and_args) {
        Types::Var result = src;
        for (NameAndArgs const& name_and_args: names_and_args) {
            Types::Function* function;
            if (name_and_args._name) {
                if (!result.is<Types::Table*>()) {
                    throw Exceptions::BadDotAccess(result.type_as_string());
                }

                Types::Value& fn = result.as<Types::Table*>()->dot(*name_and_args._name);
                if (!fn.is<Types::Function*>()) {
                    throw Exceptions::BadCall(fn.type_as_string());
                }

                function = fn.as<Types::Function*>();
            } else {
                if (!result.is<Types::Function*>()) {
                    throw Exceptions::BadCall(result.type_as_string());
                }

                function = result.as<Types::Function*>();
            }

            std::vector<Types::Value> arguments;
            // Push the table as argument if using the ':' notation.
            if (name_and_args._name) {
                arguments.push_back(result.get());
            }
            std::visit(ArgsVisitor(arguments), name_and_args._args);

            std::vector<Types::Var> results = call_function(function, arguments);
            if (results.size() == 0) {
                result = Types::Var::make(Types::Value::make_nil());
            } else {
                std::vector<Types::Value> tmp;
                std::transform(results.begin(), results.end(), std::back_inserter(tmp), [](Types::Var const& v) { return v.get(); });
                result = Types::Var::make(tmp);
            }
        }

        return result;
    }

    typedef std::map<std::string, Types::Value*> ValueStore;

    // Scope processing works like a stack. Each time a new scope is
    // entered, push a new map on top of the stack to store the local
    // Values of the scope. Once the scope is exited, pop this map from
    // the stack.
    std::vector<std::map<LuaParser::BlockContext*, ValueStore>> _local_values;
    ValueStore _global_values;

    GotoBreakListener _listener;

    std::vector<LuaParser::BlockContext*> _blocks;
    std::vector<Types::Function*> _functions;
    bool _coming_from_for = false;
    bool _coming_from_funcall = false;
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
    std::cout << tree->toStringTree(&parser, true) << std::endl;
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

        if (*v == "00_goto_break")
            continue;
        run_test(p.path().string());
    }
}

class GotoBreakResultExpected : public std::exception {
public:
    GotoBreakResultExpected(const std::string& path, const std::string& expected, const std::string& received) {
        _error = "Goto / break result of kind " + expected + " was expected in file + " + path + ", received " + received + "\n";
    }

    const char* what() const noexcept {
        return _error.c_str();
    }

private:
    std::string _error;
};

void run_goto_break_test(std::string const& path) {
    std::ifstream stream(path, std::ios::in);

    if (!stream) {
        return;
    }

    std::string header;
    stream >> header;

    std::vector<std::string> allowed = {
        "success",
        "crossed",
        "invisible",
        "lonely",
        "multiple"
    };

    if (std::find(allowed.begin(), allowed.end(), header) == allowed.end()) {
        throw std::runtime_error("Unknown goto / break result " + header);
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
        GotoBreakListener listener;
        antlr4::tree::ParseTreeWalker::DEFAULT.walk(&listener, tree);
        listener.validate_gotos();

        if (header != "success") {
            throw GotoBreakResultExpected(path, header, "success");
        }
    } catch (Exceptions::CrossedLocal& crossed) {
        if (header != "crossed") {
            throw GotoBreakResultExpected(path, header, "crossed");
        }
    } catch (Exceptions::InvisibleLabel& invisible) {
        if (header != "invisible") {
            throw GotoBreakResultExpected(path, header, "invisible");
        }
    } catch (Exceptions::LonelyBreak& lonely) {
        if (header != "lonely") {
            throw GotoBreakResultExpected(path, header, "lonely");
        }
    } catch (Exceptions::LabelAlreadyDefined& label) {
        if (header != "multiple") {
            throw GotoBreakResultExpected(path, header, "multiple");
        }
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        throw;
    }

    std::cout << "[OK] " << path << std::endl;
}

void test_goto_break() {
    for (auto& p: fs::directory_iterator("tests/00_goto_break")) {
        if (p.path().string()[0] == '.' || p.path().extension() != ".lua") {
            continue;
        }

        run_goto_break_test(p.path().string());
    }
}

struct CLIArgs {
    bool _test = false;
    std::string _test_file;
    bool _base = false;
    bool _goto_break = false;
    std::string _goto_break_file;
};

void parse_args(int argc, char** argv, CLIArgs& args) {
    po::options_description options("All options");
    options.add_options()
            ("help", "Display this help and exit")
            ("test", po::value<std::string>()->implicit_value(""), "Run all tests, or on the given file only")
            ("base", "Run the base file to get the AST")
            ("gb", po::value<std::string>()->implicit_value(""), "Run tests on the goto_break directory with listener only, or only on the given file");
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

    if (vm.count("gb")) {
        args._goto_break = true;
        args._goto_break_file = vm["gb"].as<std::string>();
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

        /* try {
            MyLuaVisitor visitor(tree);
            visitor.visit(tree);
        } catch (std::exception& e) {
            std::cerr << "Parse base error: " << e.what() << std::endl;
        } */
    }

    if (args._goto_break) {
        if (!args._goto_break_file.empty()) {
            run_goto_break_test(args._goto_break_file);
        } else {
            test_goto_break();
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
