#include "antlr4-runtime.h"

#include "LuaLexer.h"
#include "LuaParser.h"

#include "environment.h"

void Environment::run_file(const std::string &file) {
    std::ifstream stream(file, std::ios::in);

    if (!stream) {
        std::cout << "File " << file << " not found." << std::endl;
        return;
    }

    antlr4::ANTLRInputStream input(stream);
    LuaLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    LuaParser parser(&tokens);
    if (parser.getNumberOfSyntaxErrors()) {
        throw std::runtime_error("Errors encountered while processing file " + file + "\n");
    }

    antlr4::tree::ParseTree* tree = parser.chunk();
    std::cout << tree->toStringTree(&parser, true) << std::endl;

    try {
        Types::Value::init();
        _interpreter.launch(tree);
        _interpreter.visit(tree);
        std::cout << "OK" << std::endl;
    } catch (std::exception& e) {
        std::ostringstream stream;
        stream << "Caught exception while processing file: " << file <<
                  std::endl << "What: " << e.what() << std::endl;
        throw std::runtime_error(stream.str());
    }
}
