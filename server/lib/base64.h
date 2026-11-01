#pragma once
// base64.h: bytes as base64 text, the alphabet of RFC 4648, padded, and back

#include <string>

std::string base64_encode(const std::string & bytes);

// the bytes a base64 text holds, its line breaks and every other byte outside
// the alphabet skipped; false when what remains does not decode
bool base64_decode(const std::string & text, std::string & bytes);
