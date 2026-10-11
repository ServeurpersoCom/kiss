// stb.c: the implementations of the stb headers, compiled once here, apart
// from the server and its warnings; the decoder reads PNG, JPEG and GIF only,
// the formats view knows by their first bytes

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
