cmake_minimum_required(VERSION 3.10)
project(MonkeyKernelSyntax C)

set(CMAKE_C_STANDARD 23)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

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
        Parser/parser_core.c
        Parser/parser_expr.c
        Parser/parser_stmt.c

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
        Runtime/methods_cache.h

        Runtime/control_flow.c
        Runtime/control_flow.h

        GC/gc.c
        GC/gc.h
)

# 🔥 ОПТИМИЗАЦИЯ
if(NOT MSVC)
    target_compile_options(mks_run PRIVATE -Wall -Wextra -pedantic)
endif()

# 🧠 RELEASE = -O3
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT MSVC)
    target_compile_options(mks_run PRIVATE -O3)
endif()

# 🧪 SANITIZER
if(ENABLE_ASAN)
    if(MSVC)
        target_compile_options(mks_run PRIVATE /fsanitize=address /Zi)
        target_link_options(mks_run PRIVATE /fsanitize=address)
    else()
        target_compile_options(mks_run PRIVATE
                -fsanitize=address,undefined
                -g -O1
                -fno-omit-frame-pointer
        )
        target_link_options(mks_run PRIVATE
                -fsanitize=address,undefined
        )
    endif()
endif()

if(NOT MSVC)
    target_link_libraries(mks_run PRIVATE m)
endif()