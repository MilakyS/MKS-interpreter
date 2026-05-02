cmake_minimum_required(VERSION 3.10)
project(MonkeyKernelSyntax VERSION 0.2.0 LANGUAGES C)

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
        Eval/eval_index.h
        Eval/eval_index.c

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
        Runtime/module_toml.c
        Runtime/module_toml.h
        Runtime/errors.c
        Runtime/errors.h
        Runtime/context.c
        Runtime/context.h
        Runtime/runner.c
        Runtime/runner.h
        Runtime/profiler.c
        Runtime/profiler.h
        Runtime/pointers.c
        Runtime/pointers.h
        VM/vm_compile.c
        VM/vm_compile_scope.c
        VM/vm_compile_scope.h
        VM/vm_peephole.c
        VM/vm_peephole.h
        VM/vm_exec.c
        VM/vm_dump.c
        VM/vm.h

        std/watch.c
        std/watch.h
        std/json.c
        std/json.h
        std/path.c
        std/path.h
        std/process.c
        std/process.h
        std/io.c
        std/io.h
        std/registry.c
        std/math.c
        std/time.c
        std/random.c
        std/fs.c
        std/fs.h
        std/tty.c
        std/tty.h

        Utils/file.c
        Utils/file.h

        GC/gc.c
        GC/gc.h
        VM/vm_chunk.c
        VM/vm_chunk.h
        VM/vm_compile_internal.h
        VM/vm_compile_can.c
        VM/vm_compile_can.h
)

add_library(mks_runtime STATIC
        ${MKS_RUNTIME_SOURCES}
)

add_executable(mks
        main.c
)

target_link_libraries(mks PRIVATE mks_runtime)
target_link_options(mks PRIVATE -rdynamic)

add_executable(mks_unit_tests
        tests/unit/test_runtime.c
)

target_link_libraries(mks_unit_tests PRIVATE mks_runtime)

set(MKS_TARGETS
        mks_runtime
        mks
        mks_unit_tests
)

if(NOT MSVC)
    foreach(target ${MKS_TARGETS})
        target_compile_options(${target} PRIVATE -Wall -Wextra -pedantic)
    endforeach()
endif()

if(ENABLE_ASAN)
    if(MSVC)
        foreach(target ${MKS_TARGETS})
            target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
            target_link_options(${target} PRIVATE /fsanitize=address)
        endforeach()
    else()
        foreach(target ${MKS_TARGETS})
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
    target_link_libraries(mks_runtime PUBLIC m dl)
endif()

include(GNUInstallDirs)

set(MKS_INSTALL_STDLIB_DIR "${CMAKE_INSTALL_DATADIR}/mks")
set(MKS_INSTALL_STDLIB_FULL_DIR "${CMAKE_INSTALL_FULL_DATADIR}/mks")

foreach(target ${MKS_TARGETS})
    target_compile_definitions(${target} PRIVATE
            MKS_VERSION="${PROJECT_VERSION}"
            MKS_INSTALL_BINDIR_REL="${CMAKE_INSTALL_BINDIR}"
            MKS_INSTALL_STDLIB_DIR_REL="${MKS_INSTALL_STDLIB_DIR}"
            MKS_INSTALL_STDLIB_DIR_ABS="${MKS_INSTALL_STDLIB_FULL_DIR}"
    )
endforeach()

enable_testing()
add_test(NAME mks_unit_tests COMMAND mks_unit_tests)

install(TARGETS mks
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(EXISTS "${CMAKE_SOURCE_DIR}/std")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/std/"
            DESTINATION "${MKS_INSTALL_STDLIB_DIR}/std"
            FILES_MATCHING PATTERN "*.mks")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/GGui")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/GGui/"
            DESTINATION "${MKS_INSTALL_STDLIB_DIR}/GGui"
            FILES_MATCHING
                PATTERN "*.mks"
                PATTERN "package.pkg"
                PATTERN "README.md")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
    install(FILES "${CMAKE_SOURCE_DIR}/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/README.md")
    install(FILES "${CMAKE_SOURCE_DIR}/README.md"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/${PROJECT_NAME}")
endif()
