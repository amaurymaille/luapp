#include <any>
#include <cassert>
#include <cctype>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

struct NullType { };

// return-of

template<typename T>
struct return_of;

template<typename R, typename... Args>
struct return_of<R(Args...)> {
    typedef R type;
};

template<typename R>
using return_of_t = typename return_of<R>::type;

// args-list

template<typename F>
struct args_list;

template<typename F>
struct args_list<F()> {
    typedef void Head;
    using Tail = NullType;
};

template<typename F, typename T, typename... Args>
struct args_list<F(T, Args...)> {
    typedef T Head;
    using Tail = F(Args...);
};

template<typename F, typename T>
struct args_list<F(T)> {
    typedef T Head;
    using Tail = F();
};

// Lua

class PartialFunctionCall : public std::exception {
public:
    PartialFunctionCall() { }

    const char* what() const noexcept {
        return "Attempt to call function without all arguments bound";
    }
};

class FunctionArgumentCountOverflow : public std::exception {
public:
    FunctionArgumentCountOverflow() { }

    const char* what() const noexcept {
        return "Attempt to bind more effective arguments than function's formal arguments count";
    }
};

template<typename F>
class PartialLuaFunction;

class AbstractLuaFunction {
public:
    template<typename F>
    static AbstractLuaFunction* register_signature() {
        return new PartialLuaFunction<F>();
    }

    virtual ~AbstractLuaFunction() {

    }

    template<typename T>
    void bind_next(T&& t) {
        (*_bind_fn)(std::any(std::move(t)), 0);
    }

    void invoke(std::optional<std::any>& a) {
        a = _invoke_fn();
    }

protected:
    AbstractLuaFunction() {
        _bind_fn = std::make_shared<std::function<void(std::any, size_t)>>();
    }

    std::shared_ptr<std::function<void(std::any, size_t)>> _bind_fn = nullptr;
    std::function<std::optional<std::any>()> _invoke_fn;
};

template<typename F>
class PartialLuaFunction<F()> : public AbstractLuaFunction {
    using Sig = F();
public:
    PartialLuaFunction() {
        // std::cout << "Empty" << std::endl;
    }

    ~PartialLuaFunction() {

    }

    void init(Sig&& sig) {
        _fn = std::move(sig);
        _invoke_fn = [&]() -> std::optional<std::any> {
            if constexpr (std::is_same_v<void, F>) {
                this->invoke();
                return std::nullopt;
            } else {
                std::any a(this->invoke());
                return std::make_optional(a);
            }
        };
        configure_bind_fn();
    }

    void init_bind(std::function<Sig>&& sig) {
        _fn = std::move(sig);
    }

    void share_bind_fn(std::shared_ptr<std::function<void(std::any, size_t)>>& fn) { _bind_fn = fn; }
    void configure_bind_fn() {
        (*_bind_fn) = [&](std::any a, size_t pos) {
            // std::cout << "Calling lambda with this = " << &(*this) << std::endl;
            this->bind_next(a, pos);
        };
    }

    template<typename T>
    void bind_next(T&& t, size_t pos) {
        throw FunctionArgumentCountOverflow();
    }

    F invoke() {
        return _fn();
    }

private:
    std::function<Sig> _fn;
};

template<typename F, typename... Args>
class PartialLuaFunction<F(Args...)> : public AbstractLuaFunction {
private:
    using ArgsList = args_list<F(Args...)>;
    using Sig = F(Args...);

public:
    typedef ArgsList::Head ArgType;
    using Next = PartialLuaFunction<typename ArgsList::Tail>;

    PartialLuaFunction() {
        // std::cout << "With type " << typeid(ArgType).name() << std::endl;
    }

    ~PartialLuaFunction() {

    }

    template<typename T>
    void bind_next(T&& t, size_t pos) {
        _bound = true;
        _next.share_bind_fn(_bind_fn);
        _next.configure_bind_fn();
        _next.init_bind(std::bind_front(_fn, t));
    }

    void share_bind_fn(std::shared_ptr<std::function<void(std::any, size_t)>>& fn) {
        _bind_fn = fn;
    }

    void configure_bind_fn() {
        (*_bind_fn) = [&](std::any a, size_t pos) {
            // std::cout << "Calling lambda with this = " << &(*this) << std::endl;
            this->bind_next(std::any_cast<ArgType>(a), pos);
        };
    }

    void init(Sig&& sig) {
        _fn = std::move(sig);
        _invoke_fn = [&]() -> std::optional<std::any> {
            if constexpr (std::is_same_v<void, F>) {
                this->invoke();
                return std::nullopt;
            } else {
                std::any a(this->invoke());
                return std::make_optional(a);
            }
        };
        configure_bind_fn();
    }

    void init_bind(std::function<Sig>&& sig) {
        _fn = std::move(sig);
    }

    F invoke() {
        if (!_bound)
            throw PartialFunctionCall();
        return _next.invoke();
    }

private:
    std::function<Sig> _fn;
    Next _next;
    bool _bound = false;
};

void kjdfejhf() { }

class AbstractLuaFunctionAbstractBuilder {
public:
    AbstractLuaFunction* build() const {
        return _build_fn();
    }

protected:
    AbstractLuaFunctionAbstractBuilder() { }

    std::function<AbstractLuaFunction*()> _build_fn;
};

template<typename F>
class AbstractLuaFunctionBuilder : public AbstractLuaFunctionAbstractBuilder{
public:
    AbstractLuaFunctionBuilder(F&& f) : _f(f) {
        _build_fn = std::bind_front(&AbstractLuaFunctionBuilder<F>::build, this);
    }

    AbstractLuaFunction* build() const {
        PartialLuaFunction<F>* function = new PartialLuaFunction<F>();
        function->init(_f);
        return function;
    }

private:
    F&& _f;
};

typedef std::map<std::string, AbstractLuaFunctionAbstractBuilder*> FunctionAssocMap;

std::vector<std::string> string_split(std::string const& str, char delim) {
    const char* c_str = str.c_str();
    std::string::size_type pos = 0;
    std::vector<std::string> results;
    while (true) {
        pos = std::string(c_str).find(delim);
        if (pos == std::string::npos) {
            std::string final(c_str);
            if (!final.empty())
                results.push_back(std::string(c_str));
            return results;
        }

        results.push_back(std::string(c_str, c_str + pos));
        c_str += pos + 1;
    }

    return results;
}

void string_to_lower(std::string& str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
}

void process_argument(AbstractLuaFunction* function, std::string const& argument) {
    auto split = string_split(argument, ':');
    std::string type = split[0], value = split[1];

    if (type == "Int") {
        function->bind_next<int>(std::stoi(value));
    } else if (type == "Float") {
        function->bind_next<float>(std::stof(value));
    } else if (type == "Double") {
        function->bind_next<double>(std::stod(value));
    } else if (type == "Bool") {
        string_to_lower(value);
        if (std::stoi(value) == 0 || std::stod(value) == .0 || value == "false" || value == "f")
            function->bind_next<bool>(false);
        else
            function->bind_next<bool>(true);
    }
}

std::optional<std::any> parse_function_invocation(FunctionAssocMap& assoc,
                               std::string const& invocation) {
    // std::cout << "Line: " << invocation << std::endl;
    size_t open_paren = invocation.find('(');
    std::string name(invocation.substr(0, open_paren));

    // std::cout << "Function name: " << name << std::endl;

    std::string args(invocation.substr(open_paren + 1, invocation.size() - open_paren - 2));
    // std::cout << "Argument list: " << args << std::endl;

    std::unique_ptr<AbstractLuaFunction> lua_function(assoc[name]->build());

    try {
        for (std::string const& argument: string_split(args, ',')) {
            process_argument(lua_function.get(), argument);
        }
    } catch(std::exception& e) {
        std::cerr << invocation << " => " << e.what() << std::endl;
        throw;
    }

    std::optional<std::any> result;
    try {
        lua_function->invoke(result);
        return result;
    } catch (std::exception& e) {
        std::cerr << invocation << " => " << e.what() << std::endl;
        throw;
    }
}

void luafn(int i, float f, double d, bool b) {
    std::cout << "luafn: i = " << i << ", f = " << f << ", d = " << d << ", b = " << b << std::endl;
}

void toto() {
    std::cout << "toto" << std::endl;
}

int tata(int a) {
    std::cout << "tata: a = " << a << std::endl;
    return a;
}

int main() {
    FunctionAssocMap map;
    map["toto"] = new AbstractLuaFunctionBuilder(luafn);
    map["tutu"] = new AbstractLuaFunctionBuilder(tata);

    try {
        std::cout << "toto(Int:10,Float:3.5,Double:-2.5,Bool:8.2)" << std::endl;
        auto res = parse_function_invocation(map, "toto(Int:10,Float:3.5,Double:-2.5,Bool:8.2)");
        assert(!res);
    } catch (std::exception& e) {
        assert(false);
    }

    std::cout << std::endl;

    try {
        std::cout << "tutu()" << std::endl;
        auto res = parse_function_invocation(map, "tutu()");
        std::cout << "Result = " << std::any_cast<int>(*res) << std::endl;
    } catch (PartialFunctionCall& e) {

    } catch (std::exception& e) {
        assert(false);
    }
    std::cout << std::endl;

    try {
        std::cout << "tutu(Int:12)" << std::endl;
        auto res = parse_function_invocation(map, "tutu(Int:12)");
        std::cout << "Result = " << std::any_cast<int>(*res) << std::endl;
    }  catch (std::exception& e) {
        assert(false);
    }

    std::cout << std::endl;

    try {
        std::cout << "tutu(Int:12,Float:3.5)" << std::endl;
        auto res = parse_function_invocation(map, "tutu(Int:12,Float:3.5)");
        std::cout << "Result = " << std::any_cast<int>(*res) << std::endl;
    } catch (FunctionArgumentCountOverflow& e) {

    } catch (std::exception& e) {
        assert(false);
    }

    std::cout << std::endl;

    for (auto& p: map) {
        delete p.second;
    }

    return 0;
}
