#pragma once

#include <list>
#include <map>
#include <set>
#include <stack>
#include <vector>

#include "LuaBaseListener.h"
#include "LuaParser.h"

/* Preemptive pass on the whole file being interpreted to check if gotos and
 * breaks are legit, store information about blocks relations in order to
 * compute closures properly and so on.
 */
class SyntacticAnalyzer : public LuaBaseListener {
public:
    typedef std::multimap<std::string, LuaParser::BlockContext*> BlocksPerLocal;
    typedef std::multimap<LuaParser::BlockContext*, LuaParser::BlockContext*> FunctionParents;

public:
    SyntacticAnalyzer();

    void enterChunk(LuaParser::ChunkContext *ctx);

    void enterBlock(LuaParser::BlockContext *ctx);

    void exitBlock(LuaParser::BlockContext *ctx);

    void enterStat(LuaParser::StatContext *ctx);

    // Beginning of the scope of an inner function
    void enterFuncbody(LuaParser::FuncbodyContext *ctx);

    // End of the scope of an inner function
    void exitFuncbody(LuaParser::FuncbodyContext *);

    void enterLabel(LuaParser::LabelContext *ctx);

    void validate_gotos() const;

    bool is_associated_with_label(LuaParser::BlockContext const* ctx, std::string const& label) const;

    std::pair<BlocksPerLocal::iterator, BlocksPerLocal::iterator> get_context_for_local(LuaParser::BlockContext const* ctx, std::string const& name);

    std::pair<FunctionParents::iterator, FunctionParents::iterator> get_parents_of_function(LuaParser::BlockContext* fnctx);

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
                         std::vector<std::pair<std::vector<ScopeElement>::const_iterator, std::vector<ScopeElement>::const_iterator>> const& previous) const;

    void validate_goto(std::vector<std::pair<std::vector<ScopeElement>::const_iterator, std::vector<ScopeElement>::const_iterator>> const& previous,
                       std::string const& search) const;

    void validate_labels(Scope const& scope, LuaParser::BlockContext* ctx, std::set<LuaParser::BlockContext*>& seen_contexts) const;

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

