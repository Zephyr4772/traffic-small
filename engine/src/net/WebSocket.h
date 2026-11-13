#pragma once
// WebSocket protocol pieces (RFC 6455): the opening handshake and message framing.

#include <cstdint>
#include <string>

namespace sim::ws {

// The Sec-WebSocket-Accept value for a client's Sec-WebSocket-Key.
std::string acceptKey(const std::string& clientKey);

// Encodes one unmasked server-to-client frame.
std::string encodeFrame(int opcode, const std::string& payload);

// Decodes one frame from the start of buf. Returns the number of bytes used,
// 0 if more data is needed, or -1 if the data is invalid.
long decodeFrame(const std::string& buf, int& opcode, std::string& payload);

enum Opcode { Text = 1, Binary = 2, Close = 8, Ping = 9, Pong = 10 };

}  // namespace sim::ws
