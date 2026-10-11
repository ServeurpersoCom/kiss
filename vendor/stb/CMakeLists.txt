# stb (public domain or MIT, Sean Barrett), commit 2c980bb of nothings/stb:
# view decodes, resizes and encodes images with stb_image 2.30,
# stb_image_resize2 2.18 and stb_image_write 1.16

add_library(stb STATIC stb.c)
target_include_directories(stb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# suppress warnings in third-party code
if(MSVC)
    target_compile_options(stb PRIVATE /w)
else()
    target_compile_options(stb PRIVATE -w)
    target_link_libraries(stb PUBLIC m)
endif()
