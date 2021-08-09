#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <string>

#include <boost/program_options.hpp>

#include "antlr4-runtime.h"

#include "LuaLexer.h"
#include "LuaParser.h"

#include "exceptions.h"
#include "interpreter.h"


namespace fs = std::filesystem;
namespace po = boost::program_options;

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
        Interpreter visitor(tree);
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
        SyntacticAnalyzer listener;
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
