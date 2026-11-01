// base64.cpp: bytes as base64 text and back

#include "lib/base64.h"

#include <cstdint>

static const char * const ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::string & bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const size_t   n = bytes.size() - i < 3 ? bytes.size() - i : 3;
        const uint32_t v = ((uint32_t) (unsigned char) bytes[i] << 16) |
                           (n > 1 ? (uint32_t) (unsigned char) bytes[i + 1] << 8 : 0) |
                           (n > 2 ? (uint32_t) (unsigned char) bytes[i + 2] : 0);
        out += ALPHABET[(v >> 18) & 63];
        out += ALPHABET[(v >> 12) & 63];
        out += n > 1 ? ALPHABET[(v >> 6) & 63] : '=';
        out += n > 2 ? ALPHABET[v & 63] : '=';
    }
    return out;
}

// the value of a character of the alphabet, -1 for padding, -2 for any other
static int value(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    return c == '+' ? 62 : c == '/' ? 63 : c == '=' ? -1 : -2;
}

bool base64_decode(const std::string & text, std::string & bytes) {
    bytes.clear();
    uint32_t acc    = 0;
    int      bits   = 0;
    bool     padded = false;
    for (char c : text) {
        if (c == '\n') {
            continue;
        }
        const int v = value(c);
        if (v == -2) {
            return false;
        }
        if (v == -1) {
            padded = true;
            continue;
        }
        if (padded) {
            return false;
        }
        acc = (acc << 6) | (uint32_t) v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes += (char) ((acc >> bits) & 0xff);
        }
    }
    return bits < 6;
}
