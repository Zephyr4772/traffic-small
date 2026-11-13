#pragma once
// Route finding: A* over directed links, with junction delays and turn costs.

#include <cstdint>
#include <vector>

#include "sim/Network.h"

namespace sim {

struct Route {
    std::vector<int> links;
    std::vector<Turn> turns;   // turns[k] = turn from links[k] into links[k+1]
    double startS = 0;         // where the trip starts on the first link's lane
    double targetS = 0;        // where it ends on the last link's lane
    double freeFlowTime = 0;   // seconds, with empty roads and green lights
};

class Router {
public:
    Router(const Network& net, const City& city) : net_(net), city_(city) {}

    // salt varies the route slightly per trip, so not every driver picks the identical road.
    bool route(const Building& from, const Building& to, uint32_t salt, Route& out) const;

private:
    double junctionCost(int inLink, Turn turn) const;
    double positionOnLink(int link, double edgeT) const;

    const Network& net_;
    const City& city_;
};

}  // namespace sim
