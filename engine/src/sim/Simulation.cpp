#include "sim/Simulation.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Json.h"

namespace sim {

namespace {

constexpr double kStartClock = 8 * 3600.0;   // the day starts at 08:00
constexpr double kStuckLimit = 300.0;         // a vehicle frozen this long is removed (gridlock safety valve)
constexpr size_t kMaxPending = 600;

template <class T>
void put(std::string& out, T v) {
    char b[sizeof(T)];
    std::memcpy(b, &v, sizeof(T));   // little-endian on x86, matching the browser's DataView reads
    out.append(b, sizeof(T));
}

int pickWeighted(const std::vector<double>& cdf, Rng& rng) {
    if (cdf.empty() || cdf.back() <= 0) return -1;
    double r = rng.uniform() * cdf.back();
    return static_cast<int>(std::upper_bound(cdf.begin(), cdf.end(), r) - cdf.begin());
}

bool isIndustrial(BuildingType t) { return t == BuildingType::Warehouse || t == BuildingType::Factory; }

}  // namespace

// ------------------------------------------------------------------------------------------
// Setup

Simulation::Simulation(std::shared_ptr<const City> city, uint64_t seed)
    : city_(std::move(city)), router_(net_, *city_), rng_(seed * 7919 + 17) {
    net_.build(*city_);
    elemVehicles_.assign(net_.elementCount(), {});
    occupancy_.assign(net_.connectors.size(), 0);
    reserved_.assign(net_.lanes.size(), 0.0);

    // Signals: one phase per incoming approach, longer greens for bigger roads.
    linkSignal_.assign(net_.links.size(), -1);
    linkPhase_.assign(net_.links.size(), -1);
    for (const auto& J : net_.junctions) {
        if (J.control != JunctionControl::Signal || J.inLinks.empty()) continue;
        SignalPlan plan;
        plan.node = J.node;
        plan.phaseLinks = J.inLinks;
        // Order approaches clockwise so the lights go round the junction.
        std::sort(plan.phaseLinks.begin(), plan.phaseLinks.end(), [&](int a, int b) {
            Vec2 da = net_.links[a].dir, db = net_.links[b].dir;
            return std::atan2(da.y, da.x) < std::atan2(db.y, db.x);
        });
        std::vector<double> greens;
        for (int l : plan.phaseLinks) {
            RoadClass c = net_.links[l].cls;
            greens.push_back(c == RoadClass::Arterial ? 24.0 : c == RoadClass::Collector ? 16.0 : 10.0);
        }
        double cycle = 0;
        for (double g : greens) cycle += g + 5.0;
        auto controller = std::make_unique<FixedTimeController>(greens, 3.0, 2.0, rng_.range(0, cycle));
        int index = static_cast<int>(signals_.size());
        for (size_t p = 0; p < plan.phaseLinks.size(); ++p) {
            linkSignal_[plan.phaseLinks[p]] = index;
            linkPhase_[plan.phaseLinks[p]] = static_cast<int>(p);
        }
        plan.controller = std::move(controller);
        plan.smart = std::make_unique<SmartController>(static_cast<int>(plan.phaseLinks.size()));
        signals_.push_back(std::move(plan));
    }

    // Demand: trips start mostly at homes and end mostly at workplaces (and the reverse).
    double ow = 0, dw = 0;
    for (const auto& b : city_->buildings) {
        if (b.accessEdge < 0) continue;
        if (b.type == BuildingType::School) schools_.push_back(b.id);
        else if (b.type == BuildingType::Hospital) hospitals_.push_back(b.id);
        else if (b.type == BuildingType::BusDepot) depots_.push_back(b.id);
        else if (b.residents > 0) homes_.push_back(b.id);
        double o = b.residents + 0.35 * b.jobs;
        double d = b.jobs + 0.35 * b.residents + 0.2 * b.capacity;
        if (o > 0) { ow += o; originIds_.push_back(b.id); originCdf_.push_back(ow); }
        if (d > 0) { dw += d; destIds_.push_back(b.id); destCdf_.push_back(dw); }
    }
    // Default volume scales with the city: about one trip per hour for every 18 residents
    // (a busy morning, with fixed-time signals near their limit).
    defaultVolume_ = std::max(3200.0, std::round(city_->stats().population / 18.0 / 100.0) * 100.0);
    volume_ = defaultVolume_;
}

// ------------------------------------------------------------------------------------------
// Main loop

void Simulation::step(double dt) {
    time_ += dt;
    if (mode_ == 1) updateSmartSignals();
    for (auto& s : signals_) active(s).update(dt);
    spawnTrips(dt);
    spawnServiceTrips(dt);
    insertPending();
    laneChanges();
    claims();
    accelerations();
    move(dt);
}

LightState Simulation::lightFor(int link) const {
    int s = linkSignal_[link];
    return s < 0 ? LightState::Green : active(signals_[s]).state(linkPhase_[link]);
}

// Priority score of each approach for the smart controller.
void Simulation::updateSmartSignals() {
    const double h = std::fmod(clockHours(), 24.0);
    const bool schoolHours = (h >= 7.0 && h < 9.0) || (h >= 13.5 && h < 15.5);
    for (auto& plan : signals_) {
        std::vector<double> scores(plan.phaseLinks.size(), 0.0);
        int emergency = -1;
        for (size_t p = 0; p < plan.phaseLinks.size(); ++p) {
            const Link& L = net_.links[plan.phaseLinks[p]];
            for (int k = 0; k < L.lanes; ++k) {
                const int lane = L.firstLane + k;
                for (int slot : elemVehicles_[lane]) {
                    const Vehicle& v = vehicles_[slot];
                    if (net_.lanes[lane].length - v.s > 150) continue;   // what the detectors can see
                    double w = 1.0;
                    switch (v.kind) {
                        case VehicleKind::Ambulance: emergency = static_cast<int>(p); w = 50; break;
                        case VehicleKind::SchoolBus: w = schoolHours ? 6.0 : 2.0; break;
                        case VehicleKind::Bus: w = 3.0; break;
                        case VehicleKind::Truck: w = 1.8; break;   // big vehicles clear more when there is time to spare
                        case VehicleKind::Motorbike: w = 0.6; break;
                        default: break;
                    }
                    scores[p] += w * (1.0 + v.waitTime / 30.0);
                }
            }
            // Don't send traffic into a full exit road: halve the score if the road ahead is backed up.
            const Junction& J = net_.junctions[L.to];
            for (int out : J.outLinks) {
                int back = backOf(net_.links[out].firstLane);
                if (back >= 0 && vehicles_[back].s < 8) { scores[p] *= 0.85; }
            }
        }
        plan.smart->setDemand(scores, emergency);
    }
}

// Purposeful trips: school buses to schools in school hours, ambulances from hospitals.
void Simulation::spawnServiceTrips(double dt) {
    const double h = std::fmod(clockHours(), 24.0);
    const bool schoolHours = (h >= 7.0 && h < 9.0) || (h >= 13.5 && h < 15.5);
    if (!schools_.empty()) schoolBusAcc_ += (schoolHours ? 60.0 : 8.0) / 3600.0 * dt;
    if (!hospitals_.empty()) ambulanceAcc_ += 12.0 / 3600.0 * dt;
    while (schoolBusAcc_ >= 1) {
        schoolBusAcc_ -= 1;
        int school = schools_[rng_.rangeInt(0, static_cast<int>(schools_.size()) - 1)];
        // Morning: from homes/depot to school. Afternoon: from school back out.
        int other = !depots_.empty() && rng_.chance(0.3) ? depots_[rng_.rangeInt(0, static_cast<int>(depots_.size()) - 1)]
                                                         : homes_.empty() ? school : homes_[rng_.rangeInt(0, static_cast<int>(homes_.size()) - 1)];
        bool toSchool = h < 12;
        Vehicle v;
        if (makeTripBetween(toSchool ? other : school, toSchool ? school : other, VehicleKind::SchoolBus, v)) pending_.push_back({std::move(v), time_});
    }
    while (ambulanceAcc_ >= 1) {
        ambulanceAcc_ -= 1;
        if (homes_.empty()) break;
        int hospital = hospitals_[rng_.rangeInt(0, static_cast<int>(hospitals_.size()) - 1)];
        int call = homes_[rng_.rangeInt(0, static_cast<int>(homes_.size()) - 1)];
        Vehicle v;
        bool toPatient = rng_.chance(0.5);
        if (makeTripBetween(toPatient ? hospital : call, toPatient ? call : hospital, VehicleKind::Ambulance, v))
            pending_.push_front({std::move(v), time_});   // ambulances go first
    }
}

bool Simulation::makeTripBetween(int o, int d, VehicleKind kind, Vehicle& v) {
    const auto& B = city_->buildings;
    if (o == d || B[o].accessEdge == B[d].accessEdge) return false;
    Route r;
    if (!router_.route(B[o], B[d], static_cast<uint32_t>(nextId_ * 2654435761u + d), r)) return false;
    const VehicleSpec& spec = specOf(kind);
    v = Vehicle();
    v.kind = kind;
    v.length = spec.length;
    v.accel = spec.maxAccel;
    v.decel = spec.comfortDecel;
    v.maxSpeed = spec.maxSpeed;
    v.minGap = spec.minGap;
    v.headway = spec.headway;
    v.speedFactor = kind == VehicleKind::Ambulance ? 1.35 : 0.95;
    v.origin = o;
    v.destination = d;
    v.route = std::move(r.links);
    v.turns = std::move(r.turns);
    v.targetS = r.targetS;
    v.freeFlowTime = r.freeFlowTime;
    v.element = net_.links[v.route[0]].firstLane;
    v.s = std::max(r.startS, v.length);
    return true;
}

// ------------------------------------------------------------------------------------------
// Trips

void Simulation::spawnTrips(double dt) {
    spawnAccumulator_ += volume_ / 3600.0 * dt;
    while (spawnAccumulator_ >= 1.0) {
        spawnAccumulator_ -= 1.0;
        if (pending_.size() >= kMaxPending) { ++dropped_; continue; }
        Vehicle v;
        if (makeTrip(v)) pending_.push_back({std::move(v), time_});
    }
}

bool Simulation::makeTrip(Vehicle& v) {
    const auto& B = city_->buildings;
    int o = -1, d = -1;
    for (int attempt = 0; attempt < 6; ++attempt) {
        int oi = pickWeighted(originCdf_, rng_), di = pickWeighted(destCdf_, rng_);
        if (oi < 0 || di < 0) return false;
        o = originIds_[oi];
        d = destIds_[di];
        if (B[o].accessEdge != B[d].accessEdge) break;
        o = d = -1;
    }
    if (o < 0) return false;

    Route r;
    if (!router_.route(B[o], B[d], static_cast<uint32_t>(nextId_ * 2654435761u), r)) {
        ++routeFailures_;
        return false;
    }

    // Vehicle mix for an Indian city; goods vehicles are more likely around industry.
    bool goods = isIndustrial(B[o].type) || isIndustrial(B[d].type);
    double x = rng_.uniform();
    VehicleKind kind;
    if (goods && x < 0.30) kind = VehicleKind::Truck;
    else if (goods && x < 0.50) kind = VehicleKind::Van;
    else {
        double y = rng_.uniform();
        kind = y < 0.42 ? VehicleKind::Car : y < 0.76 ? VehicleKind::Motorbike : y < 0.90 ? VehicleKind::AutoRickshaw
             : y < 0.97 ? VehicleKind::Van : VehicleKind::Truck;
    }
    const VehicleSpec& spec = specOf(kind);

    v = Vehicle();
    v.kind = kind;
    v.length = spec.length;
    v.accel = spec.maxAccel * rng_.range(0.9, 1.1);
    v.decel = spec.comfortDecel;
    v.maxSpeed = spec.maxSpeed;
    v.minGap = spec.minGap;
    v.headway = spec.headway * rng_.range(0.85, 1.15);
    v.speedFactor = std::clamp(rng_.normal(1.0, 0.08), 0.8, 1.2);
    v.origin = o;
    v.destination = d;
    v.route = std::move(r.links);
    v.turns = std::move(r.turns);
    v.targetS = r.targetS;
    v.freeFlowTime = r.freeFlowTime;
    v.element = net_.links[v.route[0]].firstLane;   // start in the kerb lane
    v.s = std::max(r.startS, v.length);
    return true;
}

void Simulation::insertPending() {
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (tryInsert(it->v)) {
            it = pending_.erase(it);
        } else if (time_ - it->since > 120) {   // could not get onto the road for 2 minutes
            ++dropped_;
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
}

bool Simulation::tryInsert(Vehicle& v) {
    const double laneLen = net_.lanes[v.element].length;
    if (v.s > laneLen - 1) v.s = std::max(v.length, laneLen - 1);
    if (v.route.size() == 1 && v.targetS <= v.s) return false;

    // Pull out only into a safe gap: room ahead, and anyone behind can stop comfortably.
    for (int idx : elemVehicles_[v.element]) {
        const Vehicle& o = vehicles_[idx];
        if (o.s >= v.s) {
            if (o.s - o.length - v.s < 3) return false;
        } else {
            double need = v.length + 2.0 + o.v * 1.0 + o.v * o.v / (2 * 3.0);
            if (v.s - o.s < need) return false;
        }
    }

    int slot;
    if (!freeSlots_.empty()) { slot = freeSlots_.back(); freeSlots_.pop_back(); }
    else { slot = static_cast<int>(vehicles_.size()); vehicles_.emplace_back(); }
    v.id = nextId_++;
    v.alive = true;
    v.startTime = time_;
    vehicles_[slot] = std::move(v);
    slotOf_[vehicles_[slot].id] = slot;
    insertSorted(vehicles_[slot].element, slot);
    ++spawned_;
    ++active_;
    return true;
}

// ------------------------------------------------------------------------------------------
// Element lists: vehicles on each lane/connector, front (largest s) first

void Simulation::insertSorted(int element, int slot) {
    auto& list = elemVehicles_[element];
    double s = vehicles_[slot].s;
    auto pos = std::find_if(list.begin(), list.end(), [&](int o) { return vehicles_[o].s < s; });
    list.insert(pos, slot);
}

void Simulation::eraseFrom(int element, int slot) {
    auto& list = elemVehicles_[element];
    auto it = std::find(list.begin(), list.end(), slot);
    if (it != list.end()) list.erase(it);
}

// ------------------------------------------------------------------------------------------
// Lanes

// Is this lane right for what the vehicle does at the end of the link?
bool Simulation::laneSuits(int link, int laneIndex, int routeIndex, const Vehicle& v) const {
    const Link& L = net_.links[link];
    if (static_cast<size_t>(routeIndex) + 1 >= v.route.size()) return laneIndex == 0;   // arriving: kerb lane
    switch (v.turns[routeIndex]) {
        case Turn::Near: return laneIndex == 0;
        case Turn::Far:
        case Turn::UTurn: return laneIndex == L.lanes - 1;
        case Turn::Straight: return true;
    }
    return true;
}

void Simulation::laneChanges() {
    for (int e = 0; e < static_cast<int>(net_.lanes.size()); ++e) {
        if (elemVehicles_[e].empty()) continue;
        const Lane& ln = net_.lanes[e];
        const Link& L = net_.links[ln.link];
        if (L.lanes < 2) continue;
        std::vector<int> list = elemVehicles_[e];
        for (int slot : list) {
            Vehicle& v = vehicles_[slot];
            if (v.laneChangeCooldown > 0 || v.element != e) continue;
            if (laneSuits(ln.link, ln.index, static_cast<int>(v.ri), v)) continue;
            if (ln.length - v.s < 8) continue;   // too late; the vehicle uses an awkward-lane connector instead
            bool wantKerb = onFinalLink(v) || v.turns[v.ri] == Turn::Near;
            int target = ln.index + (wantKerb ? -1 : 1);
            if (target < 0 || target >= L.lanes) continue;
            tryLaneChange(slot, L.firstLane + target);
        }
    }
}

bool Simulation::tryLaneChange(int slot, int targetLane) {
    Vehicle& v = vehicles_[slot];
    // Find the would-be leader and follower in the target lane.
    const Vehicle* leader = nullptr;
    const Vehicle* follower = nullptr;
    for (int o : elemVehicles_[targetLane]) {
        const Vehicle& w = vehicles_[o];
        if (w.s >= v.s) leader = &w;
        else if (!follower) follower = &w;
    }
    if (leader && leader->s - leader->length - v.s < 2.0 + v.v * 0.5) return false;
    if (follower) {
        double gap = v.s - v.length - follower->s;
        if (gap < 2.0 + follower->v * 0.8 + std::max(0.0, follower->v - v.v) * 1.5) return false;
    }

    if (v.claim >= 0) releaseClaim(v);
    double oldOffset = net_.lanes[v.element].offset;
    eraseFrom(v.element, slot);
    v.element = targetLane;
    insertSorted(targetLane, slot);
    v.lat += oldOffset - net_.lanes[targetLane].offset;   // ease sideways instead of jumping
    v.laneChangeCooldown = 3.0;
    v.stopLineSince = -1;
    return true;
}

// ------------------------------------------------------------------------------------------
// Junctions

int Simulation::chooseConnector(const Vehicle& v) const {
    if (onFinalLink(v) || net_.isConnector(v.element)) return -1;
    const int next = v.route[v.ri + 1];
    const Link& N = net_.links[next];
    int best = -1;
    double bestScore = -1e18;
    for (int c : net_.lanes[v.element].out) {
        const Connector& con = net_.connectors[c];
        if (con.toLink != next) continue;
        double score = con.preferred ? 10.0 : 0.0;
        int outIndex = net_.lanes[con.toLane].index;
        if (laneSuits(next, outIndex, static_cast<int>(v.ri + 1), v)) score += 5.0;
        score -= 0.2 * static_cast<double>(elemVehicles_[con.toLane].size());
        (void)N;
        if (score > bestScore) { bestScore = score; best = c; }
    }
    return best;
}

bool Simulation::spaceAfter(const Vehicle& v, int connector) const {
    int lane = net_.connectors[connector].toLane;
    double free = net_.lanes[lane].length;
    int back = backOf(lane);
    if (back >= 0) free = vehicles_[back].s - vehicles_[back].length;
    free -= reserved_[lane];
    return free >= v.length + v.minGap + 0.5;
}

bool Simulation::conflictingTraffic(const Vehicle& me, int connector, double myTta, double window, bool onlyMajor, bool tieBreak) const {
    const Connector& c = net_.connectors[connector];
    const Junction& J = net_.junctions[c.node];
    for (int cc : c.conflicts) {
        const Connector& o = net_.connectors[cc];
        if (onlyMajor && net_.rank(o.fromLink) < J.maxRank) continue;
        int f = frontOf(o.fromLane);
        if (f < 0) continue;
        const Vehicle& other = vehicles_[f];
        if (other.id == me.id || other.claim >= 0) continue;          // claims already block via occupancy
        if (other.ri + 1 >= other.route.size() || other.route[other.ri + 1] != o.toLink) continue;
        double d = net_.lanes[o.fromLane].length - other.s;
        if (other.v < 0.3 && d < 3.0) {
            // The other vehicle is waiting at its line.
            if (onlyMajor) continue;
            if (other.stopLineSince >= 0 && me.stopLineSince >= 0 &&
                (other.stopLineSince < me.stopLineSince || (other.stopLineSince == me.stopLineSince && other.id < me.id)))
                return true;
            continue;
        }
        double tta = d / std::max(other.v, 0.5);
        if (tta >= window) continue;
        if (!tieBreak) return true;
        if (tta < myTta - 0.3 || (std::fabs(tta - myTta) <= 0.3 && other.id < me.id)) return true;
    }
    return false;
}

bool Simulation::canEnter(const Vehicle& v, int connector, double dist) const {
    const Connector& c = net_.connectors[connector];
    for (int cc : c.conflicts)
        if (occupancy_[cc] > 0) return false;          // a crossing movement is in use
    if (!spaceAfter(v, connector)) return false;        // never block the box

    const Junction& J = net_.junctions[c.node];
    const double tta = dist / std::max(v.v, 0.5);
    const bool waitedLong = v.stopLineSince >= 0 && time_ - v.stopLineSince > 20.0;

    switch (J.control) {
        case JunctionControl::Signal: {
            LightState st = lightFor(c.fromLink);
            if (st == LightState::Green) return true;
            if (v.kind == VehicleKind::Ambulance && v.v < 4) return true;   // edges through a red when the box is clear
            if (st == LightState::Amber) return dist < v.v * v.v / (2 * v.decel) * 0.8 + 1.0;   // too close to stop
            return false;
        }
        case JunctionControl::AllWayStop: {
            if (v.v > 0.5 || dist > 3.0 || v.stopLineSince < 0 || time_ - v.stopLineSince < 0.8) return false;
            // First come, first served.
            for (int cc : c.conflicts) {
                int f = frontOf(net_.connectors[cc].fromLane);
                if (f < 0) continue;
                const Vehicle& o = vehicles_[f];
                if (o.intended != cc || o.stopLineSince < 0) continue;
                if (o.stopLineSince < v.stopLineSince || (o.stopLineSince == v.stopLineSince && o.id < v.id)) return false;
            }
            return true;
        }
        case JunctionControl::Priority: {
            if (J.equalRanks) {   // no major road: whoever gets there first
                if (dist > 6 || v.v > 6) return false;
                return waitedLong || !conflictingTraffic(v, connector, tta, 4.0, false, true);
            }
            bool major = net_.rank(c.fromLink) == J.maxRank;
            if (major) {
                if (c.turn == Turn::Far || c.turn == Turn::UTurn)   // turning across oncoming traffic
                    return waitedLong || !conflictingTraffic(v, connector, tta, 3.5, true, false);
                return true;
            }
            if (dist > 4 || v.v > 5) return false;                  // minor road: slow down and look
            return waitedLong || !conflictingTraffic(v, connector, tta, 4.5, false, false);
        }
        case JunctionControl::None:
            return true;
    }
    return true;
}

void Simulation::claim(Vehicle& v, int connector) {
    v.claim = connector;
    occupancy_[connector]++;
    reserved_[net_.connectors[connector].toLane] += v.length + v.minGap;
}

void Simulation::releaseClaim(Vehicle& v) {
    if (v.claim < 0) return;
    occupancy_[v.claim]--;
    reserved_[net_.connectors[v.claim].toLane] -= v.length + v.minGap;
    v.claim = -1;
}

void Simulation::claims() {
    for (int e = 0; e < static_cast<int>(net_.lanes.size()); ++e) {
        int slot = frontOf(e);
        if (slot < 0) continue;
        Vehicle& v = vehicles_[slot];
        if (onFinalLink(v)) continue;
        const double dist = net_.lanes[e].length - v.s;

        if (dist < 3.0 && v.v < 0.5) {
            if (v.stopLineSince < 0) v.stopLineSince = time_;
        }

        if (v.claim >= 0) {
            // At a signal, give the slot back if the light changed and the vehicle can still stop.
            const Connector& c = net_.connectors[v.claim];
            if (net_.junctions[c.node].control == JunctionControl::Signal) {
                LightState st = lightFor(c.fromLink);
                double stopDist = v.v * v.v / (2 * v.decel);
                double hardStop = v.v * v.v / (2 * 6.0);
                if ((st == LightState::Red && dist > hardStop + 0.5) || (st == LightState::Amber && dist > stopDist + 2.0))
                    releaseClaim(v);
            }
            continue;
        }

        int c = chooseConnector(v);
        v.intended = c;
        if (c < 0) continue;
        double claimDist = std::max(12.0, v.v * v.v / (2 * v.decel) + v.v * 2.0 + 8.0);
        if (dist > claimDist) continue;
        if (canEnter(v, c, dist)) {
            claim(v, c);
            v.stopLineSince = -1;
        }
    }
}

// ------------------------------------------------------------------------------------------
// Car following (Intelligent Driver Model)

double Simulation::idm(const Vehicle& v, double desired, double gap, double leaderSpeed, bool hasLeader) const {
    desired = std::max(desired, 0.1);
    double acc = v.accel * (1.0 - std::pow(std::min(v.v / desired, 2.0), 4));
    if (hasLeader) {
        double dv = v.v - leaderSpeed;
        double sStar = v.minGap + std::max(0.0, v.v * v.headway + v.v * dv / (2.0 * std::sqrt(v.accel * v.decel)));
        double ratio = sStar / std::max(gap, 0.05);
        acc -= v.accel * ratio * ratio;
    }
    return std::max(acc, -9.0);
}

void Simulation::accelerations() {
    for (int e = 0; e < net_.elementCount(); ++e) {
        const auto& list = elemVehicles_[e];
        const double len = net_.elementLength(e);
        for (size_t k = 0; k < list.size(); ++k) {
            Vehicle& v = vehicles_[list[k]];
            double desired = std::min(net_.elementSpeed(e) * v.speedFactor, v.maxSpeed);
            double gap = 1e9, leaderSpeed = 0;
            bool hasLeader = false;

            auto leaderOn = [&](int element, double offset) {
                int b = backOf(element);
                if (b < 0) return false;
                const Vehicle& l = vehicles_[b];
                gap = offset + l.s - l.length;
                leaderSpeed = l.v;
                hasLeader = true;
                return true;
            };

            if (k > 0) {
                const Vehicle& l = vehicles_[list[k - 1]];
                gap = l.s - l.length - v.s;
                leaderSpeed = l.v;
                hasLeader = true;
            } else {
                double dist = len - v.s;
                if (!net_.isConnector(e)) {
                    if (onFinalLink(v)) {
                        // Ease off before pulling in at the destination.
                        desired = std::min(desired, std::sqrt(9.0 + 2.0 * v.decel * std::max(0.0, v.targetS - v.s)));
                    } else if (v.claim < 0) {
                        gap = dist;                    // stop at the line
                        leaderSpeed = 0;
                        hasLeader = true;
                    } else {
                        const Connector& c = net_.connectors[v.claim];
                        desired = std::min(desired, std::sqrt(c.speedLimit * c.speedLimit + 2.0 * v.decel * dist));
                        int ce = net_.connectorElement(v.claim);
                        if (!leaderOn(ce, dist)) leaderOn(c.toLane, dist + c.length);
                    }
                } else {
                    leaderOn(net_.connectors[net_.connectorIndex(e)].toLane, dist);
                }
            }
            v.a = idm(v, desired, gap, leaderSpeed, hasLeader);
        }
    }
}

// ------------------------------------------------------------------------------------------
// Movement

void Simulation::move(double dt) {
    for (int slot = 0; slot < static_cast<int>(vehicles_.size()); ++slot) {
        Vehicle& v = vehicles_[slot];
        if (!v.alive) continue;

        double vNew = v.v + v.a * dt;
        double ds;
        if (vNew < 0) {   // comes to a stop inside this step
            ds = v.a < 0 ? v.v * v.v / (-2.0 * v.a) : 0.0;
            vNew = 0;
        } else {
            ds = (v.v + vNew) * 0.5 * dt;
        }
        v.v = vNew;
        v.s += ds;
        v.lat *= std::exp(-dt / 0.7);
        if (std::fabs(v.lat) < 0.01) v.lat = 0;
        v.laneChangeCooldown = std::max(0.0, v.laneChangeCooldown - dt);

        // Waiting and stop counting
        bool nowStopped = v.v < 0.5;
        if (nowStopped) v.waitTime += dt;
        if (nowStopped && !v.stopped) ++v.stops;
        v.stopped = nowStopped;
        v.stuckTime = v.v < 0.1 ? v.stuckTime + dt : 0.0;

        // Move onto the next element(s)
        while (true) {
            int e = v.element;
            double len = net_.elementLength(e);
            if (v.s <= len) break;
            if (!net_.isConnector(e)) {
                if (onFinalLink(v)) break;   // arrival is handled below
                if (v.claim < 0) { v.s = len; v.v = 0; break; }   // should not happen: the stop line holds it
                int ce = net_.connectorElement(v.claim);
                eraseFrom(e, slot);
                v.s -= len;
                v.element = ce;
                v.claim = -1;                // the reservation is now physical occupancy
                v.lat = 0;
                insertSorted(ce, slot);
            } else {
                const Connector& c = net_.connectors[net_.connectorIndex(e)];
                eraseFrom(e, slot);
                occupancy_[c.id]--;
                reserved_[c.toLane] -= v.length + v.minGap;
                v.s -= len;
                v.element = c.toLane;
                v.ri++;
                v.stopLineSince = -1;
                insertSorted(c.toLane, slot);
            }
        }

        if (onFinalLink(v) && !net_.isConnector(v.element) && v.s >= v.targetS) {
            removeVehicle(slot, true);
        } else if (v.stuckTime > kStuckLimit) {
            removeVehicle(slot, false);
        }
    }
}

void Simulation::removeVehicle(int slot, bool arrived) {
    Vehicle& v = vehicles_[slot];
    releaseClaim(v);
    if (net_.isConnector(v.element)) {
        const Connector& c = net_.connectors[net_.connectorIndex(v.element)];
        occupancy_[c.id]--;
        reserved_[c.toLane] -= v.length + v.minGap;
    }
    eraseFrom(v.element, slot);
    if (arrived) {
        ++arrived_;
        double duration = time_ - v.startTime;
        arrivedKind_[static_cast<int>(v.kind)]++;
        tripKind_[static_cast<int>(v.kind)] += duration;
        recent_.push_back({duration, std::max(0.0, duration - v.freeFlowTime)});
        if (recent_.size() > 400) recent_.pop_front();
    } else {
        ++teleported_;
    }
    slotOf_.erase(v.id);
    v.alive = false;
    v.route.clear();
    v.turns.clear();
    freeSlots_.push_back(slot);
    --active_;
}

// ------------------------------------------------------------------------------------------
// Output

void Simulation::vehiclePose(const Vehicle& v, Vec2& pos, double& angle) const {
    double centre = v.s - v.length * 0.5;   // s is the front bumper
    net_.pose(v.element, std::max(0.0, centre), pos, angle);
    if (!net_.isConnector(v.element)) pos += net_.links[net_.lanes[v.element].link].normal * v.lat;
}

void Simulation::writeFrame(std::string& out, uint32_t cityVersion) const {
    out.clear();
    out.reserve(32 + static_cast<size_t>(active_) * 20 + signals_.size() * 40);
    put<uint8_t>(out, 1);
    put<uint8_t>(out, 0);
    put<uint16_t>(out, 0);
    put<uint32_t>(out, cityVersion);
    put<double>(out, kStartClock + time_);
    put<uint32_t>(out, static_cast<uint32_t>(active_));
    for (const auto& v : vehicles_) {
        if (!v.alive) continue;
        Vec2 p;
        double angle;
        vehiclePose(v, p, angle);
        uint8_t flags = 0;
        if (v.a < -1.0) flags |= 1;          // braking
        if (v.stopped) flags |= 2;           // waiting
        if (v.id == tracked_) flags |= 4;
        put<uint32_t>(out, v.id);
        put<float>(out, static_cast<float>(p.x));
        put<float>(out, static_cast<float>(p.y));
        put<float>(out, static_cast<float>(angle));
        put<uint8_t>(out, static_cast<uint8_t>(v.kind));
        put<uint8_t>(out, flags);
        put<uint16_t>(out, static_cast<uint16_t>(std::min(65535.0, v.v * 100.0)));
    }
    uint32_t count = 0;
    for (const auto& s : signals_) count += static_cast<uint32_t>(s.phaseLinks.size());
    put<uint32_t>(out, count);
    for (const auto& s : signals_) {
        for (size_t p = 0; p < s.phaseLinks.size(); ++p) {
            put<uint32_t>(out, static_cast<uint32_t>(s.node));
            put<uint32_t>(out, static_cast<uint32_t>(net_.links[s.phaseLinks[p]].edge));
            put<uint8_t>(out, static_cast<uint8_t>(active(s).state(static_cast<int>(p))));
        }
    }
}

void Simulation::writeFlow(std::string& out, uint32_t cityVersion) const {
    out.clear();
    put<uint8_t>(out, 2);
    put<uint8_t>(out, 0);
    put<uint16_t>(out, 0);
    put<uint32_t>(out, cityVersion);
    put<uint32_t>(out, static_cast<uint32_t>(net_.links.size()));
    for (const auto& L : net_.links) {
        double sum = 0;
        int n = 0;
        for (int k = 0; k < L.lanes; ++k)
            for (int slot : elemVehicles_[L.firstLane + k]) { sum += vehicles_[slot].v; ++n; }
        uint8_t value = 255;   // no vehicles: nothing to report
        if (n > 0) value = static_cast<uint8_t>(std::clamp(sum / n / L.speedLimit, 0.0, 1.0) * 200.0);
        put<uint32_t>(out, static_cast<uint32_t>(L.edge));
        put<uint8_t>(out, static_cast<uint8_t>(L.from == city_->edges[L.edge].a ? 0 : 1));
        put<uint8_t>(out, value);
    }
}

std::string Simulation::statsJson() const {
    double speedSum = 0;
    int stopped = 0;
    int waitAt[7] = {0, 0, 0, 0, 0, 0, 0};   // none, priority, all-way stop, signal, inside junction, arriving, behind lane-change
    for (const auto& v : vehicles_) {
        if (!v.alive) continue;
        speedSum += v.v;
        if (!v.stopped) continue;
        ++stopped;
        if (net_.isConnector(v.element)) waitAt[4]++;
        else if (onFinalLink(v)) waitAt[5]++;
        else waitAt[static_cast<int>(net_.junctions[net_.links[net_.lanes[v.element].link].to].control)]++;
    }
    double tripSum = 0, delaySum = 0;
    for (const auto& r : recent_) { tripSum += r.duration; delaySum += r.delay; }
    const double nRecent = std::max<double>(1.0, static_cast<double>(recent_.size()));

    JsonWriter w;
    w.beginObject();
    w.field("type", "stats");
    w.field("clock", kStartClock + time_);
    w.field("simTime", time_);
    w.field("vehicles", active_);
    w.field("pending", static_cast<int>(pending_.size()));
    w.field("spawned", static_cast<long long>(spawned_));
    w.field("arrived", static_cast<long long>(arrived_));
    w.field("removedStuck", static_cast<long long>(teleported_));
    w.field("dropped", static_cast<long long>(dropped_));
    w.field("routeFailures", static_cast<long long>(routeFailures_));
    w.field("avgSpeedKmh", active_ ? speedSum / active_ * 3.6 : 0.0);
    w.field("stoppedShare", active_ ? static_cast<double>(stopped) / active_ : 0.0);
    w.field("avgTripMin", recent_.empty() ? 0.0 : tripSum / nRecent / 60.0);
    w.field("avgDelayMin", recent_.empty() ? 0.0 : delaySum / nRecent / 60.0);
    w.field("volume", volume_);
    w.field("defaultVolume", defaultVolume_);
    w.field("signals", static_cast<int>(signals_.size()));
    w.field("controller", mode_ == 1 ? "Smart priority" : "Fixed-time");
    w.field("controllerMode", mode_);
    auto avgMin = [&](VehicleKind k) {
        int i = static_cast<int>(k);
        return arrivedKind_[i] ? tripKind_[i] / arrivedKind_[i] / 60.0 : 0.0;
    };
    w.field("ambulanceTrips", static_cast<long long>(arrivedKind_[static_cast<int>(VehicleKind::Ambulance)]));
    w.field("ambulanceAvgMin", avgMin(VehicleKind::Ambulance));
    w.field("schoolBusTrips", static_cast<long long>(arrivedKind_[static_cast<int>(VehicleKind::SchoolBus)]));
    w.field("schoolBusAvgMin", avgMin(VehicleKind::SchoolBus));
    w.key("waitingAt").beginObject()
        .field("signal", waitAt[3]).field("stopSign", waitAt[2]).field("giveWay", waitAt[1])
        .field("bend", waitAt[0]).field("insideJunction", waitAt[4]).field("arriving", waitAt[5])
        .endObject();

    auto it = slotOf_.find(tracked_);
    if (tracked_ != 0 && it != slotOf_.end()) {
        const Vehicle& v = vehicles_[it->second];
        const Building& from = city_->buildings[v.origin];
        const Building& to = city_->buildings[v.destination];
        auto place = [&](const Building& b) {
            std::string what = b.name.empty() ? std::string(toString(b.type)) : b.name;
            if (b.district >= 0) what += ", " + city_->districts[b.district].name;
            return what;
        };
        w.key("tracked").beginObject();
        w.field("id", static_cast<int>(v.id));
        w.field("kind", specOf(v.kind).name);
        w.field("speedKmh", v.v * 3.6);
        w.field("from", place(from));
        w.field("to", place(to));
        w.field("elapsedS", time_ - v.startTime);
        w.field("freeFlowS", v.freeFlowTime);
        w.field("waitS", v.waitTime);
        w.field("stops", v.stops);
        w.field("progress", static_cast<double>(v.ri) / std::max<size_t>(1, v.route.size()));
        // Remaining route as a polyline: from the vehicle along the rest of its links.
        Vec2 p;
        double ang;
        vehiclePose(v, p, ang);
        w.key("route").beginArray();
        w.value(p.x, 1).value(p.y, 1);
        for (size_t k = v.ri; k < v.route.size(); ++k) {
            const Lane& ln = net_.lanes[net_.links[v.route[k]].firstLane];
            if (k + 1 == v.route.size()) {
                Vec2 t = ln.start + ln.dir * v.targetS;
                w.value(t.x, 1).value(t.y, 1);
            } else {
                w.value(ln.end.x, 1).value(ln.end.y, 1);
            }
        }
        w.endArray();
        w.endObject();
    }
    w.endObject();
    return w.str();
}

}  // namespace sim
