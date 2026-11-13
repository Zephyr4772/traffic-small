#include "sim/SimulationHost.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

#include "core/Json.h"

namespace sim {

namespace {

// Tiny readers for the flat JSON commands the browser sends.
std::string jsonString(const std::string& j, const std::string& key) {
    size_t k = j.find("\"" + key + "\"");
    if (k == std::string::npos) return "";
    size_t q1 = j.find('"', j.find(':', k) + 1);
    size_t q2 = q1 == std::string::npos ? q1 : j.find('"', q1 + 1);
    return q2 == std::string::npos ? "" : j.substr(q1 + 1, q2 - q1 - 1);
}

bool jsonNumber(const std::string& j, const std::string& key, double& out) {
    size_t k = j.find("\"" + key + "\"");
    if (k == std::string::npos) return false;
    size_t colon = j.find(':', k);
    if (colon == std::string::npos) return false;
    char* end = nullptr;
    out = std::strtod(j.c_str() + colon + 1, &end);
    return end != j.c_str() + colon + 1;
}

}  // namespace

SimulationHost::SimulationHost() : thread_([this] { run(); }) {}

SimulationHost::~SimulationHost() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

uint32_t SimulationHost::loadCity(std::shared_ptr<const City> city) {
    // Build the network outside the lock; it is the slow part.
    auto fresh = std::make_unique<Simulation>(city, city->config.seed);
    std::lock_guard<std::mutex> lock(simMutex_);
    city_ = std::move(city);
    sim_ = std::move(fresh);
    return ++cityVersion_;
}

void SimulationHost::handleCommand(const std::string& json) {
    const std::string cmd = jsonString(json, "cmd");
    double value = 0;
    const bool hasValue = jsonNumber(json, "value", value);
    std::lock_guard<std::mutex> lock(simMutex_);
    if (cmd == "pause") running_ = false;
    else if (cmd == "play") running_ = true;
    else if (cmd == "speed" && hasValue) speed_ = std::clamp(value, 0.1, 50.0);
    else if (cmd == "volume" && hasValue && sim_) sim_->setVolume(std::clamp(value, 0.0, 20000.0));
    else if (cmd == "controller" && hasValue && sim_) sim_->setController(static_cast<int>(value));
    else if (cmd == "track" && sim_) sim_->setTracked(hasValue && value > 0 ? static_cast<uint32_t>(value) : 0);
    else if (cmd == "reset" && city_) {
        double volume = sim_ ? sim_->volume() : 0;
        int mode = sim_ ? sim_->controllerMode() : 0;
        sim_ = std::make_unique<Simulation>(city_, city_->config.seed);
        if (volume > 0) sim_->setVolume(volume);
        sim_->setController(mode);
        ++cityVersion_;
    }
}

std::string SimulationHost::helloJson() const {
    JsonWriter w;
    w.beginObject().field("type", "hello").field("step", kStep);
    {
        std::lock_guard<std::mutex> lock(simMutex_);
        w.field("cityVersion", static_cast<int>(cityVersion_));
        w.field("running", running_);
        w.field("speed", speed_);
    }
    w.key("kinds").beginArray();
    for (int k = 0; k < kVehicleKinds; ++k) {
        const auto& s = specOf(static_cast<VehicleKind>(k));
        w.beginObject().field("name", s.name).field("length", s.length).field("width", s.width).endObject();
    }
    w.endArray().endObject();
    return w.str();
}

void SimulationHost::publish(Snapshot& slot, std::string data) {
    auto ptr = std::make_shared<const std::string>(std::move(data));
    std::lock_guard<std::mutex> lock(pubMutex_);
    slot.serial = ++serial_;
    slot.data = std::move(ptr);
}

std::string SimulationHost::hostStatsJson(const std::string& simStats) const {
    // Append host-level fields to the simulation's JSON object.
    JsonWriter w;
    w.beginObject()
        .field("running", running_)
        .field("speed", speed_)
        .field("effectiveSpeed", effectiveSpeed_)
        .field("stepMs", stepMs_)
        .field("cityVersion", static_cast<int>(cityVersion_))
        .endObject();
    std::string extra = w.str();
    return simStats.substr(0, simStats.size() - 1) + "," + extra.substr(1);
}

void SimulationHost::run() {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    auto lastFrame = last, lastStats = last;
    double budget = 0;          // simulated seconds owed
    double simAtLastStats = 0;

    while (!stop_) {
        auto now = clock::now();
        double wall = std::chrono::duration<double>(now - last).count();
        last = now;
        {
            std::lock_guard<std::mutex> lock(simMutex_);
            if (sim_ && running_) {
                budget += wall * speed_;
                int steps = 0;
                auto t0 = clock::now();
                while (budget >= kStep && steps < 1000) {
                    sim_->step(kStep);
                    budget -= kStep;
                    ++steps;
                    if (clock::now() - t0 > std::chrono::milliseconds(45)) break;
                }
                if (budget > 2.0) budget = 0;   // can't keep up: run slower rather than lag further
                if (steps > 0) {
                    double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count() / steps;
                    stepMs_ = stepMs_ * 0.9 + ms * 0.1;
                }
            } else {
                budget = 0;
            }

            if (sim_ && now - lastFrame >= std::chrono::milliseconds(66)) {
                std::string buf;
                sim_->writeFrame(buf, cityVersion_);
                publish(frame_, std::move(buf));
                lastFrame = now;
            }
            if (sim_ && now - lastStats >= std::chrono::milliseconds(500)) {
                double elapsed = std::chrono::duration<double>(now - lastStats).count();
                effectiveSpeed_ = elapsed > 0 ? (sim_->time() - simAtLastStats) / elapsed : 0;
                if (effectiveSpeed_ < 0) effectiveSpeed_ = 0;   // the city was just replaced
                simAtLastStats = sim_->time();
                publish(stats_, hostStatsJson(sim_->statsJson()));
                std::string flow;
                sim_->writeFlow(flow, cityVersion_);
                publish(flow_, std::move(flow));
                lastStats = now;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
}

}  // namespace sim
