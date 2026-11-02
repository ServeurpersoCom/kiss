# cpp-httplib: HTTP server and client, commit 98459f3 of the branch live of
# ServeurpersoCom/cpp-httplib: its master a18895f, 0.64.0 with the stop of a
# client that ends a request waiting on Windows, and the commits of its two
# pull requests to yhirose/cpp-httplib, pending: one deadline on every wait
# for the reply to a WebSocket close(), and a stop() that ends a connect or a
# TLS handshake; split by its split.py into httplib.h and httplib.cpp.
#
# The server runs plain HTTP behind a reverse proxy, but the client side talks
# to whatever endpoint the user configures, and that includes https: a cloud
# API, or a llama-server behind their own TLS proxy. Windows ships no OpenSSL,
# so the LibreSSL of vendor/libressl is built and linked there; elsewhere the
# system OpenSSL is linked when present.

add_library(httplib STATIC httplib.cpp)
target_include_directories(httplib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# a WebSocket peer that does not answer a Close within a second is gone, so a
# page that never answers holds no end of a stream, nor of the process
target_compile_definitions(httplib PUBLIC CPPHTTPLIB_WEBSOCKET_CLOSE_TIMEOUT_SECOND=1)

# suppress warnings in third-party code
if(MSVC)
    target_compile_options(httplib PRIVATE /w)
else()
    target_compile_options(httplib PRIVATE -w)
endif()

if(WIN32)
    target_link_libraries(httplib PRIVATE ws2_32)

    # LibreSSL: static ssl and crypto only, no apps, tests or install rules
    set(BUILD_SHARED_LIBS OFF)
    set(LIBRESSL_APPS OFF CACHE BOOL "" FORCE)
    set(LIBRESSL_TESTS OFF CACHE BOOL "" FORCE)
    set(LIBRESSL_SKIP_INSTALL ON CACHE BOOL "" FORCE)

    # MSVC at /O2 miscompiles the LibreSSL bignum on x64: RSA and EC checks
    # fail at random and break the handshake, so the C code of this scope,
    # LibreSSL alone, builds at /Od
    if(MSVC)
        foreach(cfg RELEASE RELWITHDEBINFO MINSIZEREL)
            string(REGEX REPLACE "/O[12]" "/Od" CMAKE_C_FLAGS_${cfg} "${CMAKE_C_FLAGS_${cfg}}")
        endforeach()
    endif()
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../libressl ${CMAKE_BINARY_DIR}/libressl EXCLUDE_FROM_ALL)

    # LibreSSL builds at -W4 with its own list of warnings off; the defensive
    # returns of mlkem/mlkem_key.c add C4702, unreachable code, which it turns
    # off the same way for pkcs7/pk7_doit.c alone
    if(MSVC)
        target_compile_options(crypto_obj PRIVATE /wd4702)
    endif()

    # httplib.h loads the Windows certificate store, links crypt32 and
    # undefines the wincrypt.h macros that clash with the LibreSSL headers
    target_compile_definitions(httplib PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT LIBRESSL_DISABLE_OVERRIDE_WINCRYPT_DEFINES_WARNING)
    target_link_libraries(httplib PUBLIC ssl crypto)
    message(STATUS "httplib: https client enabled (vendored LibreSSL)")
    return()
endif()

find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    target_compile_definitions(httplib PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(httplib PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    # httplib.h loads the macOS Keychain root certificates when TLS is enabled
    if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        find_library(CORE_FOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
        find_library(SECURITY_FRAMEWORK Security REQUIRED)
        target_link_libraries(httplib PUBLIC ${CORE_FOUNDATION_FRAMEWORK} ${SECURITY_FRAMEWORK})
    endif()
    message(STATUS "httplib: https client enabled (OpenSSL ${OPENSSL_VERSION})")
else()
    message(STATUS "httplib: no OpenSSL, https endpoints will be refused")
endif()
