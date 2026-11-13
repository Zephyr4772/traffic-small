// Smart Traffic City — entry point.
//
//   traffic.exe                      start the server and open the browser
//   traffic.exe --port 9000          use another port
//   traffic.exe --no-open            don't open the browser
//   traffic.exe --dump city.json     write one city to a file and exit
//   traffic.exe --selftest           generate many cities and check they are valid

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "net/HttpServer.h"
#include "sim/SimulationHost.h"
#include "world/CityGenerator.h"
#include "world/CityJson.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

using namespace sim;

namespace {

double clampParam(const HttpRequest& req, const std::string& key, double fallback, double lo, double hi) {
    std::string v = req.param(key);
    if (v.empty()) return fallback;
    try {
        return std::clamp(std::stod(v), lo, hi);
    } catch (...) {
        return fallback;
    }
}

CityConfig configFromRequest(const HttpRequest& req) {
    CityConfig cfg = CityConfig::preset(req.param("scale", "medium"));
    try {
        cfg.seed = std::stoull(req.param("seed", "1"));
    } catch (...) {
        cfg.seed = 1;
    }
    cfg.sizeM = clampParam(req, "size", cfg.sizeM, 500, 6000);
    cfg.blockM = clampParam(req, "block", cfg.blockM, 100, 300);
    cfg.arterialSpacing = static_cast<int>(clampParam(req, "spacing", cfg.arterialSpacing, 3, 8));
    cfg.jitter = clampParam(req, "jitter", cfg.jitter, 0, 0.35);
    cfg.irregular = clampParam(req, "irregular", cfg.irregular, 0, 0.35);
    std::string river = req.param("river");
    if (!river.empty()) cfg.river = river != "0" && river != "false";
    return cfg;
}

std::filesystem::path findWebRoot(const char* argv0) {
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates = {"web", "../web", "../../web"};
    std::error_code ec;
    fs::path exeDir = fs::absolute(argv0, ec).parent_path();
    candidates.push_back(exeDir / "web");
    candidates.push_back(exeDir / ".." / "web");
    for (const auto& c : candidates)
        if (fs::exists(c / "index.html", ec)) return fs::canonical(c, ec);
    return {};
}

void openBrowser(const std::string& url) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::system(("open " + url).c_str());
#else
    std::system(("xdg-open " + url + " >/dev/null 2>&1 &").c_str());
#endif
}

// Checks the invariants the rest of the simulator relies on.
bool validateCity(const City& c, std::string& why) {
    if (c.nodes.empty() || c.edges.empty()) { why = "no roads"; return false; }
    for (const auto& e : c.edges) {
        if (e.a < 0 || e.b < 0 || e.a >= static_cast<int>(c.nodes.size()) || e.b >= static_cast<int>(c.nodes.size())) { why = "edge with bad node"; return false; }
        if (!(e.length > 1)) { why = "zero-length edge"; return false; }
    }
    // The road network must be fully connected (every building can reach every other).
    std::vector<char> seen(c.nodes.size(), 0);
    std::vector<int> stack = {0};
    seen[0] = 1;
    size_t reached = 0;
    while (!stack.empty()) {
        int u = stack.back(); stack.pop_back();
        ++reached;
        for (int eid : c.nodes[u].edges) {
            int v = c.edges[eid].other(u);
            if (!seen[v]) { seen[v] = 1; stack.push_back(v); }
        }
    }
    if (reached != c.nodes.size()) { why = "road network is not connected"; return false; }
    for (const auto& b : c.buildings) {
        if (b.accessEdge < 0) { why = "building without road access"; return false; }
        for (const auto& p : b.poly)
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) { why = "NaN in building"; return false; }
    }
    auto s = c.stats();
    if (s.schools < 1 || s.hospitals < 1) { why = "missing school or hospital"; return false; }
    return true;
}

int selfTest() {
    int failures = 0;
    for (const char* scale : {"small", "medium", "large"}) {
        for (uint64_t seed = 1; seed <= 25; ++seed) {
            CityConfig cfg = CityConfig::preset(scale);
            cfg.seed = seed;
            cfg.river = seed % 4 != 0;
            City city = CityGenerator().generate(cfg);
            std::string why;
            if (!validateCity(city, why)) {
                std::cout << "FAIL " << scale << " seed " << seed << ": " << why << "\n";
                ++failures;
            }
        }
        CityConfig cfg = CityConfig::preset(scale);
        auto t0 = std::chrono::steady_clock::now();
        City city = CityGenerator().generate(cfg);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        auto s = city.stats();
        std::cout << scale << ": " << s.signals << " signals, " << s.allWayStops << " all-way stops, " << s.buildings
                  << " buildings, " << s.population << " people, " << s.jobs << " jobs, " << s.schools << " schools, "
                  << s.hospitals << " hospitals, " << s.bridges << " bridges (" << ms << " ms)\n";
    }
    std::cout << (failures ? "Self-test FAILED: " : "Self-test passed: ") << failures << " failures in 75 cities\n";
    return failures ? 1 : 0;
}

// Runs traffic on the default medium city without a browser and prints how it is going.
int simTest(double hours, double volume) {
    auto city = std::make_shared<const City>(CityGenerator().generate(CityConfig::preset("medium")));
    Simulation sim(city, city->config.seed);
    if (volume > 0) sim.setVolume(volume);
    std::cout << "Simulating " << hours << " h on the medium city at " << sim.volume() << " vehicles/hour\n";
    auto t0 = std::chrono::steady_clock::now();
    const int steps = static_cast<int>(hours * 3600 / SimulationHost::kStep);
    for (int i = 1; i <= steps; ++i) {
        sim.step(SimulationHost::kStep);
        if (i % 3000 == 0) {   // every 5 simulated minutes
            std::string s = sim.statsJson();
            std::cout << "  t=" << static_cast<int>(sim.time() / 60 + 0.5) << " min  " << s << "\n";
        }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "Done: " << hours * 3600 / secs << "x real time (" << secs << " s wall)\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    int port = 8080;
    bool open = true;
    std::string dumpFile;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--no-open") open = false;
        else if (a == "--dump" && i + 1 < argc) dumpFile = argv[++i];
        else if (a == "--selftest") return selfTest();
        else if (a == "--simtest")
            return simTest(i + 1 < argc ? std::atof(argv[i + 1]) : 1.0, i + 2 < argc ? std::atof(argv[i + 2]) : 0.0);
        else if (a == "--help" || a == "-h") {
            std::cout << "Usage: traffic [--port N] [--no-open] [--dump city.json] [--selftest] [--simtest hours]\n";
            return 0;
        }
    }

    if (!dumpFile.empty()) {
        City city = CityGenerator().generate(CityConfig::preset("medium"));
        std::ofstream(dumpFile) << cityToJson(city);
        std::cout << "Wrote " << dumpFile << "\n";
        return 0;
    }

    auto webRoot = findWebRoot(argv[0]);
    if (webRoot.empty()) {
        std::cerr << "Could not find the web/ folder. Run traffic.exe from the project folder (run.bat does this).\n";
        return 1;
    }

    SimulationHost host;
    host.loadCity(std::make_shared<const City>(CityGenerator().generate(CityConfig::preset("medium"))));

    HttpServer server(webRoot.string());
    // Generating a city also restarts the traffic simulation on it.
    server.get("/api/city", [&host](const HttpRequest& req) {
        auto t0 = std::chrono::steady_clock::now();
        CityConfig cfg = configFromRequest(req);
        auto city = std::make_shared<const City>(CityGenerator().generate(cfg));
        std::string body = cityToJson(*city);
        uint32_t version = host.loadCity(city);
        body.insert(1, "\"cityVersion\":" + std::to_string(version) + ",");
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        auto s = city->stats();
        std::cout << "City seed " << cfg.seed << " (" << cfg.scale << "): " << s.signals << " signals, " << s.buildings
                  << " buildings, " << s.population << " people  [" << ms << " ms, " << body.size() / 1024 << " KB]\n";
        return HttpResponse::json(std::move(body));
    });
    server.get("/api/health", [](const HttpRequest&) { return HttpResponse::json("{\"ok\":true}"); });

    // Live stream: vehicle frames (~15 per second), statistics and congestion (2 per second).
    server.websocket("/ws", [&host](WebSocketConnection& conn) {
        if (!conn.sendText(host.helloJson())) return;
        uint64_t frameSeen = 0, statsSeen = 0, flowSeen = 0;
        std::vector<std::string> messages;
        while (conn.poll(10, messages)) {
            for (const auto& m : messages) host.handleCommand(m);
            messages.clear();
            auto f = host.frame();
            if (f.data && f.serial != frameSeen) { frameSeen = f.serial; if (!conn.sendBinary(*f.data)) break; }
            auto s = host.stats();
            if (s.data && s.serial != statsSeen) { statsSeen = s.serial; if (!conn.sendText(*s.data)) break; }
            auto fl = host.flow();
            if (fl.data && fl.serial != flowSeen) { flowSeen = fl.serial; if (!conn.sendBinary(*fl.data)) break; }
        }
    });

    int tries = 0;
    while (!server.listen(port) && tries < 10) { ++port; ++tries; }
    if (tries == 10) {
        std::cerr << "Could not open a port. Is another copy already running?\n";
        return 1;
    }

    std::string url = "http://localhost:" + std::to_string(server.port()) + "/";
    std::cout << "Smart Traffic City\n"
              << "  Web files: " << webRoot.string() << "\n"
              << "  Open " << url << " in your browser.  Press Ctrl+C to stop.\n\n";
    if (open) openBrowser(url);
    server.serveForever();
    return 0;
}
