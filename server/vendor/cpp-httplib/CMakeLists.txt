# cpp-httplib: the HTTP server. TLS stays out: a LAN or the internet reaches
# the server through a reverse proxy or an ssh tunnel.

add_library(httplib STATIC httplib.cpp)
target_include_directories(httplib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# suppress warnings in third-party code
if(MSVC)
    target_compile_options(httplib PRIVATE /w)
else()
    target_compile_options(httplib PRIVATE -w)
endif()

if(WIN32)
    target_link_libraries(httplib PRIVATE ws2_32)
endif()
