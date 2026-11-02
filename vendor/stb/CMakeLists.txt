# stb (public domain or MIT), commit d14d46f of nothings/stb and commit
# 703100c of nothings/stb_image_resize, where the resizer lives now: view
# decodes, resizes and encodes images with stb_image 2.30, stb_image_resize2
# 2.50 and stb_image_write 1.16

add_library(stb STATIC stb.c)
target_include_directories(stb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# suppress warnings in third-party code
if(MSVC)
    target_compile_options(stb PRIVATE /w)
else()
    target_compile_options(stb PRIVATE -w)
    target_link_libraries(stb PUBLIC m)
endif()
