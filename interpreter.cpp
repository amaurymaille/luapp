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

#include "exceptions.h"
#include "interpreter.h"
#include "syntactic_analyzer.h"
#include "types.h"

Interpreter::Interpreter(antlr4::tree::ParseTree* tree) {
    antlr4::tree::ParseTreeWalker::DEFAULT.walk(&_listener, tree);
    _listener.validate_gotos();
}

Interpreter::~Interpreter() {
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

antlrcpp::Any Interpreter::visitChunk(LuaParser::ChunkContext *context) {
    // std::cout << "Chunk: " << context->getText() << std::endl;
    try {
        _local_values.push_back(decltype(_local_values)::value_type());
        return visit(context->block());
    } catch (Exceptions::Return& ret) {
        return ret.get();
    }
}

antlrcpp::Any Interpreter::visitBlock(LuaParser::BlockContext *context) {
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

antlrcpp::Any Interpreter::visitStat(LuaParser::StatContext *context) {
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

antlrcpp::Any Interpreter::visitAttnamelist(LuaParser::AttnamelistContext *context) {
    std::vector<antlr4::tree::TerminalNode*> names = context->NAME();
    std::vector<std::string> result;
    std::transform(names.cbegin(), names.cend(), std::back_inserter(result), [](antlr4::tree::TerminalNode* node) {
                       return node->toString();
                   });
    return result;
}

antlrcpp::Any Interpreter::visitAttrib(LuaParser::AttribContext *) {
    std::cerr << "Attributes are not supported" << std::endl;
    return Types::Var::make(Types::Value::make_nil());
}

antlrcpp::Any Interpreter::visitRetstat(LuaParser::RetstatContext *context) {
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

antlrcpp::Any Interpreter::visitLabel(LuaParser::LabelContext *) {
    return Types::Var::make(Types::Value::make_nil());
}

antlrcpp::Any Interpreter::visitFuncname(LuaParser::FuncnameContext *context) {
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

antlrcpp::Any Interpreter::visitVarlist(LuaParser::VarlistContext *context) {
    _var__context = Var_Context::VARLIST;
    std::vector<Types::Var> vars;
    for (LuaParser::Var_Context* ctx: context->var_()) {
        vars.push_back(visit(ctx).as<Types::Var>());
    }

    _var__context = Var_Context::OTHER;
    return vars;
}

antlrcpp::Any Interpreter::visitNamelist(LuaParser::NamelistContext *context) {
    std::vector<antlr4::tree::TerminalNode*> names(context->NAME());
    std::vector<std::string> retval;

    std::transform(names.begin(), names.end(), std::back_inserter(retval), [](antlr4::tree::TerminalNode* node) { return node->getText(); });

    return retval;
}

antlrcpp::Any Interpreter::visitExplist(LuaParser::ExplistContext *context) {
    std::vector<Types::Var> values;
    for (LuaParser::ExpContext* ctx: context->exp()) {
        values.push_back(visit(ctx).as<Types::Var>());
    }

    return values;
}

antlrcpp::Any Interpreter::visitExp(LuaParser::ExpContext *context) {
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

antlrcpp::Any Interpreter::visitPrefixexp(LuaParser::PrefixexpContext *context) {
    Types::Var val = visit(context->varOrExp()).as<Types::Var>();
    std::vector<NameAndArgs> names_and_args;
    for (LuaParser::NameAndArgsContext* ctx: context->nameAndArgs()) {
        names_and_args.push_back(visit(ctx).as<NameAndArgs>());
    }

    return process_names_and_args(val, names_and_args);
}

antlrcpp::Any Interpreter::visitFunctioncall(LuaParser::FunctioncallContext *context) {
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

antlrcpp::Any Interpreter::visitVarOrExp(LuaParser::VarOrExpContext *context) {
    Types::Var result;
    if (context->var_()) {
        result = visit(context->var_()).as<Types::Var>();
    } else if (context->exp()) {
        result = visit(context->exp()).as<Types::Var>();
    }

    return result;
}

antlrcpp::Any Interpreter::visitVar_(LuaParser::Var_Context *context) {
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

antlrcpp::Any Interpreter::visitVarSuffix(LuaParser::VarSuffixContext *context) {
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

antlrcpp::Any Interpreter::visitNameAndArgs(LuaParser::NameAndArgsContext *context) {
    NameAndArgs res;
    if (context->NAME()) {
        res._name = context->NAME()->getText();
    }

    res._args = visit(context->args()).as<Args>();

    return res;
}

antlrcpp::Any Interpreter::visitArgs(LuaParser::ArgsContext *context) {
    Args args;

    if (context->explist()) {
        args = visit(context->explist()).as<std::vector<Types::Var>>();
        std::vector<Types::Var>& values = std::get<std::vector<Types::Var>>(args);

        // Immediately expand the last argument if it is an elipsis or
        // a list of values. This will help when processing arguments
        // further down the road.
        std::vector<Types::Value> remains;
        if (values.size() != 0) {
            Types::Var& last = values.back();
            if (last.is<Types::Elipsis>()) {
                remains = last.as<Types::Elipsis>().values();
            } else if (last.list()) {
                remains = last._list();
            }
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

antlrcpp::Any Interpreter::visitFunctiondef(LuaParser::FunctiondefContext *context) {
    std::vector<std::string> names = visit(context->funcbody()).as<std::vector<std::string>>();
    Types::Function* function = new Types::Function(std::move(names), context->funcbody()->block());
    close_function(function, context->funcbody()->block());
    Types::Value v; v._type = function;
    sGC->add_reference(v._type);
    return Types::Var::make(v);
}

antlrcpp::Any Interpreter::visitFuncbody(LuaParser::FuncbodyContext *context) {
    if (LuaParser::ParlistContext* ctx = context->parlist()) {
        return visit(ctx);
    } else {
        return std::vector<std::string>();
    }
}

antlrcpp::Any Interpreter::visitParlist(LuaParser::ParlistContext *context) {
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

antlrcpp::Any Interpreter::visitTableconstructor(LuaParser::TableconstructorContext *context) {
    std::list<std::pair<Types::Value, Types::Value>> values;

    if (LuaParser::FieldlistContext* ctx = context->fieldlist()) {
        values = visit(ctx).as<std::list<std::pair<Types::Value, Types::Value>>>();
    }

    return Types::Var::make(Types::Value::make_table(values));
}

antlrcpp::Any Interpreter::visitFieldlist(LuaParser::FieldlistContext *context) {
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

antlrcpp::Any Interpreter::visitField(LuaParser::FieldContext *context) {
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

antlrcpp::Any Interpreter::visitFieldsep(LuaParser::FieldsepContext *) {
    return nullptr;
}

antlrcpp::Any Interpreter::visitOperatorOr(LuaParser::OperatorOrContext *) {
    return nullptr;
}

antlrcpp::Any Interpreter::visitOperatorAnd(LuaParser::OperatorAndContext *) {
    return nullptr;
}

antlrcpp::Any Interpreter::visitOperatorComparison(LuaParser::OperatorComparisonContext *context) {
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

antlrcpp::Any Interpreter::visitOperatorStrcat(LuaParser::OperatorStrcatContext *) {
    return nullptr;
}

antlrcpp::Any Interpreter::visitOperatorAddSub(LuaParser::OperatorAddSubContext *context) {
    std::string symbol(context->getText());

    if (symbol == "+") {
        return OperatorAddSub::ADD;
    } else if (symbol == "-") {
        return OperatorAddSub::SUB;
    } else {
        return OperatorAddSub::ERROR;
    }
}

antlrcpp::Any Interpreter::visitOperatorMulDivMod(LuaParser::OperatorMulDivModContext *context) {
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

antlrcpp::Any Interpreter::visitOperatorBitwise(LuaParser::OperatorBitwiseContext *context) {
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

antlrcpp::Any Interpreter::visitOperatorUnary(LuaParser::OperatorUnaryContext *context) {
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

antlrcpp::Any Interpreter::visitOperatorPower(LuaParser::OperatorPowerContext *) {
    return nullptr;
}

antlrcpp::Any Interpreter::visitNumber(LuaParser::NumberContext *context) {
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

antlrcpp::Any Interpreter::visitString(LuaParser::StringContext *context) {
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

Interpreter::ArgsVisitor::ArgsVisitor(std::vector<Types::Value>& dest) : _dest(dest) { }

void Interpreter::ArgsVisitor::operator()(std::vector<Types::Var> const& args) {
    std::transform(args.begin(), args.end(), std::back_inserter(_dest), [](Types::Var const& var) { return var.get(); });
}

void Interpreter::ArgsVisitor::operator()(TableConstructor const& cons) {
    _dest.push_back(cons._var.get());
}

void Interpreter::ArgsVisitor::operator()(String const& string) {
    _dest.push_back(string._var.get());
}

void Interpreter::process_stat_var_list(LuaParser::VarlistContext* varlist, LuaParser::ExplistContext* explist) {
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
    unsigned int i = exprs.size();
    if (exprs.size() < vars.size()) {
        if (exprs.size() != 0) {
            std::vector<Types::Value> remains;
            unsigned int j;
            Types::Var& last = exprs.back();

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
        }



        for (; i < vars.size(); ++i) {
            vars[i]._lvalue()->value() = Types::Nil();
        }
    }
}

void Interpreter::process_break() {
    throw Exceptions::Break();
}

void Interpreter::process_goto(std::string const& label) {
    throw Exceptions::Goto(label);
}

void Interpreter::process_while(LuaParser::ExpContext* exp, LuaParser::BlockContext* block) {
    LuaParser::BlockContext* current = current_block();
    try {
        while (visit(exp).as<Types::Var>().as_bool_weak()) {
            visit(block);
        }
    } catch (Exceptions::Break& brk) {
        stabilize_blocks(current);
    }
}

void Interpreter::process_repeat(LuaParser::BlockContext* block, LuaParser::ExpContext* exp) {
    LuaParser::BlockContext* current = current_block();
    try {
        do {
            visit(block);
        } while (!visit(exp).as<Types::Var>().as_bool_weak());
    } catch (Exceptions::Break& brk) {
        stabilize_blocks(current);
    }
}

void Interpreter::process_if(LuaParser::StatContext* ctx) {
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

void Interpreter::process_for_in(LuaParser::NamelistContext* nl, LuaParser::ExplistContext* el, LuaParser::BlockContext* block) {
    std::vector<std::string> names = visit(nl).as<std::vector<std::string>>();
    std::vector<Types::Var> exprs = visit(el).as<std::vector<Types::Var>>();

    /* for var1, ..., varn in exprlist do
     *   stmts
     * end
     *
     * <=>
     *
     * do
     *   local f, s, var = exprlist
     *   while true do
     *     local var1, ..., varn = f(s, var)
     *     if var1 == nil then break end
     *     var = var1
     *     stmts
     *   end
     * end
     *
     * loop_values holds the results of exprlist.
     */
    std::vector<Types::Value> loop_values;
    for (unsigned int i = 0; i < exprs.size() - 1; ++i) {
        loop_values.push_back(exprs[i].get());
    }

    if (exprs.size() == 0) {
        throw std::runtime_error("How the hell did you produce an empty expression vector in a `for in`?");
    }

    // Expand only the last element of the expression list, as always.
    // Yes I could factor it, but how do I keep those sweet sweet references ?
    if (exprs.back().list()) {
        std::vector<Types::Value> const& values = exprs.back()._list();
        std::ranges::for_each(values, [&loop_values](Types::Value const& value) { loop_values.push_back(value);});
    } else if (exprs.back().is<Types::Elipsis>()) {
        std::vector<Types::Value> const& values = exprs.back().as<Types::Elipsis>().values();
        std::ranges::for_each(values, [&loop_values](Types::Value const& value) { loop_values.push_back(value);});
    }

    if (loop_values.size() < 1) {
        throw Exceptions::BadForIn();
    }

    if (!loop_values.front().is<Types::Function*>()) {
        throw Exceptions::ForInBadType(loop_values.front().type_as_string());
    }

    LuaParser::BlockContext* current = current_block();
    _blocks.push_back(block);

    Types::Value state;
    Types::Value iteration_value;

    if (loop_values.size() >= 2) {
        state = loop_values[1];
    }

    if (loop_values.size() >= 3) {
        iteration_value = loop_values[2];
    }

    try {
        while (true) {
            std::vector<Types::Var> results = call_function(loop_values.front().as<Types::Function*>(), std::vector<Types::Value>{state, iteration_value});
            if (results.size() == 0 || results[0].is<Types::Nil>()) {
                throw Exceptions::Break();
            }

            iteration_value = results[0].get();

            for (unsigned int i = 0; i < std::min(results.size(), names.size()); ++i) {
                Types::Value* value = new Types::Value;
                *value = results[i].get();
                _local_values.back()[block][names[i]] = value;
            }

            if (results.size() < names.size()) {
                unsigned int i;
                if (results.back().is<Types::Elipsis>()) {
                    std::vector<Types::Value> const& remains = results.back().as<Types::Elipsis>().values();
                    i = results.size() - 1;
                    for (unsigned int j = 0 ; j < remains.size() && i < names.size(); ++i, ++j) {
                        if (i < results.size()) {
                            sGC->remove_reference(_local_values.back()[block][names[i]]->value());
                            *(_local_values.back()[block][names[i]]) = remains[j];
                        } else {
                            Types::Value* value = new Types::Value;
                            *value = remains[j];
                            _local_values.back()[block][names[i]] = value;
                        }
                    }
                } else if (results.back().list()) {
                    std::vector<Types::Value> const& remains = results.back()._list();
                    i = results.size();
                    for (unsigned int j = 1; j < remains.size() && i < names.size(); ++i, ++j) {
                        Types::Value* value = new Types::Value;
                        *value = remains[j];
                        _local_values.back()[block][names[i]] = value;
                    }
                }

                for (; i < names.size(); ++i) {
                    _local_values.back()[block][names[i]] = new Types::Value;
                }
            }

            _coming_from_for = true;
            visit(block);

            clear_block(block);
            _local_values.back()[block].clear();
        }
    } catch (Exceptions::Break& brk) {
        stabilize_blocks(current);
    }
}

void Interpreter::process_for_loop(LuaParser::StatContext* ctx) {
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

void Interpreter::process_function(LuaParser::FuncnameContext* name, LuaParser::FuncbodyContext*) {
    visit(name);
}

void Interpreter::process_local_variables(LuaParser::AttnamelistContext* al, LuaParser::ExplistContext* el) {
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

    unsigned int i = values.size();
    // Adjust
    if (values.size() < names.size()) {
        if (values.size() != 0) {
            unsigned int j;
            std::vector<Types::Value> remains;
            Types::Var& last = values.back();

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
        }


        for (; i < names.size(); ++i) {
            _local_values.back()[current_block()][names[i]] = new Types::Value();
        }
    }
}

void Interpreter::process_local_function(std::string const& name, LuaParser::FuncbodyContext* body) {
    Types::Function* f = new Types::Function(std::move(visit(body->parlist()).as<std::vector<std::string>>()), body->block());
    close_function(f, body->block());
    Types::Value* value = new Types::Value;
    value->_type = f;
    sGC->add_reference(value->value());
    _local_values.back()[current_block()][name] = value;
}

Types::Var Interpreter::nyi(std::string const& str) {
    std::cout << "[NYI] " << str << std::endl;
    return Types::Var::make(Types::Value::make_nil());
}

std::pair<Types::Value*, Interpreter::Scope> Interpreter::lookup_name(std::string const& name, bool should_throw) {
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

LuaParser::BlockContext* Interpreter::current_block() {
    if (_blocks.empty()) {
        return nullptr;
    } else {
        return _blocks.back();
    }
}

Types::Function* Interpreter::current_function() {
    if (_functions.empty()) {
        return nullptr;
    } else {
        return _functions.back();
    }
}

void Interpreter::stabilize_blocks(LuaParser::BlockContext* context) {
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

void Interpreter::erase_block(LuaParser::BlockContext* context) {
    clear_block(context);
    _local_values.back().erase(context);
}

void Interpreter::clear_block(LuaParser::BlockContext* context) {
    auto& data = _local_values.back()[context];
    for (auto& p: data) {
        p.second->remove_reference();
    }
}

void Interpreter::close_function(Types::Function* function, LuaParser::BlockContext* body) {
    auto pair = _listener.get_parents_of_function(body);
    for (auto it = pair.first; it != pair.second; ++it) {
        auto& store = _local_values.back()[it->second];
        for (auto& p: store) {
            function->close(p.first, p.second);
        }
    }
}

std::vector<Types::Var> Interpreter::call_function(Types::Function* function, std::vector<Types::Value> const& values) {
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

bool Interpreter::funcall_test_infrastructure(LuaParser::FunctioncallContext* context) {
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

Types::Var Interpreter::process_names_and_args(Types::Var const& src, std::vector<NameAndArgs> const& names_and_args) {
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
