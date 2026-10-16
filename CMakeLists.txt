cmake_minimum_required(VERSION 3.14...3.28)
project(kiss LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# version.h: the git commit of the build, rewritten only when it changes
set(VERSION_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/version.h")
add_custom_target(version ALL
    COMMAND "${CMAKE_COMMAND}" "-DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}" "-DOUTPUT=${VERSION_OUTPUT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/version.cmake"
    BYPRODUCTS "${VERSION_OUTPUT}"
    COMMENT "Checking git version"
)

find_package(Threads REQUIRED)

# UTF-8 source and execution charsets, so MSVC reads the sources as they are
if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# cpp-httplib: the HTTP server, and the HTTPS client of the MCP servers it
# relays
add_subdirectory(vendor/cpp-httplib)

# stb: the images view decodes, resizes and encodes
add_subdirectory(vendor/stb)

# yyjson (MIT, fast JSON parser and writer): every JSON the server reads or
# writes
add_library(yyjson STATIC vendor/yyjson/yyjson.c)
target_include_directories(yyjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/yyjson)
if(MSVC)
    target_compile_options(yyjson PRIVATE /W0)
else()
    target_compile_options(yyjson PRIVATE -w)
endif()

# the files the binary embeds as C arrays, each from its one source: the page,
# gzipped by the page build and committed so the server builds without npm,
# the client.conf the server serves to the page, its factory configuration,
# and what a model reads of it
set(EMBEDDED)
foreach(input tools/public/index.html.gz server/client.conf server/server.conf server/prompts.json)
    get_filename_component(name "${input}" NAME)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/${name}.hpp")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/${input}" "-DOUTPUT=${output}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/xxd.cmake"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${input}"
        COMMENT "Embedding ${input}"
    )
    list(APPEND EMBEDDED "${output}")
endforeach()

# server: the binary. The commands, the modules and the tools register
# themselves by existing, so every source is compiled into the executable,
# never into a library the linker could leave out.
file(GLOB SERVER_SOURCES CONFIGURE_DEPENDS server/*.cpp server/*/*.cpp)
list(FILTER SERVER_SOURCES EXCLUDE REGEX "/server/tests/")
add_executable(server ${SERVER_SOURCES} ${EMBEDDED})
target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/server ${CMAKE_CURRENT_BINARY_DIR})
if(MSVC)
    target_compile_options(server PRIVATE /W4 /wd4100 /wd4505)
else()
    target_compile_options(server PRIVATE -Wall -Wextra -Wshadow -Wconversion
                           -Wno-unused-parameter -Wno-unused-function -Wno-sign-conversion)
endif()
target_link_libraries(server PRIVATE httplib yyjson stb Threads::Threads)
if(WIN32)
    target_link_libraries(server PRIVATE ws2_32)
endif()
add_dependencies(server version)

# seal_test: the laws of the seal of the keychain, run once built, a broken law
# failing the build
add_executable(seal_test server/tests/seal.cpp server/engine/seal.cpp server/engine/text.cpp)
target_include_directories(seal_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/server)
target_link_libraries(seal_test PRIVATE yyjson)
add_custom_command(TARGET seal_test POST_BUILD COMMAND seal_test)

# files_test: the laws of the files the tools on files reach, run once built,
# a broken law failing the build
add_executable(files_test server/tests/files.cpp server/lib/file.cpp)
target_include_directories(files_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/server)
add_custom_command(TARGET files_test POST_BUILD COMMAND files_test)
