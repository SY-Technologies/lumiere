cmake_minimum_required(VERSION 3.23)

project(LUMIERE VERSION 0.1.0 LANGUAGES CXX)

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

set(LUMIERE_LIBRARY_TYPE STATIC)
if(LUMIERE_BUILD_SHARED)
    set(LUMIERE_LIBRARY_TYPE SHARED)
endif()

# -----------------------------
# Library target
# -----------------------------

add_library(lumiere ${LUMIERE_LIBRARY_TYPE}
    src/lexer.cpp
    src/token.cpp
    src/parser.cpp
    src/interpreter.cpp
)

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
        ${PROJECT_SOURCE_DIR}/src
)

if(LUMIERE_ENABLE_WARNINGS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(lumiere PRIVATE -Wall -Wextra -Wpedantic -Wno-deprecated)
    elseif(MSVC)
        target_compile_options(lumiere PRIVATE /W4)
    endif()
endif()

# -----------------------------
# CLI executable
# -----------------------------

add_executable(lumiere_cli src/lumiere.cpp)

add_executable(LUMIERE::lumiere_cli ALIAS lumiere_cli)

set_target_properties(lumiere_cli PROPERTIES OUTPUT_NAME lumiere)

target_link_libraries(lumiere_cli PRIVATE LUMIERE::lumiere)

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

# -----------------------------
# Tests
# -----------------------------

include(CTest)

if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.16.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(lumiere_unit_tests
        tests/test_lexer.cpp
        tests/test_parser.cpp
        tests/test_parser_fixtures.cpp
        tests/test_interpreter_fixtures.cpp
    )

    target_compile_definitions(lumiere_unit_tests
        PRIVATE
            LUMIERE_INTERPRETER_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/interpreter"
            LUMIERE_PARSER_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/parser"
    )

    target_link_libraries(lumiere_unit_tests
        PRIVATE
            LUMIERE::lumiere
            GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(lumiere_unit_tests)
endif()

# -----------------------------
# Status
# -----------------------------

message(STATUS "Configured Lumière ${PROJECT_VERSION}")
message(STATUS "Build type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "Library type:  ${LUMIERE_LIBRARY_TYPE}")