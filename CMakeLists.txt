cmake_minimum_required(VERSION 3.23)

project(LUMIERE VERSION 0.1.3 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LUMIERE_BUILD_SHARED  "Build Lumière as a shared library (OFF builds static)." OFF)
option(LUMIERE_ENABLE_WARNINGS "Enable strict compiler warnings." ON)
set(LUMIERE_ENABLE_LUMINET_DEFAULT ON)
option(LUMIERE_ENABLE_LUMINET "Build LumiNet networking stdlib." ${LUMIERE_ENABLE_LUMINET_DEFAULT})

set(LUMIERE_LIBRARY_SOURCES
    src/analysis/analysis.cpp
    src/diagnostics/diagnostic.cpp
    src/lexer/lexer.cpp
    src/lexer/token.cpp
    src/lexer/scanner.cpp
    src/lexer/tokenizer.cpp
    src/parser/parser.cpp
    src/parser/utf8.cpp
    src/interpreter/stdlib_helpers.cpp
    src/interpreter/stdlib/chemin.cpp
    src/interpreter/stdlib/fichier.cpp
    src/interpreter/stdlib/texte.cpp
    src/interpreter/stdlib/maths.cpp
    src/interpreter/stdlib/temps.cpp
    src/interpreter/stdlib/aleatoire.cpp
    src/interpreter/stdlib/lumitest.cpp
    src/interpreter/stdlib/luminet.cpp
    src/interpreter/tree_walker/tree_walker.cpp
    src/interpreter/tree_walker/tree_walker_core.cpp
    src/interpreter/tree_walker/tree_walker_expr.cpp
    src/interpreter/tree_walker/tree_walker_modules.cpp
    src/interpreter/tree_walker/tree_walker_objects.cpp
    src/interpreter/tree_walker/tree_walker_runtime.cpp
    src/interpreter/tree_walker/tree_walker_sequences.cpp
    src/interpreter/tree_walker/tree_walker_stmt.cpp
    src/interpreter/tree_walker/tree_walker_types.cpp
    src/interpreter/runtime/value.cpp
    src/interpreter/vm/lir.cpp
    src/interpreter/vm/ast_to_lir.cpp
    src/interpreter/vm/bytecode.cpp
    src/interpreter/vm/disassembler.cpp
    src/interpreter/vm/compiler.cpp
    src/interpreter/vm/lir_to_bytecode.cpp
    src/interpreter/vm/native_globals.cpp
    src/interpreter/vm/vm.cpp
)

if(LUMIERE_ENABLE_LUMINET)
    list(APPEND LUMIERE_LIBRARY_SOURCES
        src/interpreter/stdlib/luminet/foundation.cpp
        src/interpreter/stdlib/luminet/protocol.cpp
        src/interpreter/stdlib/luminet/http_values.cpp
        src/interpreter/stdlib/luminet/canal_runtime.cpp
        src/interpreter/stdlib/luminet/tcp_runtime.cpp
        src/interpreter/stdlib/luminet/http_server.cpp
        src/interpreter/stdlib/luminet/udp_runtime.cpp
        src/interpreter/stdlib/luminet/shared.cpp
        src/interpreter/stdlib/luminet/address_dns.cpp
        src/interpreter/stdlib/luminet/tcp_udp.cpp
        src/interpreter/stdlib/luminet/http_canal.cpp
    )
    if(WIN32)
        list(APPEND LUMIERE_LIBRARY_SOURCES
            src/interpreter/stdlib/luminet/luminet_platform_windows.cpp
        )
    else()
        list(APPEND LUMIERE_LIBRARY_SOURCES
            src/interpreter/stdlib/luminet/luminet_platform_posix.cpp
        )
    endif()
endif()

set(LUMIERE_LIBRARY_TYPE STATIC)
if(LUMIERE_BUILD_SHARED)
    set(LUMIERE_LIBRARY_TYPE SHARED)
endif()

set(LUMIERE_PLATFORM_LIBRARIES)
if(WIN32)
    list(APPEND LUMIERE_PLATFORM_LIBRARIES ws2_32)
endif()

# -----------------------------
# Library target
# -----------------------------

add_library(lumiere ${LUMIERE_LIBRARY_TYPE} ${LUMIERE_LIBRARY_SOURCES})

add_library(LUMIERE::lumiere ALIAS lumiere)

set_target_properties(lumiere PROPERTIES
    OUTPUT_NAME lumiere
    VERSION     ${PROJECT_VERSION}
    SOVERSION   ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(lumiere
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/lexer
        ${PROJECT_SOURCE_DIR}/src/parser
        ${PROJECT_SOURCE_DIR}/src/interpreter/tree_walker
        ${PROJECT_SOURCE_DIR}/src/interpreter
)

if(LUMIERE_ENABLE_WARNINGS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(lumiere PRIVATE -Wall -Wextra -Wpedantic -Wno-deprecated)
    elseif(MSVC)
        target_compile_options(lumiere PRIVATE /W4)
    endif()
endif()

target_compile_definitions(lumiere PUBLIC LUMIERE_ENABLE_LUMINET=$<BOOL:${LUMIERE_ENABLE_LUMINET}>)

# -----------------------------
# CLI executable
# -----------------------------

add_executable(lumiere_cli src/lumiere.cpp)

add_executable(LUMIERE::lumiere_cli ALIAS lumiere_cli)

set_target_properties(lumiere_cli PROPERTIES OUTPUT_NAME lumiere)

target_link_libraries(lumiere PRIVATE ${LUMIERE_PLATFORM_LIBRARIES})
target_link_libraries(lumiere_cli PRIVATE LUMIERE::lumiere)
target_compile_definitions(lumiere_cli PRIVATE LUMIERE_VERSION="${PROJECT_VERSION}")

# -----------------------------
# Install + package export
# -----------------------------

install(TARGETS lumiere lumiere_cli
    EXPORT LUMIERETargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/lumiere DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT LUMIERETargets
    FILE      LUMIERETargets.cmake
    NAMESPACE LUMIERE::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LUMIERE
)

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/LUMIEREConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LUMIEREConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LUMIERE
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/LUMIEREConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LUMIEREConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LUMIEREConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LUMIERE
)

set(CPACK_PACKAGE_NAME "lumiere")
set(CPACK_PACKAGE_VENDOR "Lumiere")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Lumiere language runtime and tooling")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_CONTACT "SY Technologies")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "SY Technologies")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)

# -----------------------------
# Tests
# -----------------------------

include(CTest)

if(BUILD_TESTING)
    include(FetchContent)
    set(LUMIERE_TEST_SOURCES
        tests/test_lexer.cpp
        tests/test_analysis.cpp
        tests/test_parser.cpp
        tests/test_parser_fixtures.cpp
        tests/test_interpreter_fixtures.cpp
        tests/test_cli_integration.cpp
        tests/test_vm_lir.cpp
        tests/test_windows_header_compatibility.cpp
    )

    set(LUMIERE_HAS_TEST_CASES FALSE)
    foreach(test_source IN LISTS LUMIERE_TEST_SOURCES)
        if(EXISTS "${PROJECT_SOURCE_DIR}/${test_source}")
            file(SIZE "${PROJECT_SOURCE_DIR}/${test_source}" test_source_size)
            if(test_source_size GREATER 0)
                set(LUMIERE_HAS_TEST_CASES TRUE)
                break()
            endif()
        endif()
    endforeach()

    if(LUMIERE_HAS_TEST_CASES)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.16.0.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)

        add_executable(lumiere_unit_tests
            ${LUMIERE_TEST_SOURCES}
        )

        add_dependencies(lumiere_unit_tests lumiere_cli)

        if(EXISTS "${PROJECT_SOURCE_DIR}/tests/fixtures/interpreter")
            target_compile_definitions(lumiere_unit_tests
                PRIVATE
                    LUMIERE_INTERPRETER_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/interpreter"
            )
        endif()

        if(EXISTS "${PROJECT_SOURCE_DIR}/tests/fixtures/parser")
            target_compile_definitions(lumiere_unit_tests
                PRIVATE
                    LUMIERE_PARSER_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/parser"
            )
        endif()

        target_compile_definitions(lumiere_unit_tests
            PRIVATE
                LUMIERE_CLI_PATH="$<TARGET_FILE:lumiere_cli>"
                LUMIERE_VERSION="${PROJECT_VERSION}"
        )

        target_link_libraries(lumiere_unit_tests
            PRIVATE
                LUMIERE::lumiere
                GTest::gtest_main
                ${LUMIERE_PLATFORM_LIBRARIES}
        )

        include(GoogleTest)
        gtest_discover_tests(lumiere_unit_tests)
    else()
        message(STATUS "Skipping lumiere_unit_tests: test sources are empty.")
    endif()
endif()

# -----------------------------
# Status
# -----------------------------

message(STATUS "Configured Lumière ${PROJECT_VERSION}")
message(STATUS "Build type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "Library type:  ${LUMIERE_LIBRARY_TYPE}")
