cmake_minimum_required(VERSION 3.10)
project(CMinusInterpretator C)

set(CMAKE_C_STANDARD 23)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
endif()

add_executable(CMinusInterpretator main.c
        Lexer/lexer.h
        Lexer/lexer.c
        Parser/parser.h
        Parser/AST.h
        Parser/AST.c
        Parser/parser.c
        Eval/eval.h
        Eval/eval.c)
