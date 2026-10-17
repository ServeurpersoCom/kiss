# CMake equivalent of `xxd -i ${INPUT} ${OUTPUT}`
# Converts any binary file into a C array. Pure CMake, no external tools.
# Works on Linux, macOS, and Windows (no xxd, no bash required).
# Usage: cmake -DINPUT=path/to/file -DOUTPUT=path/to/file.hpp -DNAME=symbol -P xxd.cmake

file(READ "${INPUT}" hex_data HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex_sequence "${hex_data}")

string(LENGTH ${hex_data} hex_len)
math(EXPR len "${hex_len} / 2")

file(WRITE "${OUTPUT}" "unsigned char ${NAME}[] = {${hex_sequence}};\nunsigned int ${NAME}_len = ${len};\n")
