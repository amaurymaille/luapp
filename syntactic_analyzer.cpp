#include "exceptions.h"
#include "syntactic_analyzer.h"

SyntacticAnalyzer::SyntacticAnalyzer() {
    _current_context = nullptr;
}

void SyntacticAnalyzer::enterChunk(LuaParser::ChunkContext *ctx) {
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

void SyntacticAnalyzer::enterBlock(LuaParser::BlockContext *ctx) {
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

void SyntacticAnalyzer::exitBlock(LuaParser::BlockContext *ctx) {
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

void SyntacticAnalyzer::enterStat(LuaParser::StatContext *ctx) {
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
void SyntacticAnalyzer::enterFuncbody(LuaParser::FuncbodyContext *ctx) {
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
void SyntacticAnalyzer::exitFuncbody(LuaParser::FuncbodyContext *) {
    _stack_scopes.pop();
    if (_stack_scopes.empty()) {
        _current_scope = nullptr;
    } else {
        _current_scope = _stack_scopes.top();
    }
}

void SyntacticAnalyzer::enterLabel(LuaParser::LabelContext *ctx) {
    _current_scope->_scope_elements[_current_context].push_back(make_element(Label(ctx->NAME()->getText())));
}

void SyntacticAnalyzer::validate_gotos() const {
    std::set<LuaParser::BlockContext*> seen_contexts;

    for (const Scope& scope: _scopes) {
        LuaParser::BlockContext* ctx = scope._root_context;
        validate_labels(scope, ctx, seen_contexts);

        std::vector<std::string> seen_labels;
        std::vector<std::pair<std::vector<ScopeElement>::const_iterator, std::vector<ScopeElement>::const_iterator>> previous;
        explore_context(scope, scope._root_context, seen_labels, previous);
    }
}

bool SyntacticAnalyzer::is_associated_with_label(LuaParser::BlockContext const* ctx, std::string const& label) const {
    auto iter = _label_to_context.find(label);
    if (iter == _label_to_context.end()) {
        return false;
    } else {
        return std::find(iter->second.begin(), iter->second.end(), ctx) != iter->second.end();
    }
}

std::pair<SyntacticAnalyzer::BlocksPerLocal::iterator, SyntacticAnalyzer::BlocksPerLocal::iterator> SyntacticAnalyzer::get_context_for_local(LuaParser::BlockContext const* ctx, std::string const& name) {
    return _locals_per_block[const_cast<LuaParser::BlockContext*>(ctx)].equal_range(name);
}

std::pair<SyntacticAnalyzer::FunctionParents::iterator, SyntacticAnalyzer::FunctionParents::iterator> SyntacticAnalyzer::get_parents_of_function(LuaParser::BlockContext* fnctx) {
    return _functions_parents.equal_range(fnctx);
}

void SyntacticAnalyzer::explore_context(Scope const& scope,
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

void SyntacticAnalyzer::validate_goto(std::vector<std::pair<std::vector<ScopeElement>::const_iterator, std::vector<ScopeElement>::const_iterator>> const& previous,
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

void SyntacticAnalyzer::validate_labels(Scope const& scope, LuaParser::BlockContext* ctx, std::set<LuaParser::BlockContext*>& seen_contexts) const {
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
