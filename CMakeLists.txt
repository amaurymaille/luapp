cmake_minimum_required(VERSION 3.5)

project(template_erasure LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set (CMAKE_CXX_FLAGS "-Wall -Wextra")

# find_package(antlr4-runtime REQUIRED)
find_package(Boost REQUIRED COMPONENTS program_options)

include_directories("${Boost_INCLUDE_DIR}" "/usr/include/antlr4-runtime")
link_directories("/usr/lib/x86_64-linux-gnu")

add_library (lua_core STATIC interpreter.cpp LuaLexer.cpp LuaParser.cpp LuaVisitor.cpp
    syntactic_analyzer.cpp types.cpp exceptions.cpp environment.cpp
    function_abstraction.cpp)

# add_executable(function_embedding main.cpp)
# add_executable(template_erasure main.cpp)
add_executable(interpreter_tester test_interpreter.cpp)
add_executable(playfield playfield.cpp)
target_link_libraries (interpreter_tester lua_core antlr4-runtime "${Boost_PROGRAM_OPTIONS_LIBRARY}")
target_link_libraries(playfield lua_core antlr4-runtime "${Boost_PROGRAM_OPTIONS_LIBRARY}")
