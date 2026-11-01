#pragma once
// base64.h: bytes as base64 text, the alphabet of RFC 4648, padded, and back

#include <string>

std::string base64_encode(const std::string & bytes);

// the bytes a base64 text holds, its line breaks skipped; false for any other
// byte outside the alphabet, padding before its end, or a text that stops
// within a byte
bool base64_decode(const std::string & text, std::string & bytes);
