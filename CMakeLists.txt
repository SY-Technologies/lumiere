cmake_minimum_required(VERSION 3.23)

project(LUMIERE VERSION 0.1.7 LANGUAGES CXX)

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
set(LUMIERE_ENABLE_LUMIDESSIN_DEFAULT ON)
option(LUMIERE_ENABLE_LUMIDESSIN "Build the LumiDessin 2D drawing stdlib." ${LUMIERE_ENABLE_LUMIDESSIN_DEFAULT})
set(LUMIERE_ENABLE_LUMIDESSIN_WINDOW_DEFAULT ON)
option(LUMIERE_ENABLE_LUMIDESSIN_WINDOW "Build LumiDessin's visible-window backend (requires LUMIERE_ENABLE_LUMIDESSIN)." ${LUMIERE_ENABLE_LUMIDESSIN_WINDOW_DEFAULT})

set(LUMIERE_LIBRARY_SOURCES
    src/analysis/analysis.cpp
    src/analysis/inspection.cpp
    src/analysis/native_signatures.cpp
    src/analysis/semantic_analysis.cpp
    src/analysis/semantic_type.cpp
    src/diagnostics/diagnostic.cpp
    src/diagnostics/runtime_stats.cpp
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
    src/interpreter/stdlib/collections.cpp
    src/interpreter/stdlib/json.cpp
    src/interpreter/stdlib/regex.cpp
    src/interpreter/stdlib/lumidessin.cpp
    src/interpreter/stdlib/lumidessin/values.cpp
    src/interpreter/stdlib/lumidessin/raster.cpp
    src/interpreter/stdlib/lumidessin/text.cpp
    src/interpreter/stdlib/lumidessin/image.cpp
    src/interpreter/stdlib/lumidessin/crayon.cpp
    src/interpreter/stdlib/lumidessin/window.cpp
    src/interpreter/stdlib/lumidessin/input.cpp
    src/interpreter/tree_walker/tree_walker.cpp
    src/interpreter/tree_walker/tree_walker_core.cpp
    src/interpreter/tree_walker/tree_walker_expr.cpp
    src/interpreter/tree_walker/tree_walker_modules.cpp
    src/interpreter/tree_walker/tree_walker_objects.cpp
    src/interpreter/tree_walker/tree_walker_runtime.cpp
    src/interpreter/tree_walker/tree_walker_stmt.cpp
    src/interpreter/tree_walker/tree_walker_types.cpp
    src/interpreter/runtime/conversions.cpp
    src/interpreter/runtime/members.cpp
    src/interpreter/runtime/value.cpp
    src/interpreter/runtime/cycles.cpp
    src/interpreter/vm/lir.cpp
    src/interpreter/vm/ast_to_lir.cpp
    src/interpreter/vm/bytecode.cpp
    src/interpreter/vm/disassembler.cpp
    src/interpreter/vm/verifier.cpp
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
# Embedded stdlib documentation
# -----------------------------

include(cmake/EmbedStdlibDocs.cmake)

# -----------------------------
# LumiDessin: bundled font (docs/stdlib-lumidessin.md, "Platform backend")
# -----------------------------
#
# stb_truetype.h is vendored the same way googletest is (FetchContent,
# pinned commit). The Inter font ships from upstream as a zip of many
# weights; FetchContent_MakeAvailable populates the whole archive (no
# CMakeLists.txt inside it, so this just downloads and extracts -- there is
# no subproject to configure), and only the one static Regular .ttf inside
# it is ever read, by the embed step below.
if(LUMIERE_ENABLE_LUMIDESSIN)
    include(FetchContent)

    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(stb)

    FetchContent_Declare(
        lumidessin_inter_font
        URL https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(lumidessin_inter_font)

    set(LUMIDESSIN_FONT_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${LUMIDESSIN_FONT_GEN_DIR}")
    set(LUMIDESSIN_FONT_SOURCE "${lumidessin_inter_font_SOURCE_DIR}/extras/ttf/Inter-Regular.ttf")
    set(LUMIDESSIN_FONT_GENERATED "${LUMIDESSIN_FONT_GEN_DIR}/lumidessin_font.generated.hpp")

    execute_process(
        COMMAND python3 "${PROJECT_SOURCE_DIR}/scripts/embed_binary.py"
                "${LUMIDESSIN_FONT_SOURCE}" "${LUMIDESSIN_FONT_GENERATED}" "INTER_REGULAR_TTF"
        RESULT_VARIABLE LUMIDESSIN_FONT_EMBED_RESULT
    )
    if(NOT LUMIDESSIN_FONT_EMBED_RESULT EQUAL 0)
        message(FATAL_ERROR "LumiDessin: failed to embed the bundled font (${LUMIDESSIN_FONT_SOURCE})")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${LUMIDESSIN_FONT_SOURCE}")
endif()

# -----------------------------
# LumiDessin: SDL3 (window creation, input, presentation -- stage 7/8).
# Vendored the same way stb and googletest are (FetchContent, pinned tag).
# Building only the static library keeps the CLI a single self-contained
# binary, matching LUMIERE_LIBRARY_TYPE's own default.
# -----------------------------
if(LUMIERE_ENABLE_LUMIDESSIN AND LUMIERE_ENABLE_LUMIDESSIN_WINDOW)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.16
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL3)
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
        ${STDLIB_DOC_GEN_DIR}
)

if(LUMIERE_ENABLE_LUMIDESSIN)
    target_include_directories(lumiere
        PRIVATE
            ${stb_SOURCE_DIR}
            ${LUMIDESSIN_FONT_GEN_DIR}
    )
endif()

if(LUMIERE_ENABLE_WARNINGS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(lumiere PRIVATE -Wall -Wextra -Wpedantic -Wno-deprecated)
    elseif(MSVC)
        target_compile_options(lumiere PRIVATE /W4)
    endif()
endif()

if(MSVC)
    # Lumière's identifiers, string literals, and error messages are French
    # (docs/README.md), so the compiler must be told this source is UTF-8 --
    # otherwise MSVC falls back to the system codepage, which both misparses
    # multi-byte characters in identifiers/literals (surfacing as C2015 or
    # C3872 on accented text) and silently corrupts accented content baked
    # into string literals that happen to parse anyway. /utf-8 sets both
    # /source-charset:utf-8 and /execution-charset:utf-8.
    target_compile_options(lumiere PRIVATE /utf-8)
endif()

target_compile_definitions(lumiere PUBLIC LUMIERE_ENABLE_LUMINET=$<BOOL:${LUMIERE_ENABLE_LUMINET}>)
target_compile_definitions(lumiere PUBLIC LUMIERE_ENABLE_LUMIDESSIN=$<BOOL:${LUMIERE_ENABLE_LUMIDESSIN}>)
target_compile_definitions(lumiere PUBLIC LUMIERE_ENABLE_LUMIDESSIN_WINDOW=$<BOOL:${LUMIERE_ENABLE_LUMIDESSIN_WINDOW}>)

if(LUMIERE_ENABLE_LUMIDESSIN AND LUMIERE_ENABLE_LUMIDESSIN_WINDOW)
    # BUILD_INTERFACE-guarded: SDL3 is vendored (FetchContent), not
    # installed, so it must never appear in lumiere's exported usage
    # requirements -- only in this build's own link line. A downstream
    # consumer of an installed Lumière package gets the window backend
    # already linked into the static archive it receives; it does not need
    # to find SDL3 itself.
    target_link_libraries(lumiere PRIVATE $<BUILD_INTERFACE:SDL3::SDL3-static>)
endif()

# -----------------------------
# CLI executable
# -----------------------------

add_executable(lumiere_cli src/lumiere.cpp)

add_executable(LUMIERE::lumiere_cli ALIAS lumiere_cli)

if(MSVC)
    target_compile_options(lumiere_cli PRIVATE /utf-8)
endif()

set_target_properties(lumiere_cli PROPERTIES OUTPUT_NAME lumiere)

target_link_libraries(lumiere PRIVATE ${LUMIERE_PLATFORM_LIBRARIES})
target_link_libraries(lumiere_cli PRIVATE LUMIERE::lumiere)
target_compile_definitions(lumiere_cli PRIVATE LUMIERE_VERSION="${PROJECT_VERSION}")

# -----------------------------
# Bundled time zone database (third_party/zoneinfo/README.md)
# -----------------------------
#
# Temps resolves named zones (Fuseau("Europe/Paris"), ...) against a
# compiled IANA tzdata tree it finds next to the running executable --
# Windows has no system tzdata to fall back to the way Linux/macOS do.
# Copied rather than installed-only so a build-tree run (tests, `cmake
# --build . && ./lumiere ...`) also finds it, not just an installed one.
set(LUMIERE_ZONEINFO_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/zoneinfo")
add_custom_command(TARGET lumiere_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${LUMIERE_ZONEINFO_SOURCE_DIR}" "$<TARGET_FILE_DIR:lumiere_cli>/zoneinfo"
    COMMENT "Copying bundled zoneinfo next to lumiere_cli"
)

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

install(DIRECTORY "${LUMIERE_ZONEINFO_SOURCE_DIR}/" DESTINATION "${CMAKE_INSTALL_BINDIR}/zoneinfo")

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
        tests/test_numeric.cpp
        tests/test_collection_constraints.cpp
        tests/test_value_hash.cpp
        tests/test_cycles.cpp
        tests/test_analysis.cpp
        tests/test_semantic_index.cpp
        tests/test_parser.cpp
        tests/test_parser_fixtures.cpp
        tests/test_interpreter_fixtures.cpp
        tests/test_lumidessin_window.cpp
        tests/test_cli_integration.cpp
        tests/test_vm_lir.cpp
        tests/test_vm_verifier.cpp
        tests/test_vm_verifier_fuzz.cpp
        tests/test_no_legacy_exceptions.cpp
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

        # Interpreter fixture tests exercise Temps in-process (not just
        # through the CLI), so the unit test binary needs its own copy too.
        add_custom_command(TARGET lumiere_unit_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${LUMIERE_ZONEINFO_SOURCE_DIR}" "$<TARGET_FILE_DIR:lumiere_unit_tests>/zoneinfo"
            COMMENT "Copying bundled zoneinfo next to lumiere_unit_tests"
        )

        if(MSVC)
            target_compile_options(lumiere_unit_tests PRIVATE /utf-8)
        endif()

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
                LUMIERE_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
                LUMIERE_VERSION="${PROJECT_VERSION}"
        )

        target_include_directories(lumiere_unit_tests
            PRIVATE
                ${PROJECT_SOURCE_DIR}/src/interpreter/stdlib
        )

        target_link_libraries(lumiere_unit_tests
            PRIVATE
                LUMIERE::lumiere
                GTest::gtest_main
                ${LUMIERE_PLATFORM_LIBRARIES}
        )

        if(LUMIERE_ENABLE_LUMIDESSIN AND LUMIERE_ENABLE_LUMIDESSIN_WINDOW)
            # The bounded visible-window smoke test injects a real SDL close
            # event (docs/stdlib-lumidessin.md, "Platform tests") from the
            # test process itself, which needs SDL3's own header and link
            # symbols -- lumiere's own SDL3 dependency is BUILD_INTERFACE/
            # PRIVATE and does not reach this target otherwise.
            target_link_libraries(lumiere_unit_tests PRIVATE $<BUILD_INTERFACE:SDL3::SDL3-static>)
        endif()

        include(GoogleTest)
        # A handful of CLI tests spawn a dozen subprocesses each. Under a
        # sanitizer every one of those runs several times slower, which is what
        # pushes them past a timeout sized for an ordinary build rather than any
        # change in what they do.
        set(LUMIERE_TEST_TIMEOUT 30)
        if(CMAKE_CXX_FLAGS MATCHES "-fsanitize")
            set(LUMIERE_TEST_TIMEOUT 180)
        endif()
        gtest_discover_tests(lumiere_unit_tests PROPERTIES TIMEOUT ${LUMIERE_TEST_TIMEOUT})

        # The conformance corpus runs the real CLI under both engines and checks
        # that they agree, which no in-process test can do. It is a ctest entry
        # so a divergence fails the build instead of waiting to be noticed.
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(NAME conformance
                     COMMAND ${Python3_EXECUTABLE}
                             ${PROJECT_SOURCE_DIR}/scripts/conformance
                             $<TARGET_FILE:lumiere_cli>
                             --out ${CMAKE_BINARY_DIR}/conformance)
            set_tests_properties(conformance PROPERTIES TIMEOUT ${LUMIERE_TEST_TIMEOUT})
        else()
            message(STATUS "Skipping the conformance corpus: no Python 3 interpreter found.")
        endif()
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
