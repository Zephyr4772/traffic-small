#pragma once
// A minimal, dependency-free HTTP + WebSocket server (Winsock on Windows, BSD sockets elsewhere).
// Each connection runs on its own thread. Serves the browser front end from web/, a JSON API
// under /api/, and WebSocket endpoints for the live simulation stream.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace sim {

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> query;
    std::map<std::string, std::string> headers;   // keys in lower case

    std::string param(const std::string& key, const std::string& fallback = "") const {
        auto it = query.find(key);
        return it == query.end() ? fallback : it->second;
    }
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "text/plain; charset=utf-8";
    std::string body;

    static HttpResponse json(std::string body) { return {200, "application/json; charset=utf-8", std::move(body)}; }
    static HttpResponse error(int status, std::string message) { return {status, "text/plain; charset=utf-8", std::move(message)}; }
};

// One open WebSocket connection. Used only from its own connection thread.
class WebSocketConnection {
public:
    explicit WebSocketConnection(std::intptr_t socket) : socket_(socket) {}

    bool sendText(const std::string& text);
    bool sendBinary(const std::string& data);

    // Waits up to timeoutMs for incoming data and collects any text messages.
    // Returns false once the connection is closed.
    bool poll(int timeoutMs, std::vector<std::string>& messages);

private:
    bool sendRaw(const std::string& bytes);

    std::intptr_t socket_;
    std::string buffer_;
    bool open_ = true;
};

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;
    using SocketHandler = std::function<void(WebSocketConnection&)>;

    explicit HttpServer(std::string webRoot);
    ~HttpServer();

    void get(const std::string& path, Handler handler) { routes_[path] = std::move(handler); }
    void websocket(const std::string& path, SocketHandler handler) { sockets_[path] = std::move(handler); }

    // Binds to 127.0.0.1:port. Returns false if the port is taken.
    bool listen(int port);
    int port() const { return port_; }
    void serveForever();

private:
    void handleClient(std::intptr_t client);
    HttpResponse serveStatic(const std::string& path) const;

    std::string webRoot_;
    std::map<std::string, Handler> routes_;
    std::map<std::string, SocketHandler> sockets_;
    std::intptr_t listenSocket_ = -1;
    int port_ = 0;
};

}  // namespace sim
