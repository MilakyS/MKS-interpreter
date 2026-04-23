cmake_minimum_required(VERSION 3.10)
project(MonkeyKernelSyntax C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
endif()

set(MKS_RUNTIME_SOURCES
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
        Runtime/extension.c
        Runtime/extension.h

        Runtime/module.c
        Runtime/module.h
        Runtime/errors.c
        Runtime/errors.h
        Runtime/context.c
        Runtime/context.h
        Runtime/runner.c
        Runtime/runner.h
        Runtime/profiler.c
        Runtime/profiler.h
        std/watch.c
        std/watch.h
        Utils/file.c
        Utils/file.h

        GC/gc.c
        GC/gc.h

        std/registry.c
        std/math.c
        std/time.c
        std/random.c
        std/fs.c
        std/fs.h
        std/tty.c
        std/tty.h
)

add_executable(mks
        main.c
        ${MKS_RUNTIME_SOURCES}
)

add_executable(mks_unit_tests
        tests/unit/test_runtime.c
        ${MKS_RUNTIME_SOURCES}
)

if(NOT MSVC)
    foreach(target mks mks_unit_tests)
        target_compile_options(${target} PRIVATE -Wall -Wextra -pedantic)
    endforeach()
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        foreach(target mks mks_unit_tests)
            target_compile_options(${target} PRIVATE -O3 -DNDEBUG)
        endforeach()
    else()
        foreach(target mks mks_unit_tests)
            target_compile_options(${target} PRIVATE -O2)
        endforeach()
    endif()
endif()

if(ENABLE_ASAN)
    if(MSVC)
        foreach(target mks mks_unit_tests)
            target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
            target_link_options(${target} PRIVATE /fsanitize=address)
        endforeach()
    else()
        foreach(target mks mks_unit_tests)
            target_compile_options(${target} PRIVATE
                    -fsanitize=address,undefined
                    -g
                    -O1
                    -fno-omit-frame-pointer
            )
            target_link_options(${target} PRIVATE
                    -fsanitize=address,undefined
            )
        endforeach()
    endif()
endif()

if(NOT MSVC)
    target_link_libraries(mks PRIVATE m)
    target_link_libraries(mks_unit_tests PRIVATE m)
endif()

enable_testing()
add_test(NAME mks_unit_tests COMMAND mks_unit_tests)

include(GNUInstallDirs)

set(MKS_INSTALL_STDLIB_DIR "${CMAKE_INSTALL_DATADIR}/mks")
set(MKS_INSTALL_STDLIB_FULL_DIR "${CMAKE_INSTALL_FULL_DATADIR}/mks")

foreach(target mks mks_unit_tests)
    target_compile_definitions(${target} PRIVATE
            MKS_INSTALL_BINDIR_REL="${CMAKE_INSTALL_BINDIR}"
            MKS_INSTALL_STDLIB_DIR_REL="${MKS_INSTALL_STDLIB_DIR}"
            MKS_INSTALL_STDLIB_DIR_ABS="${MKS_INSTALL_STDLIB_FULL_DIR}"
    )
endforeach()

install(TARGETS mks
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(EXISTS "${CMAKE_SOURCE_DIR}/std")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/std/"
            DESTINATION "${MKS_INSTALL_STDLIB_DIR}/std"
            FILES_MATCHING PATTERN "*.mks")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
    install(FILES "${CMAKE_SOURCE_DIR}/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/README.md")
    install(FILES "${CMAKE_SOURCE_DIR}/README.md"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/${PROJECT_NAME}")
endif()
