#pragma once
// The traffic simulation: vehicles driving on the lane network, obeying signals and
// give-way rules, following each other with the Intelligent Driver Model (IDM).
//
// Each step (0.1 s of simulated time):
//   1. signals advance
//   2. new trips are created at the requested volume and enter the road when there is a gap
//   3. vehicles in the wrong lane for their next turn try to change lanes
//   4. the front vehicle of each lane asks to enter the junction ahead (a "claim");
//      claims are refused while a conflicting movement is in use, the light is red,
//      a give-way rule applies, or there is no room on the exit road (no blocking the box)
//   5. every vehicle picks an acceleration (IDM) from its leader or the stop line
//   6. everyone moves; vehicles that reach their destination leave the road

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Rng.h"
#include "sim/Network.h"
#include "sim/Router.h"
#include "sim/Signals.h"
#include "sim/VehicleTypes.h"

namespace sim {

struct Vehicle {
    uint32_t id = 0;
    VehicleKind kind = VehicleKind::Car;
    bool alive = false;

    // Driver and vehicle parameters (IDM), with some person-to-person variation.
    double length = 4.4, accel = 1.8, decel = 3.0, maxSpeed = 20, minGap = 2, headway = 1.3, speedFactor = 1.0;

    // Trip
    int origin = -1, destination = -1;
    std::vector<int> route;          // links
    std::vector<Turn> turns;         // turn at the end of each link
    size_t ri = 0;                   // index into route of the link being driven (or just left, on a connector)
    double targetS = 0;
    double freeFlowTime = 0;
    double startTime = 0;

    // State
    int element = -1;                // lane or connector
    double s = 0;                    // front bumper position along the element
    double v = 0;                    // speed m/s
    double a = 0;                    // acceleration m/s^2
    int claim = -1;                  // connector reserved at the junction ahead
    int intended = -1;               // connector it would like to use next
    double stopLineSince = -1;       // when it came to a stop at the line (give-way order)
    double lat = 0;                  // sideways offset that eases out after a lane change
    double laneChangeCooldown = 0;

    // Tracking
    double waitTime = 0;             // seconds spent (nearly) stopped
    int stops = 0;
    bool stopped = false;
    double stuckTime = 0;
};

struct TripRecord { double duration, delay; };

class Simulation {
public:
    Simulation(std::shared_ptr<const City> city, uint64_t seed);

    void step(double dt);

    double time() const { return time_; }
    double volume() const { return volume_; }
    double defaultVolume() const { return defaultVolume_; }
    void setVolume(double vehiclesPerHour) { volume_ = std::max(0.0, vehiclesPerHour); }
    void setTracked(uint32_t id) { tracked_ = id; }
    // 0 = fixed-time signals, 1 = smart priority signals.
    void setController(int mode) { mode_ = mode == 1 ? 1 : 0; }
    int controllerMode() const { return mode_; }
    int activeVehicles() const { return active_; }
    const Network& network() const { return net_; }

    // Binary snapshot for the browser: vehicles and signal states (see web/js/live.js).
    void writeFrame(std::string& out, uint32_t cityVersion) const;
    // Binary congestion map: average speed on each link as a share of its limit.
    void writeFlow(std::string& out, uint32_t cityVersion) const;
    // Live statistics as JSON (includes the tracked vehicle, if any).
    std::string statsJson() const;

private:
    // Trips
    void spawnTrips(double dt);
    bool makeTrip(Vehicle& v);
    bool makeTripBetween(int origin, int destination, VehicleKind kind, Vehicle& v);
    void spawnServiceTrips(double dt);
    void updateSmartSignals();
    double clockHours() const { return 8.0 + time_ / 3600.0; }
    void insertPending();
    bool tryInsert(Vehicle& v);

    // Driving
    void laneChanges();
    bool tryLaneChange(int slot, int targetLane);
    void claims();
    int chooseConnector(const Vehicle& v) const;
    bool canEnter(const Vehicle& v, int connector, double dist) const;
    bool conflictingTraffic(const Vehicle& me, int connector, double myTta, double window, bool onlyMajor, bool tieBreak) const;
    bool spaceAfter(const Vehicle& v, int connector) const;
    void claim(Vehicle& v, int connector);
    void releaseClaim(Vehicle& v);
    void accelerations();
    double idm(const Vehicle& v, double desired, double gap, double leaderSpeed, bool hasLeader) const;
    void move(double dt);
    void removeVehicle(int slot, bool arrived);

    // Element lists (front vehicle first)
    void insertSorted(int element, int slot);
    void eraseFrom(int element, int slot);
    int frontOf(int element) const { return elemVehicles_[element].empty() ? -1 : elemVehicles_[element].front(); }
    int backOf(int element) const { return elemVehicles_[element].empty() ? -1 : elemVehicles_[element].back(); }

    LightState lightFor(int link) const;
    bool laneSuits(int link, int laneIndex, int routeIndex, const Vehicle& v) const;
    bool onFinalLink(const Vehicle& v) const { return v.ri + 1 >= v.route.size(); }
    void vehiclePose(const Vehicle& v, Vec2& pos, double& angle) const;

    std::shared_ptr<const City> city_;
    Network net_;
    Router router_;
    Rng rng_;
    double time_ = 0;

    std::vector<Vehicle> vehicles_;
    std::vector<int> freeSlots_;
    std::unordered_map<uint32_t, int> slotOf_;
    std::vector<std::vector<int>> elemVehicles_;
    std::vector<int> occupancy_;     // per connector: vehicles on it plus claims on it
    std::vector<double> reserved_;   // per lane: road space promised to vehicles still in the junction
    uint32_t nextId_ = 1;
    int active_ = 0;

    struct SignalPlan {
        int node = -1;
        std::vector<int> phaseLinks;
        std::unique_ptr<SignalController> controller;   // fixed-time
        std::unique_ptr<SmartController> smart;          // priority-aware
    };
    SignalController& active(const SignalPlan& p) const {
        return mode_ == 1 ? static_cast<SignalController&>(*p.smart) : *p.controller;
    }
    int mode_ = 0;
    std::vector<int> schools_, hospitals_, depots_, homes_;
    double schoolBusAcc_ = 0, ambulanceAcc_ = 0;
    long long arrivedKind_[kVehicleKinds] = {};
    double tripKind_[kVehicleKinds] = {};
    std::vector<SignalPlan> signals_;
    std::vector<int> linkSignal_, linkPhase_;

    // Demand
    std::vector<int> originIds_, destIds_;
    std::vector<double> originCdf_, destCdf_;
    double volume_ = 2000, defaultVolume_ = 2000, spawnAccumulator_ = 0;
    struct Pending { Vehicle v; double since; };
    std::deque<Pending> pending_;

    // Statistics
    long long spawned_ = 0, arrived_ = 0, teleported_ = 0, dropped_ = 0, routeFailures_ = 0;
    std::deque<TripRecord> recent_;
    uint32_t tracked_ = 0;
};

}  // namespace sim
