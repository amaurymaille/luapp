#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>

#include "antlr4-runtime.h"

#include "LuaParser.h"
#include "LuaVisitor.h"

#include "syntactic_analyzer.h"
#include "types.h"

class MyLuaVisitor : public LuaVisitor {
public:
    MyLuaVisitor(antlr4::tree::ParseTree* tree);

    ~MyLuaVisitor();

    virtual antlrcpp::Any visitChunk(LuaParser::ChunkContext *context);

    virtual antlrcpp::Any visitBlock(LuaParser::BlockContext *context);

    virtual antlrcpp::Any visitStat(LuaParser::StatContext *context);

    virtual antlrcpp::Any visitAttnamelist(LuaParser::AttnamelistContext *context);

    virtual antlrcpp::Any visitAttrib(LuaParser::AttribContext *);

    virtual antlrcpp::Any visitRetstat(LuaParser::RetstatContext *context);

    virtual antlrcpp::Any visitLabel(LuaParser::LabelContext *);

    virtual antlrcpp::Any visitFuncname(LuaParser::FuncnameContext *context);

    virtual antlrcpp::Any visitVarlist(LuaParser::VarlistContext *context);

    virtual antlrcpp::Any visitNamelist(LuaParser::NamelistContext *context);

    virtual antlrcpp::Any visitExplist(LuaParser::ExplistContext *context);

    virtual antlrcpp::Any visitExp(LuaParser::ExpContext *context);

    virtual antlrcpp::Any visitPrefixexp(LuaParser::PrefixexpContext *context);

    virtual antlrcpp::Any visitFunctioncall(LuaParser::FunctioncallContext *context);

    virtual antlrcpp::Any visitVarOrExp(LuaParser::VarOrExpContext *context);

    virtual antlrcpp::Any visitVar_(LuaParser::Var_Context *context);

    virtual antlrcpp::Any visitVarSuffix(LuaParser::VarSuffixContext *context);

    virtual antlrcpp::Any visitNameAndArgs(LuaParser::NameAndArgsContext *context);

    virtual antlrcpp::Any visitArgs(LuaParser::ArgsContext *context);

    virtual antlrcpp::Any visitFunctiondef(LuaParser::FunctiondefContext *context);

    virtual antlrcpp::Any visitFuncbody(LuaParser::FuncbodyContext *context);

    virtual antlrcpp::Any visitParlist(LuaParser::ParlistContext *context);

    virtual antlrcpp::Any visitTableconstructor(LuaParser::TableconstructorContext *context);

    virtual antlrcpp::Any visitFieldlist(LuaParser::FieldlistContext *context);

    virtual antlrcpp::Any visitField(LuaParser::FieldContext *context);

    virtual antlrcpp::Any visitFieldsep(LuaParser::FieldsepContext *);

    virtual antlrcpp::Any visitOperatorOr(LuaParser::OperatorOrContext *);

    virtual antlrcpp::Any visitOperatorAnd(LuaParser::OperatorAndContext *);

    virtual antlrcpp::Any visitOperatorComparison(LuaParser::OperatorComparisonContext *context);

    virtual antlrcpp::Any visitOperatorStrcat(LuaParser::OperatorStrcatContext *);

    virtual antlrcpp::Any visitOperatorAddSub(LuaParser::OperatorAddSubContext *context);

    virtual antlrcpp::Any visitOperatorMulDivMod(LuaParser::OperatorMulDivModContext *context);

    virtual antlrcpp::Any visitOperatorBitwise(LuaParser::OperatorBitwiseContext *context);

    virtual antlrcpp::Any visitOperatorUnary(LuaParser::OperatorUnaryContext *context);

    virtual antlrcpp::Any visitOperatorPower(LuaParser::OperatorPowerContext *);

    virtual antlrcpp::Any visitNumber(LuaParser::NumberContext *context);

    virtual antlrcpp::Any visitString(LuaParser::StringContext *context);

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
        ArgsVisitor(std::vector<Types::Value>& dest);

        void operator()(std::vector<Types::Var> const& args);
        void operator()(TableConstructor const& cons);
        void operator()(String const& string);

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

    void process_stat_var_list(LuaParser::VarlistContext* varlist, LuaParser::ExplistContext* explist);

    void process_break();

    void process_goto(std::string const& label);

    void process_while(LuaParser::ExpContext* exp, LuaParser::BlockContext* block);

    void process_repeat(LuaParser::BlockContext* block, LuaParser::ExpContext* exp);

    void process_if(LuaParser::StatContext* ctx);

    void process_for_in(LuaParser::NamelistContext* nl, LuaParser::ExplistContext* el, LuaParser::BlockContext* block);

    void process_for_loop(LuaParser::StatContext* ctx);

    void process_function(LuaParser::FuncnameContext* name, LuaParser::FuncbodyContext*) ;

    void process_local_variables(LuaParser::AttnamelistContext* al, LuaParser::ExplistContext* el);

    void process_local_function(std::string const& name, LuaParser::FuncbodyContext* body);

    Types::Var nyi(std::string const& str);

    std::pair<Types::Value*, Scope> lookup_name(std::string const& name, bool should_throw = true);

    LuaParser::BlockContext* current_block();

    Types::Function* current_function();

    void stabilize_blocks(LuaParser::BlockContext* context);

    void erase_block(LuaParser::BlockContext* context);

    void clear_block(LuaParser::BlockContext* context);

    void close_function(Types::Function* function, LuaParser::BlockContext* body);

    std::vector<Types::Var> call_function(Types::Function* function, std::vector<Types::Value> const& values);

    bool funcall_test_infrastructure(LuaParser::FunctioncallContext* context);

    Types::Var process_names_and_args(Types::Var const& src, std::vector<NameAndArgs> const& names_and_args);

    typedef std::map<std::string, Types::Value*> ValueStore;

    // Scope processing works like a stack. Each time a new scope is
    // entered, push a new map on top of the stack to store the local
    // Values of the scope. Once the scope is exited, pop this map from
    // the stack.
    std::vector<std::map<LuaParser::BlockContext*, ValueStore>> _local_values;
    ValueStore _global_values;

    SyntacticAnalyzer _listener;

    std::vector<LuaParser::BlockContext*> _blocks;
    std::vector<Types::Function*> _functions;
    bool _coming_from_for = false;
    bool _coming_from_funcall = false;
};
