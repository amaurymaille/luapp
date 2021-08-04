cmake_minimum_required(VERSION 3.5)

project(template_erasure LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set (CMAKE_CXX_FLAGS "-Wall -Wextra")

# find_package(antlr4-runtime REQUIRED)
find_package(Boost REQUIRED)

include_directories("${Boost_INCLUDE_DIR}" "/usr/include/antlr4-runtime")
link_directories("/usr/lib/x86_64-linux-gnu")

add_executable(function_embedding main.cpp)
add_executable(parser parser.cpp LuaLexer.cpp LuaParser.cpp LuaVisitor.cpp)
target_link_libraries (parser antlr4-runtime)
