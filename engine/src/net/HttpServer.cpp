#include "net/HttpServer.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include "net/WebSocket.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static const socket_t kInvalidSocket = INVALID_SOCKET;
static void closeSocket(socket_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
static const socket_t kInvalidSocket = -1;
static void closeSocket(socket_t s) { close(s); }
#endif

namespace sim {

namespace {

std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string mimeFor(const std::string& path) {
    auto ext = std::filesystem::path(path).extension().string();
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".js") return "text/javascript; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".ico") return "image/x-icon";
    return "application/octet-stream";
}

const char* statusText(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default: return "Unknown";
    }
}

bool sendAll(socket_t s, const char* data, size_t size) {
    while (size > 0) {
        int chunk = static_cast<int>(std::min<size_t>(size, 1 << 20));
        int sent = send(s, data, chunk, 0);
        if (sent <= 0) return false;
        data += sent;
        size -= static_cast<size_t>(sent);
    }
    return true;
}

}  // namespace

// ---- WebSocket connection --------------------------------------------------------------------

bool WebSocketConnection::sendRaw(const std::string& bytes) {
    if (!open_) return false;
    if (!sendAll(static_cast<socket_t>(socket_), bytes.data(), bytes.size())) open_ = false;
    return open_;
}

bool WebSocketConnection::sendText(const std::string& text) { return sendRaw(ws::encodeFrame(ws::Text, text)); }
bool WebSocketConnection::sendBinary(const std::string& data) { return sendRaw(ws::encodeFrame(ws::Binary, data)); }

bool WebSocketConnection::poll(int timeoutMs, std::vector<std::string>& messages) {
    if (!open_) return false;
    socket_t s = static_cast<socket_t>(socket_);
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    int ready = select(static_cast<int>(s) + 1, &set, nullptr, nullptr, &tv);
    if (ready < 0) { open_ = false; return false; }
    if (ready == 0) return true;

    char buf[4096];
    int got = recv(s, buf, sizeof buf, 0);
    if (got <= 0) { open_ = false; return false; }
    buffer_.append(buf, static_cast<size_t>(got));

    for (;;) {
        int opcode = 0;
        std::string payload;
        long used = ws::decodeFrame(buffer_, opcode, payload);
        if (used == 0) break;
        if (used < 0) { open_ = false; return false; }
        buffer_.erase(0, static_cast<size_t>(used));
        if (opcode == ws::Text) messages.push_back(std::move(payload));
        else if (opcode == ws::Ping) sendRaw(ws::encodeFrame(ws::Pong, payload));
        else if (opcode == ws::Close) {
            sendRaw(ws::encodeFrame(ws::Close, ""));
            open_ = false;
            return false;
        }
    }
    return true;
}

// ---- HTTP server -----------------------------------------------------------------------------

HttpServer::HttpServer(std::string webRoot) : webRoot_(std::move(webRoot)) {
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
}

HttpServer::~HttpServer() {
    if (listenSocket_ != -1) closeSocket(static_cast<socket_t>(listenSocket_));
#ifdef _WIN32
    WSACleanup();
#endif
}

bool HttpServer::listen(int port) {
    socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalidSocket) return false;
#ifndef _WIN32
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // only this computer can connect
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(s, 32) != 0) {
        closeSocket(s);
        return false;
    }
    listenSocket_ = static_cast<std::intptr_t>(s);
    port_ = port;
    return true;
}

void HttpServer::serveForever() {
    for (;;) {
        socket_t client = accept(static_cast<socket_t>(listenSocket_), nullptr, nullptr);
        if (client == kInvalidSocket) continue;
        std::thread([this, client] {
            handleClient(static_cast<std::intptr_t>(client));
            closeSocket(client);
        }).detach();
    }
}

void HttpServer::handleClient(std::intptr_t clientHandle) {
    socket_t client = static_cast<socket_t>(clientHandle);
#ifdef _WIN32
    DWORD timeout = 5000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
#else
    timeval tv{5, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif

    std::string raw;
    char buf[4096];
    while (raw.find("\r\n\r\n") == std::string::npos && raw.size() < 65536) {
        int got = recv(client, buf, sizeof buf, 0);
        if (got <= 0) return;
        raw.append(buf, static_cast<size_t>(got));
    }

    HttpRequest req;
    {
        std::istringstream in(raw.substr(0, raw.find("\r\n\r\n")));
        std::string line, target;
        std::getline(in, line);
        std::istringstream first(line);
        first >> req.method >> target;
        while (std::getline(in, line)) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) req.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
        }
        size_t q = target.find('?');
        req.path = urlDecode(target.substr(0, q));
        if (q != std::string::npos) {
            std::stringstream qs(target.substr(q + 1));
            std::string pair;
            while (std::getline(qs, pair, '&')) {
                size_t eq = pair.find('=');
                if (eq == std::string::npos) req.query[urlDecode(pair)] = "";
                else req.query[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
            }
        }
    }

    // WebSocket upgrade
    auto sock = sockets_.find(req.path);
    if (sock != sockets_.end() && lower(req.headers["upgrade"]) == "websocket" && !req.headers["sec-websocket-key"].empty()) {
        std::string reply = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                            "Sec-WebSocket-Accept: " + ws::acceptKey(req.headers["sec-websocket-key"]) + "\r\n\r\n";
        if (!sendAll(client, reply.data(), reply.size())) return;
#ifdef _WIN32
        DWORD none = 0;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&none), sizeof none);
#endif
        WebSocketConnection conn(clientHandle);
        try {
            sock->second(conn);
        } catch (const std::exception& ex) {
            std::cerr << "WebSocket handler error: " << ex.what() << "\n";
        }
        return;
    }

    HttpResponse res;
    try {
        if (req.method != "GET") res = HttpResponse::error(405, "Only GET is supported");
        else if (auto it = routes_.find(req.path); it != routes_.end()) res = it->second(req);
        else if (req.path.rfind("/api/", 0) == 0) res = HttpResponse::error(404, "Unknown API endpoint");
        else res = serveStatic(req.path);
    } catch (const std::exception& ex) {
        res = HttpResponse::error(500, std::string("Server error: ") + ex.what());
    }

    std::ostringstream head;
    head << "HTTP/1.1 " << res.status << ' ' << statusText(res.status) << "\r\n"
         << "Content-Type: " << res.contentType << "\r\n"
         << "Content-Length: " << res.body.size() << "\r\n"
         << "Cache-Control: no-store\r\n"
         << "Connection: close\r\n\r\n";
    std::string h = head.str();
    if (sendAll(client, h.data(), h.size())) sendAll(client, res.body.data(), res.body.size());
}

HttpResponse HttpServer::serveStatic(const std::string& path) const {
    std::string rel = path == "/" ? "/index.html" : path;
    if (rel.find("..") != std::string::npos) return HttpResponse::error(400, "Bad path");
    std::filesystem::path file = std::filesystem::path(webRoot_) / rel.substr(1);
    std::ifstream in(file, std::ios::binary);
    if (!in) return HttpResponse::error(404, "Not found: " + rel);
    std::ostringstream body;
    body << in.rdbuf();
    return {200, mimeFor(file.string()), body.str()};
}

}  // namespace sim
