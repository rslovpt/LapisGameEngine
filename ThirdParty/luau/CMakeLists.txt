# Vendored Luau (VM + compiler only). See VERSION.txt / LICENSE.txt.
cmake_minimum_required(VERSION 3.16)

file(GLOB LUAU_COMMON_SRC   CONFIGURE_DEPENDS Common/src/*.cpp)
file(GLOB LUAU_AST_SRC      CONFIGURE_DEPENDS Ast/src/*.cpp)
file(GLOB LUAU_BYTECODE_SRC CONFIGURE_DEPENDS Bytecode/src/*.cpp)
file(GLOB LUAU_COMPILER_SRC CONFIGURE_DEPENDS Compiler/src/*.cpp)
file(GLOB LUAU_VM_SRC       CONFIGURE_DEPENDS VM/src/*.cpp)

add_library(LuauLib STATIC
    ${LUAU_COMMON_SRC} ${LUAU_AST_SRC} ${LUAU_BYTECODE_SRC} ${LUAU_COMPILER_SRC} ${LUAU_VM_SRC})

target_compile_features(LuauLib PUBLIC cxx_std_17)
target_include_directories(LuauLib PUBLIC
    Common/include Ast/include Bytecode/include Compiler/include VM/include)
target_include_directories(LuauLib PRIVATE VM/src Bytecode/src)

if(MSVC)
    target_compile_options(LuauLib PRIVATE /W0 /MP /utf-8)
    target_compile_definitions(LuauLib PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(LuauLib PRIVATE -w)
endif()
