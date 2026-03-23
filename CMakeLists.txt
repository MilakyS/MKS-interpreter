cmake_minimum_required(VERSION 3.10)
project(MonkeyKernelSyntax C)

set(CMAKE_C_STANDARD 23)

if(NOT MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
endif()


if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
endif()


add_executable(mks_run
        main.c

        Lexer/lexer.h
        Lexer/lexer.c

        Parser/parser.h
        Parser/AST.h
        Parser/AST.c

        Eval/eval.h
        Eval/eval.c

        main.h

        Runtime/value.h
        Runtime/value.c

        env/env.h
        env/env.c

        Runtime/output.h
        Runtime/output.c

        Runtime/operators.h
        Runtime/operators.c

        Utils/hash.h
        Utils/hash.c

        Runtime/functions.h
        Runtime/functions.c

        Runtime/indexing.c
        Runtime/indexing.h

        Runtime/methods.c
        Runtime/methods.h

        Runtime/control_flow.c
        Runtime/control_flow.h

        GC/gc.c
        GC/gc.h

        Runtime/methods_cache.h
        Parser/parser_core.c
        Parser/parser_expr.c
        Parser/parser_stmt.c
)

if(NOT MSVC)
    target_link_libraries(mks_run PRIVATE m)
endif()