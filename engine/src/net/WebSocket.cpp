#include "net/WebSocket.h"

#include <array>

namespace sim::ws {

namespace {

// SHA-1, needed only for the handshake.
std::array<uint8_t, 20> sha1(const std::string& msg) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::string data = msg;
    uint64_t bitLen = static_cast<uint64_t>(msg.size()) * 8;
    data += static_cast<char>(0x80);
    while (data.size() % 64 != 56) data += '\0';
    for (int i = 7; i >= 0; --i) data += static_cast<char>((bitLen >> (i * 8)) & 0xFF);

    auto rotl = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(static_cast<uint8_t>(data[chunk + i * 4])) << 24) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(data[chunk + i * 4 + 1])) << 16) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(data[chunk + i * 4 + 2])) << 8) |
                   static_cast<uint32_t>(static_cast<uint8_t>(data[chunk + i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    std::array<uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<uint8_t>(h[i] >> (24 - j * 8));
    return out;
}

std::string base64(const uint8_t* p, size_t n) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = static_cast<uint32_t>(p[i]) << 16;
        if (i + 1 < n) v |= static_cast<uint32_t>(p[i + 1]) << 8;
        if (i + 2 < n) v |= p[i + 2];
        out += t[(v >> 18) & 63];
        out += t[(v >> 12) & 63];
        out += i + 1 < n ? t[(v >> 6) & 63] : '=';
        out += i + 2 < n ? t[v & 63] : '=';
    }
    return out;
}

}  // namespace

std::string acceptKey(const std::string& clientKey) {
    auto digest = sha1(clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    return base64(digest.data(), digest.size());
}

std::string encodeFrame(int opcode, const std::string& payload) {
    std::string out;
    out += static_cast<char>(0x80 | (opcode & 0x0F));
    const uint64_t n = payload.size();
    if (n < 126) {
        out += static_cast<char>(n);
    } else if (n < 65536) {
        out += static_cast<char>(126);
        out += static_cast<char>((n >> 8) & 0xFF);
        out += static_cast<char>(n & 0xFF);
    } else {
        out += static_cast<char>(127);
        for (int i = 7; i >= 0; --i) out += static_cast<char>((n >> (i * 8)) & 0xFF);
    }
    out += payload;
    return out;
}

long decodeFrame(const std::string& b, int& opcode, std::string& payload) {
    if (b.size() < 2) return 0;
    const uint8_t b0 = static_cast<uint8_t>(b[0]), b1 = static_cast<uint8_t>(b[1]);
    opcode = b0 & 0x0F;
    const bool masked = (b1 & 0x80) != 0;
    uint64_t len = b1 & 0x7F;
    size_t pos = 2;
    if (len == 126) {
        if (b.size() < 4) return 0;
        len = (static_cast<uint64_t>(static_cast<uint8_t>(b[2])) << 8) | static_cast<uint8_t>(b[3]);
        pos = 4;
    } else if (len == 127) {
        if (b.size() < 10) return 0;
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8) | static_cast<uint8_t>(b[2 + i]);
        pos = 10;
    }
    if (len > (1u << 20)) return -1;   // clients only send small commands
    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked) {
        if (b.size() < pos + 4) return 0;
        for (int i = 0; i < 4; ++i) mask[i] = static_cast<uint8_t>(b[pos + i]);
        pos += 4;
    }
    if (b.size() < pos + len) return 0;
    payload = b.substr(pos, static_cast<size_t>(len));
    if (masked)
        for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
    return static_cast<long>(pos + len);
}

}  // namespace sim::ws
