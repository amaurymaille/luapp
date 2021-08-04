#include <algorithm>
#include <cmath>
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

#include "LuaLexer.h"
#include "LuaParser.h"
#include "LuaVisitor.h"

namespace fs = std::filesystem;
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
}

namespace Types {
    struct Value;

    struct Nil {
        bool operator==(const Nil&) const {
            return true;
        }

        bool operator!=(const Nil&) const {
            return false;
        }
    };

    struct Elipsis {
        bool operator==(const Elipsis&) const {
            return false;
        }

        bool operator!=(const Elipsis&) const {
            return true;
        }
    };

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
        Table(std::map<Value*, Value*> const& values);

        ~Table();

        bool operator==(const Table& other) const {
            return this == &other;
        }

        bool operator!=(const Table& other) const {
            return this != &other;
        }

        int border() const;

    private:
        struct FieldVisitor {
        public:
            FieldVisitor(Table& t, Value* key, Value* value) : _key(key), _value(value), _t(t) { }

            ~FieldVisitor();

            // Can't have nil as key
            void operator()(Nil) { }

            void operator()(int i) {
                _t._int_fields[i] = _value;
            }

            void operator()(double d) {
                _t._double_fields[d] = _value;
            }

            void operator()(bool b) {
                _t._bool_fields[b ? 1 : 0] = _value;
            }

            void operator()(std::string const& s) {
                _t._string_fields[s] = _value;
            }

            void operator()(Function*) {
                _t._fields[_key] = _value;
            }

            void operator()(Table*) {
                _t._fields[_key] = _value;
            }

            void operator()(Userdata*) {
                _t._fields[_key] = _value;
            }

            // Can't have elipsis as key
            void operator()(Elipsis) { }

        private:
            Value* _key;
            Value* _value;
            Table& _t;
        };

        friend class Value;
        friend class FieldVisitor;

        std::map<int, Value*> _int_fields;
        std::map<double, Value*> _double_fields;
        std::vector<Value*> _bool_fields;
        std::map<std::string, Value*> _string_fields;

        std::map<Value*, Value*> _fields;
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

    class Value {
    public:
        Value() {
            _type = Nil();
        }

        template<typename T>
        Value(T&& t) {
            _type = t;
        }

        static void free() {
            delete _nil;
            delete _true;
            delete _false;
        }

        friend Table::Table(const std::map<Value *, Value *>&);
        friend Table::~Table();
        friend Table::FieldVisitor::~FieldVisitor();
        friend MyLuaVisitor;

        bool operator==(const Value& other) const {
            if (this == &other) {
                return true;
            }

            // Referenced type (like (userdata, table, function) are equal iff
            // they have the same adress.
            bool naive = _type == other._type;
            if (!naive) {
                if (_type.index() == other._type.index()) {
                    if (is<double>()) {
                        double diff = std::fabs(as<double>() - other.as<double>());
                        double eps (std::numeric_limits<double>::epsilon() * std::max(1.0, std::max(std::fabs(as<double>()), std::fabs(other.as<double>()))));
                        return diff <= eps;
                    }
                    return naive;
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

            return naive;
        }

        bool operator!=(const Value& other) const {
            return !(*this == other);
        }

        constexpr bool is_reference() const {
            return std::visit(IsReferenceChecker(), _type);
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

        // Decision: conversion from string to integer yields double in the
        // Lua 5.3.2 interpreter, I've decided to yield the appropriate type
        // instead.
        Value* from_string_to_number(bool force_double = false) const {
            if (!is<std::string>()) {
                throw Exceptions::ContextlessBadTypeException("string", type_as_string());
            }

            Value* v = new Value;
            if (force_double) {
                v->_type = as_double_weak();
                return v;
            } else {
                try {
                    v->_type = as_int_weak();
                } catch (std::invalid_argument& e) {
                    v->_type = as_double_weak();
                } catch (std::out_of_range& e) {
                    throw;
                }
            }

            return v;
        }

        /// Makers
        static Value* make_nil() { return _nil; }

        static Value* make_bool(bool b) {
            if (b) {
                return _true;
            } else {
                return _false;
            }
        }

        static Value* make_true() { return _true; }

        static Value* make_false() { return _false; }

        static Value* make_elipsis() { return make(Elipsis()); }

        static Value* make_table(std::map<Value*, Value*> const& values) {
            Value* v = new Value;
            v->_type = new Table(values);
            return v;
        }

        template<typename T>
        static Value* make(T value) requires (!std::is_same_v<T, Nil> && !std::is_same_v<T, bool>) {
            Value* v = new Value;
            v->_type = value;
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
                result << as<Function*>();
            } else if (is<Userdata*>()) {
                result << as<Userdata*>();
            } else if (is<Table*>()) {
                result << as<Table*>();
            } else if (is<bool>()) {
                result << std::boolalpha << as<bool>() << std::noboolalpha;
            } else {
                result << "unknown type";
            }

            return result.str();
        }

    private:
        std::variant<Nil, int, double, bool, std::string, Function*, Userdata*, Table*, Elipsis> _type;

        // Naive GC
        unsigned int _nb_references = 1;

        void remove_reference() {
            if (is<Nil>() || is<bool>()) {
                return;
            }

            if (_nb_references == 0) {
                throw std::runtime_error("Destroying object with no references !");
            }

            --_nb_references;

            if (_nb_references == 0) {
                if (is<Table*>()) {
                    delete as<Table*>();
                }

                delete this;
            }
        }

        void add_reference() {
            if (is<Nil>()) {
                return;
            }

            ++_nb_references;
        }

        void remove_reference_if_free() {
            if (bound())
                return;

            remove_reference();
        }

        void bind() {
            // Attempting to bind a value a second time means we are assigning
            // it to another variable, so add a reference
            if (_bound) {
                if (is_reference()) {
                    add_reference();
                }
            }

            _bound = true;
        }

        bool bound() const {
            return _bound;
        }

        bool _bound = false;
        static Value* _nil;
        static Value* _true;
        static Value* _false;
    };

    Value* Value::_nil = new Value();
    Value* Value::_true = new Value(true);
    Value* Value::_false = new Value(false);

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

    Table::Table(const std::map<Value *, Value *> &values)  : _bool_fields(2, nullptr) {
        for (auto const& p: values) {
            std::visit(FieldVisitor(*this, p.first, p.second), p.first->_type);
        }
    }

    Table::~Table() {
        for (Value* v: std::views::values(_int_fields)) {
            v->remove_reference();
        }

        for (Value* v: std::views::values(_string_fields)) {
            v->remove_reference();
        }

        for (Value* v: std::views::values(_double_fields)) {
            v->remove_reference();
        }

        for (auto& p: _fields) {
            p.first->remove_reference();
            p.second->remove_reference();
        }
    }

    Table::FieldVisitor::~FieldVisitor() {
        _key->remove_reference_if_free();
    }
}

class MyLuaVisitor : public LuaVisitor {
public:
    MyLuaVisitor() {

    }

    ~MyLuaVisitor() {
        for (Types::Value* value: std::views::values(_global_values)) {
            value->remove_reference();
        }
    }

    virtual antlrcpp::Any visitChunk(LuaParser::ChunkContext *context) {
        // std::cout << "Chunk: " << context->getText() << std::endl;
        return visit(context->block());
    }

    virtual antlrcpp::Any visitBlock(LuaParser::BlockContext *context) {
        // std::cout << "Block: " << context->getText() << std::endl;
        _local_values.push(ValueStore());
        antlrcpp::Any retval = Types::Value::make_nil();

        for (LuaParser::StatContext* ctx: context->stat()) {
            visit(ctx);
        }

        if (LuaParser::RetstatContext* ctx = context->retstat()) {
            retval = visit(ctx);
        }

        for (auto& value: std::views::values(_local_values.top())) {
            value->remove_reference();
        }

        _local_values.pop();

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

        return Types::Value::make_nil();
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
        return Types::Value::make_nil();
    }

    virtual antlrcpp::Any visitRetstat(LuaParser::RetstatContext *context) {
        if (LuaParser::ExplistContext* ctx = context->explist()) {
            return visit(ctx);
        } else {
            return Types::Value::make_nil();
        }
    }

    virtual antlrcpp::Any visitLabel(LuaParser::LabelContext *context) {
        return nyi("Label");
    }

    virtual antlrcpp::Any visitFuncname(LuaParser::FuncnameContext *context) {
        return nyi("Funcname");
    }

    virtual antlrcpp::Any visitVarlist(LuaParser::VarlistContext *context) {
        return nyi("Varlist");
    }

    virtual antlrcpp::Any visitNamelist(LuaParser::NamelistContext *context) {
        return nyi("Namelist");
    }

    virtual antlrcpp::Any visitExplist(LuaParser::ExplistContext *context) {
        std::vector<Types::Value*> values;
        for (LuaParser::ExpContext* ctx: context->exp()) {
            values.push_back(visit(ctx).as<Types::Value*>());
        }

        return values;
    }

    virtual antlrcpp::Any visitExp(LuaParser::ExpContext *context) {
        if (context->getText() == "nil") {
            return Types::Value::make_nil();
        } else if (context->getText() == "true") {
            return Types::Value::make_true();
        } else if (context->getText() == "false") {
            return Types::Value::make_false();
        } else if (context->getText() == "...") {
            return Types::Value::make_elipsis();
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
            Types::Value* leftV = visit(context->exp()[0]).as<Types::Value*>();
            double left = leftV->as_double_weak();

            Types::Value* rightV = visit(context->exp()[1]).as<Types::Value*>();
            double right = rightV->as_double_weak();

            leftV->remove_reference_if_free();
            rightV->remove_reference_if_free();

            return Types::Value::make(std::pow(left, right));
        } else if (LuaParser::OperatorUnaryContext* ctx = context->operatorUnary()) {
            Types::Value* v = visit(context->exp()[0]).as<Types::Value*>();
            OperatorUnary op = visit(ctx).as<OperatorUnary>();

            switch (op) {
            case OperatorUnary::BANG:
                if (v->is<std::string>()) {
                    Types::Value* result = Types::Value::make((int)v->as<std::string>().size());
                    v->remove_reference_if_free();
                    return result;
                } else {
                    Types::Value* result = Types::Value::make(v->as<Types::Table*>()->border());
                    v->remove_reference_if_free();
                    return result;
                }

            case OperatorUnary::BIN_NOT: {
                 Types::Value* result = Types::Value::make(~v->as_int_weak());
                 v->remove_reference_if_free();
                 return result;
            }

            case OperatorUnary::MINUS: {
                std::function<Types::Value*(Types::Value*)> convert = [&](Types::Value* value) -> Types::Value* {
                    Types::Value* result;
                    if (value->is<int>()) {
                        result =  Types::Value::make(-value->as<int>());
                    } else if (value->is<double>()) {
                        result = Types::Value::make(-value->as<double>());
                    } else {
                        result = convert(value->from_string_to_number(true));
                    }

                    value->remove_reference_if_free();
                    return result;
                };

                return convert(v);
            }

            case OperatorUnary::NOT: {
                Types::Value* result = Types::Value::make_bool(!v->as_bool_weak());
                v->remove_reference_if_free();
                return result;
            }

            default:
                throw std::runtime_error("Invalid Unary operator");
            }
        } else if (LuaParser::OperatorMulDivModContext* ctx = context->operatorMulDivMod()) {
            Types::Value* leftV = visit(context->exp()[0]).as<Types::Value*>();
            Types::Value* rightV = visit(context->exp()[1]).as<Types::Value*>();

            Types::Value* result;

            double left = leftV->as_double_weak();
            double right = rightV->as_double_weak();

            OperatorMulDivMod op = visit(ctx).as<OperatorMulDivMod>();
            switch (op) {
            case OperatorMulDivMod::MUL:
                if (leftV->is<int>() && rightV->is<int>()) {
                    result = Types::Value::make(leftV->as<int>() * rightV->as<int>());
                } else {
                    result = Types::Value::make(left * right);
                }
                break;

            case OperatorMulDivMod::DIV:
                result = Types::Value::make(left / right);
                break;

            case OperatorMulDivMod::MOD:
                if (leftV->is<int>() && rightV->is<int>()) {
                    result = Types::Value::make(leftV->as<int>() % rightV->as<int>());
                } else {
                    result = Types::Value::make(std::remainder(left, right));
                }
                break;

            case OperatorMulDivMod::QUOT:
                if (leftV->is<int>() && rightV->is<int>()) {
                    result = Types::Value::make<int>(std::floor(left / right));
                } else {
                    result = Types::Value::make(std::floor(left / right));
                }
                break;

            default:
                throw std::runtime_error("Invalid MulDivMod operator");
            }

            leftV->remove_reference_if_free();
            rightV->remove_reference_if_free();

            return result;
        } else if (LuaParser::OperatorAddSubContext* ctx = context->operatorAddSub()) {
            Types::Value* leftV = visit(context->exp()[0]).as<Types::Value*>();
            Types::Value* rightV = visit(context->exp()[1]).as<Types::Value*>();

            Types::Value* result;

            OperatorAddSub op = visit(ctx);
            if (leftV->is<int>() && rightV->is<int>()) {
                int left = leftV->as<int>();
                int right = rightV->as<int>();

                switch (op) {
                case OperatorAddSub::ADD:
                    result = Types::Value::make(left + right);
                    break;

                case OperatorAddSub::SUB:
                    result = Types::Value::make(left - right);
                    break;

                default:
                    throw std::runtime_error("Invalid AddSub operator");
                }
            } else {
                double left = leftV->as_double_weak();
                double right = rightV->as_double_weak();

                switch (op) {
                case OperatorAddSub::ADD:
                    result = Types::Value::make(left + right);
                    break;

                case OperatorAddSub::SUB:
                    result = Types::Value::make(left - right);
                    break;

                default:
                    throw std::runtime_error("Invalid AddSub operator");
                }
            }

            leftV->remove_reference_if_free();
            rightV->remove_reference_if_free();

            return result;
        } else if (context->operatorStrcat()) {
            // Promote numbers to strings if necessary.
            // Note that Lua allows two numbers to concatenate as a string.
            Types::Value* leftV = visit(context->exp()[0]).as<Types::Value*>();
            Types::Value* rightV = visit(context->exp()[1]).as<Types::Value*>();

            std::string left = leftV->as_string();
            std::string right = rightV->as_string();

            leftV->remove_reference_if_free();
            rightV->remove_reference_if_free();

            return Types::Value::make(left + right);
        } else if (LuaParser::OperatorComparisonContext* ctx = context->operatorComparison()) {
            Types::Value* leftV = visit(context->exp()[0]).as<Types::Value*>();
            Types::Value* rightV = visit(context->exp()[1]).as<Types::Value*>();

            double left = leftV->as_double_weak();
            double right = rightV->as_double_weak();

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

            leftV->remove_reference_if_free();
            rightV->remove_reference_if_free();

            return Types::Value::make_bool(fn(left, right));
        } else if (context->operatorAnd()) {
            Types::Value* leftV = visit(context->exp()[0]).as<Types::Value*>();
            bool left = leftV->as_bool_weak();
            Types::Value* rightV = visit(context->exp()[1]).as<Types::Value*>();
            // bool right = rightV->as_bool_weak();

            // Decrease a single reference counter here because the value is
            // returned by reference.
            if (left) {
                leftV->remove_reference_if_free();
                return rightV;
            } else {
                rightV->remove_reference_if_free();
                return leftV; // false and nil == false, nil and false == nil
            }
        } else if (context->operatorOr()) {
            Types::Value* leftV = visit(context->exp()[0]).as<Types::Value*>();
            Types::Value* rightV = visit(context->exp()[1]).as<Types::Value*>();

            bool left = leftV->as_bool_weak();
            // bool right = rightV.as_bool_weak();

            if (left) {
                rightV->remove_reference_if_free();
                return leftV;
            } else {
                leftV->remove_reference_if_free();
                return rightV; // false or nil == nil, nil or false == false
            }
        } else if (LuaParser::OperatorBitwiseContext* ctx = context->operatorBitwise()) {
            // Promote exact floats / strings representing exact floats to int
            // for bitwise operations.
            Types::Value* leftV = visit(context->exp()[0]).as<Types::Value*>();
            Types::Value* rightV = visit(context->exp()[1]).as<Types::Value*>();

            int left = leftV->as_int_weak();
            int right = rightV->as_int_weak();

            Types::Value* result;

            OperatorBitwise op = visit(ctx).as<OperatorBitwise>();
            switch (op) {
            case OperatorBitwise::AND:
                result = Types::Value::make(left & right);
                break;

            case OperatorBitwise::LSHIFT:
                result = Types::Value::make(left << right);
                break;

            case OperatorBitwise::OR:
                result = Types::Value::make(left | right);
                break;

            case OperatorBitwise::RSHIFT:
                result = Types::Value::make(left >> right);
                break;

            case OperatorBitwise::XOR:
                result = Types::Value::make(left ^ right);
                break;

            default:
                throw std::runtime_error("Invalid Bitwise operator");
            }

            leftV->remove_reference_if_free();
            rightV->remove_reference_if_free();

            return result;
        } else {
            throw std::runtime_error("Invalid expression");
        }
    }

    virtual antlrcpp::Any visitPrefixexp(LuaParser::PrefixexpContext *context) {
        return nyi("Prefixexp");
    }

    virtual antlrcpp::Any visitFunctioncall(LuaParser::FunctioncallContext *context) {
        if (!context->varOrExp()->var_()) {
            return nyi("Functioncall#1");
        }

        LuaParser::Var_Context* var_context = context->varOrExp()->var_();
        std::string funcname(var_context->NAME()->getText());

        if (funcname != "ensure_value_type" && funcname != "expect_failure") {
            return nyi("Functioncall#2");
        }

        std::string expression(context->nameAndArgs()[0]->args()->explist()->exp()[0]->getText());

        if (funcname == "ensure_value_type") {
            Types::Value* left = visit(context->nameAndArgs()[0]->args()->explist()->exp()[0]).as<Types::Value*>();
            Types::Value* middle = visit(context->nameAndArgs()[0]->args()->explist()->exp()[1]).as<Types::Value*>();
            Types::Value* right = visit(context->nameAndArgs()[0]->args()->explist()->exp()[2]).as<Types::Value*>();

            std::string expression(context->nameAndArgs()[0]->args()->explist()->exp()[0]->getText());

            // Do not attempt to perform equality checks on reference types
            if (!middle->is_reference() && *left != *middle) {
                throw Exceptions::ValueEqualityExpected(expression, middle->value_as_string(), left->value_as_string());
            }
            std::string type(right->as<std::string>());
            if (type != "int" && type != "double" && type != "string" && type != "table" && type != "bool" && type != "nil") {
                throw std::runtime_error("Invalid type in ensure_type " + type);
            }

            if ((type == "int" && !left->is<int>()) ||
                (type == "double" && !left->is<double>()) ||
                (type == "string" && !left->is<std::string>()) ||
                (type == "table" && !left->is<Types::Table*>()) ||
                (type == "bool" && !left->is<bool>()) ||
                (type == "nil" && !left->is<Types::Nil>())) {
                throw Exceptions::TypeEqualityExpected(expression, type, left->type_as_string());
            }

            std::cout << "Expression " << expression << " has value " << left->value_as_string() << " of type " << left->type_as_string() << " (expected equivalent of " << middle->value_as_string() << " of type " << type << ") => OK" << std::endl;

            left->remove_reference_if_free();
            middle->remove_reference_if_free();
            right->remove_reference_if_free();
        } else {
            try {
                Types::Value* left = visit(context->nameAndArgs()[0]->args()->explist()->exp()[0]).as<Types::Value*>();
                left->remove_reference_if_free();
                throw std::runtime_error("Failure expected in expression " + expression);
            } catch (Exceptions::BadTypeException& e) {
                std::cout << "Expression " << expression << " rightfully triggered a type error" << std::endl;
            } catch (std::exception& e) {
                throw;
            }
        }

        return Types::Value::make_nil();
    }

    virtual antlrcpp::Any visitVarOrExp(LuaParser::VarOrExpContext *context) {
        return nyi("VarOrExp");
    }

    virtual antlrcpp::Any visitVar_(LuaParser::Var_Context *context) {
        return nyi("Var_");
    }

    virtual antlrcpp::Any visitVarSuffix(LuaParser::VarSuffixContext *context) {
        return nyi("VarSuffix");
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
        std::map<Types::Value*, Types::Value*> values;

        if (LuaParser::FieldlistContext* ctx = context->fieldlist()) {
            values = visit(ctx).as<std::map<Types::Value*, Types::Value*>>();
        }

        return Types::Value::make_table(values);
    }

    virtual antlrcpp::Any visitFieldlist(LuaParser::FieldlistContext *context) {
        std::map<Types::Value*, Types::Value*> values;
        unsigned int index = 1;
        for (LuaParser::FieldContext* ctx: context->field()) {
            std::pair<Types::Value*, Types::Value*> value = visit(ctx).as<std::pair<Types::Value*, Types::Value*>>();
            if (value.first && value.first->is<Types::Nil>()) {
                continue;
            }

            // If an integer key was already specified, and a value without a
            // key appears, use the next integer available as key, even if it
            // means overriding a value.
            if (!value.first) {
                if (value.second->is<Types::Nil>()) {
                    index++;
                    continue;
                }
                Types::Value* key = Types::Value::make((int)index++);
                values[key] = value.second;
            } else {
                values[value.first] = value.second;
            }
        }

        return values;
    }

    virtual antlrcpp::Any visitField(LuaParser::FieldContext *context) {
        std::string text(context->getText());

        if (text.starts_with("[")) {
            std::cout << "[NYI] Expressions as keys" << std::endl;
            return std::make_pair(Types::Value::make_nil(), Types::Value::make_nil());
        } else if (context->NAME()) {
            std::cout << "[NYI] Names as keys" << std::endl;
            return std::make_pair(Types::Value::make_nil(), Types::Value::make_nil());
        } else {
            return std::make_pair((Types::Value*)nullptr, visit(context->exp()[0]).as<Types::Value*>());
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
            return Types::Value::make(std::stoi(ptr->getText()));
        } else if (auto ptr = context->HEX()) {
            return Types::Value::make(std::stoi(ptr->getText(), nullptr, 16));
        } else if (auto ptr = context->FLOAT()) {
            return Types::Value::make(std::stod(ptr->getText()));
        } else if (auto ptr = context->HEX_FLOAT()) {
            std::cout << "[NYI] Hexfloat" << std::endl;
            return Types::Value::make(std::stod(ptr->getText()));
        } else {
            throw std::runtime_error("Invalid number");
        }
    }

    virtual antlrcpp::Any visitString(LuaParser::StringContext *context) {
        if (auto ptr = context->NORMALSTRING()) {
            std::string text(ptr->getText());
            return Types::Value::make(text.substr(1, text.size() - 2));
        } else if (auto ptr = context->CHARSTRING()) {
            std::string text(ptr->getText());
            return Types::Value::make(text.substr(1, text.size() - 2));
        } else if (auto ptr = context->LONGSTRING()) {
            std::cout << "[NYI] Longstring" << std::endl;
            return Types::Value::make(ptr->getText());
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

    void process_stat_var_list(LuaParser::VarlistContext* varlist, LuaParser::ExplistContext* explist) {

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
        std::vector<Types::Value*> values;

        if (el) {
             values = visit(el).as<std::vector<Types::Value*>>();
        } else {
            values.resize(names.size(), Types::Value::make_nil());
        }

        auto names_iter = names.begin();
        auto values_iter = values.begin();

        for (; names_iter != names.end() && values_iter != values.end(); ++names_iter, ++values_iter) {
            // Apparently Lua allows local a = 12; local a = 12...
            /* if (_local_values.top().find(*names_iter) != _local_values.top().end()) {
                throw Exceptions::NameAlreadyUsedException(*names_iter);
            } */

            _local_values.top()[*names_iter] = *values_iter;
            (*values_iter)->bind();
        }
    }

    void process_local_function(std::string const& name, LuaParser::FuncbodyContext* body) {

    }

    Types::Value* nyi(std::string const& str) {
        std::cout << "[NYI] " << str << std::endl;
        return Types::Value::make_nil();
    }

    typedef std::map<std::string, Types::Value*> ValueStore;

    // Scope processing works like a stack. Each time a new scope is
    // entered, push a new map on top of the stack to store the local
    // Values of the scope. Once the scope is exited, pop this map from
    // the stack.
    std::stack<ValueStore> _local_values;
    ValueStore _global_values;
};

/* template<typename Expected>
void test_expect_failure(std::string const& file) {
    std::ifstream stream(file);
    antlr4::ANTLRInputStream input(stream);
    LuaLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    LuaParser parser(&tokens);

    antlr4::tree::ParseTree* tree = parser.chunk();
    MyLuaVisitor visitor;

    try {
        visitor.visit(tree);
        std::cerr << tree->toStringTree(&parser, true) << std::endl;
        std::cerr << "Execution expected failure" << std::endl;
        std::terminate();
    } catch (Expected const& e) {
        std::cout << "Rightfully caught exception: " << e.what() << std::endl;
    } catch (std::exception const& e) {
        std::cerr << tree->toStringTree(&parser, true) << std::endl;
        std::cerr << e.what() << std::endl;
        std::terminate();
    }
}

void test_expect_success(std::string const& file) {
    std::ifstream stream(file);
    antlr4::ANTLRInputStream input(stream);
    LuaLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    LuaParser parser(&tokens);

    antlr4::tree::ParseTree* tree = parser.chunk();
    MyLuaVisitor visitor;
    try {
        visitor.visit(tree);
        std::cout << "Execution successfully completed" << std::endl;
    } catch (std::exception const& e) {
        std::cerr << "Unexpected error" << std::endl;
        std::cerr << tree->toStringTree(&parser, true) << std::endl;
        std::cerr << e.what() << std::endl;
        std::terminate();
    }
}

class AbstractTestData {
public:
    AbstractTestData(std::string const& filename) : _filename(filename) { }

    void run() {
        _run_fn();
    }

protected:
    std::string _filename;
    std::function<void()> _run_fn;
};

template<bool Fail, typename T>
struct TestData : public AbstractTestData {
    TestData(std::string const& filename) : AbstractTestData(filename) {
        _run_fn = std::bind_front(&TestData::run, this);
    }

    void run() {
        if constexpr (Fail) {
            test_expect_failure<T>(_filename);
        } else {
            test_expect_success(_filename);
        }
    }
};

template<bool Fail, typename Exception>
AbstractTestData* make_test_data(std::string const& filename) {
    if constexpr (Fail) {
        return new TestData<Fail, Exception>(filename);
    } else {
        return new TestData<Fail, void>(filename);
    }
} */

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

void run_test(fs::path const& path) {
    std::ifstream stream(path.string(), std::ios::in);

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
        throw std::runtime_error("Errors encountered while processing file " + path.string() + "\n");
    }

    antlr4::tree::ParseTree* tree = parser.chunk();
    MyLuaVisitor visitor;
    try {
        visitor.visit(tree);
        std::cout << "[OK] " << path << std::endl;
    } catch (std::exception& e) {
        std::ostringstream stream;
        stream << "Caught unexpected exception while processing file: " << path.string() <<
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

        run_test(p.path());
    }
}

int main() {
    /* make_test_data<false, void>("parse_test.lua")->run();
    make_test_data<true, Exceptions::ContextlessBadTypeException>("parse_test2.lua")->run();
    make_test_data<false, void>("parse_test3.lua")->run(); */
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
    MyLuaVisitor visitor;
    // try { visitor.visit(tree); } catch (std::exception& e) { std::cerr << "Parse base error: " << e.what() << std::endl; }

    tests();

    Types::Value::free();
    return 0;
}
