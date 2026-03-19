cmake_minimum_required(VERSION 3.10)
project(MonkeyKernelSyntax C)

set(CMAKE_C_STANDARD 23)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
endif()

# Вот тут меняем название выходного файла на mks_run
add_executable(mks_run main.c
        Lexer/lexer.h
        Lexer/lexer.c
        Parser/parser.h
        Parser/AST.h
        Parser/AST.c
        Parser/parser.c
        Eval/eval.h
        Eval/eval.c)
