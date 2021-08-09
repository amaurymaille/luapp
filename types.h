#pragma once

#include <string>
#include <variant>
#include <vector>

#include "LuaParser.h"

class Interpreter;

namespace Types {
    struct Value;
    struct Function;
    struct Userdata;
    class Table;

    struct Nil {
        bool operator==(const Nil&) const;
        bool operator!=(const Nil&) const;
        bool operator<(const Nil&) const;
    };

    struct Elipsis {
    public:
        Elipsis(std::vector<Types::Value> const& values);
        Elipsis(Elipsis const& other);

        Elipsis& operator=(Elipsis const& other);

        bool operator==(const Elipsis&) const;
        bool operator!=(const Elipsis&) const;
        bool operator<(const Elipsis&) const;

        std::vector<Types::Value> const& values() const { return _values; }

    private:
        std::vector<Types::Value> _values;
    };

    typedef std::variant<bool, int, double, std::string, Nil, Elipsis, Function*, Userdata*, Table*> LuaValue;

    class Function {
    public:
        Function(std::vector<std::string>&& formal_parameters, LuaParser::BlockContext* body);

        ~Function();

        bool operator==(const Function& other) const;
        bool operator!=(const Function& other) const;

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
        bool operator==(const Userdata& other) const;
        bool operator!=(const Userdata& other) const;
    };

    class Table {
    public:
        Table(const std::list<std::pair<Value, Value> > &values);

        bool operator==(const Table& other) const;
        bool operator!=(const Table& other) const;

        int border() const;
        Value& subscript(Value const&);
        Value& dot(std::string const&);
        void add_field(std::string const& name, Value const& value);
        void add_field(Value const& source, Value const& dst);

    private:
        struct FieldSetter {
        public:
            FieldSetter(Table& t, Value const& value);

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
            FieldGetter(Table& t);

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
        static GC* instance();

        void add_reference(LuaValue const& l);
        void remove_reference(LuaValue& l);

    private:
        GC() { }

        std::map<LuaValue, unsigned int> _references;

        struct Deleter {
        public:
            template<typename T>
            void operator()(T value) { delete value; }

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
        Value();
        Value(Value const& other);

        ~Value();

        Value& operator=(const Value& other);

        constexpr bool is_refcounted() const;

        static void init();

        friend Table::Table(const std::list<std::pair<Value, Value>>&);
        friend void Table::add_field(const Value&, const Value&);
        friend Value& Table::subscript(const Value &);
        friend Value& Table::dot(const std::string &);
        friend Interpreter;

        bool operator==(const Value& other) const;
        bool operator!=(const Value& other) const;

        constexpr bool has_dot() const;

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

        std::string as_string() const;
        int as_int_weak(bool allow_double = true) const;
        double as_double_weak() const;
        bool as_bool_weak() const;

        template<typename T, typename... Args>
        static void alloc(Value& v, Args&&... args) {
            v._type = new T(std::forward<Args>(args)...);
            sGC->add_reference(v._type);
        }

        // Decision: conversion from string to integer yields double in the
        // Lua 5.3.2 interpreter, I've decided to yield the appropriate type
        // instead.
        Value from_string_to_number(bool force_double = false) const;

        /// Makers
        static Value make_nil();
        static Value make_bool(bool b);
        static Value make_true();
        static Value make_false();
        static Value make_table(std::list<std::pair<Value, Value>> const& values);
        static Value make_string(std::string&& string);
        static Value make_int(int i);
        static Value make_double(double d);

        template<typename T>
        static Value make(T value) requires (!std::is_same_v<T, Nil> && !std::is_same_v<T, bool> && !std::is_same_v<T, Elipsis>) {
            Value v;
            v._type = value;
            return v;
        }

        std::string type_as_string() const;
        std::string value_as_string() const;

        Value& subscript(Value const& value);
        Value& dot(std::string const& name);

        LuaValue& value();
        LuaValue const& value() const;

        void add_reference();
        void remove_reference();

    private:
        LuaValue _type;

        static Value _nil;
        // static Value _elipsis;
        static Value _true;
        static Value _false;

        unsigned int _references = 1;
    };

    class VarError {};
    static const VarError var_error_t;

    /* A free value (rvalue); a bound value (lvalue); an error */
    typedef std::variant<Value, Value*, std::vector<Value>, VarError> VarElements;
    class Var {
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

        Value get() const;

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

        void morph();

        template<typename T>
        void set(T&& t) {
            _value = std::move(t);
        }

        bool error() const;

        template<typename T>
        static Var make(T&& value) {
            Var v;
            v._value = std::move(value);
            return v;
        }

        double as_double_weak() const;
        int as_int_weak(bool allow_double = true) const;
        bool as_bool_weak() const;
        std::string as_string() const;

        Var();

        Var(Var const& other);

        Var& operator=(Var const& other);

        bool operator==(const Var& other) const;
        bool operator!=(const Var& other) const;

        bool has_dot() const;

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

        bool is_refcounted() const;

        Value from_string_to_number(bool force_double = false) const;

        std::string type_as_string() const;
        std::string value_as_string() const;

        Value& subscript(Value const& value);
        Value& dot(std::string const& s);

        LuaValue& value();

    private:
        VarElements _value;

        [[noreturn]] void _error() const {
            throw std::runtime_error("Attempted to access an errored value");
        }
    };


}
