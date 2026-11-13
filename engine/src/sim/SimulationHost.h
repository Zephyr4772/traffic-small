#pragma once
// Runs the simulation on its own thread in (scaled) real time and publishes snapshots
// that any number of browser connections can pick up.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "sim/Simulation.h"

namespace sim {

class SimulationHost {
public:
    SimulationHost();
    ~SimulationHost();

    // Replaces the running simulation with a fresh one on this city. Returns its version number,
    // which tags every frame so the browser can ignore frames from the previous city.
    uint32_t loadCity(std::shared_ptr<const City> city);

    // A JSON command from the browser, e.g. {"cmd":"speed","value":5}.
    void handleCommand(const std::string& json);

    std::string helloJson() const;

    struct Snapshot {
        uint64_t serial = 0;
        std::shared_ptr<const std::string> data;
    };
    Snapshot frame() const { std::lock_guard<std::mutex> lock(pubMutex_); return frame_; }
    Snapshot stats() const { std::lock_guard<std::mutex> lock(pubMutex_); return stats_; }
    Snapshot flow() const { std::lock_guard<std::mutex> lock(pubMutex_); return flow_; }

    static constexpr double kStep = 0.1;   // seconds of simulated time per step

private:
    void run();
    void publish(Snapshot& slot, std::string data);
    std::string hostStatsJson(const std::string& simStats) const;

    mutable std::mutex simMutex_;          // guards everything below until pubMutex_
    std::unique_ptr<Simulation> sim_;
    std::shared_ptr<const City> city_;
    uint32_t cityVersion_ = 0;
    bool running_ = true;
    double speed_ = 1.0;
    double effectiveSpeed_ = 0;
    double stepMs_ = 0;

    mutable std::mutex pubMutex_;
    Snapshot frame_, stats_, flow_;
    uint64_t serial_ = 0;

    std::atomic<bool> stop_{false};
    std::thread thread_;
};

}  // namespace sim
