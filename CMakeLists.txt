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
# what a model reads of it and of the page; each named by its path,
# server/prompts.json as embedded/server/prompts.json.hpp holding
# server_prompts_json
set(EMBEDDED)
foreach(input tools/public/index.html.gz server/client.conf server/server.conf server/prompts.json webui/src/prompts.json)
    string(MAKE_C_IDENTIFIER "${input}" symbol)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/embedded/${input}.hpp")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/${input}" "-DOUTPUT=${output}" "-DNAME=${symbol}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/xxd.cmake"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${input}"
        COMMENT "Embedding ${input}"
    )
    list(APPEND EMBEDDED "${output}")
endforeach()

# kiss: every source of the server but its start, compiled once for the binary
# and the tests built on the whole server. The commands, the modules and the
# tools register themselves by existing, so they are objects every target
# links whole, never a library the linker could leave a file out of.
file(GLOB SERVER_SOURCES CONFIGURE_DEPENDS server/*.cpp server/*/*.cpp)
list(FILTER SERVER_SOURCES EXCLUDE REGEX "/server/(tests/|main.cpp$)")
if(MSVC)
    set(SERVER_WARNINGS /W4 /wd4100 /wd4505)
else()
    set(SERVER_WARNINGS -Wall -Wextra -Wshadow -Wconversion
        -Wno-unused-parameter -Wno-unused-function -Wno-sign-conversion)
endif()
add_library(kiss OBJECT ${SERVER_SOURCES} ${EMBEDDED})
target_include_directories(kiss PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/server ${CMAKE_CURRENT_BINARY_DIR}
                           ${CMAKE_CURRENT_BINARY_DIR}/embedded)
target_compile_options(kiss PRIVATE ${SERVER_WARNINGS})
target_link_libraries(kiss PUBLIC httplib yyjson stb Threads::Threads)
if(WIN32)
    target_link_libraries(kiss PUBLIC ws2_32)
endif()
add_dependencies(kiss version)

# server: the binary
add_executable(server server/main.cpp)
target_compile_options(server PRIVATE ${SERVER_WARNINGS})
target_link_libraries(server PRIVATE kiss)

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

# conversation_test: the laws of the conversations as files and of the library
# that holds them and tells its followers, run once built, a broken law failing the build
add_executable(conversation_test server/tests/conversation.cpp server/lib/conversation.cpp server/lib/library.cpp
               server/lib/file.cpp server/lib/log.cpp server/engine/parse.cpp server/engine/seal.cpp
               server/engine/text.cpp)
target_include_directories(conversation_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/server)
target_link_libraries(conversation_test PRIVATE yyjson)
add_custom_command(TARGET conversation_test POST_BUILD COMMAND conversation_test)

# protocol_test: the laws of the protocols, a request as it goes and an event
# as it reads, run once built, a broken law failing the build
add_executable(protocol_test server/tests/protocol.cpp server/protocols/chat.cpp server/protocols/messages.cpp
               server/protocols/responses.cpp server/lib/conversation.cpp
               server/engine/parse.cpp server/engine/seal.cpp server/engine/text.cpp)
target_include_directories(protocol_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/server)
target_compile_definitions(protocol_test PRIVATE BODIES="${CMAKE_CURRENT_SOURCE_DIR}/webui/tests/bodies.jsonl")
target_link_libraries(protocol_test PRIVATE httplib yyjson)
add_custom_command(TARGET protocol_test POST_BUILD COMMAND protocol_test)

# the laws built on the whole server, each run once built, a broken law
# failing the build: mcp_test, the client of the MCP servers against a server
# of the test; agent_test, a turn of the agent against an endpoint and MCP
# servers of the test; place_test, the place the tools on files act on, the
# machine of the server and a POSIX shell answering alike; engine_test, the
# engine, twins of the laws of the page; http_test, the admission of a
# request, against the server listening on loopback; compaction_test, the
# compaction of a history, the twins of those of the page
foreach(test mcp agent place engine http compaction)
    add_executable(${test}_test server/tests/${test}.cpp)
    target_link_libraries(${test}_test PRIVATE kiss)
    add_custom_command(TARGET ${test}_test POST_BUILD COMMAND ${test}_test)
endforeach()
target_compile_definitions(agent_test PRIVATE COMPACTION="${CMAKE_CURRENT_SOURCE_DIR}/webui/tests/compaction.jsonl"
                                              COMPACT="${CMAKE_CURRENT_SOURCE_DIR}/webui/tests/compact.jsonl")
