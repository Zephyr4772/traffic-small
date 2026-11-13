#pragma once
// The drivable network built from the city's roads.
//
//   Link       one direction of a road, between the stop lines at either end
//   Lane       one lane of a link; lane 0 is the kerb lane (the left lane in India)
//   Connector  a path through a junction from one incoming lane to one outgoing lane
//
// Vehicles always sit on an "element": a lane or a connector. Connectors that cross or merge
// are marked as conflicting, which is what keeps vehicles from colliding inside junctions.

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "world/City.h"

namespace sim {

// Near = turning towards the kerb (left in India), Far = turning across oncoming traffic.
enum class Turn : uint8_t { Straight = 0, Near = 1, Far = 2, UTurn = 3 };

struct Lane {
    int id = -1;
    int link = -1;
    int index = 0;                   // 0 = kerb lane
    Vec2 start, end, dir;
    double length = 0;
    double offset = 0;               // distance from the road centre line, along the link's left normal
    std::vector<int> out;            // connectors leaving this lane
};

struct Link {
    int id = -1, edge = -1, from = -1, to = -1;
    Vec2 dir, normal;                // normal = left of the direction of travel
    double length = 0;               // stop line to stop line
    double startTrim = 0, endTrim = 0;
    int firstLane = 0, lanes = 1;
    double speedLimit = 10;
    RoadClass cls = RoadClass::Local;
    std::vector<std::pair<int, Turn>> next;   // links reachable at the end, and the turn needed
};

struct Connector {
    int id = -1, node = -1;
    int fromLane = -1, toLane = -1, fromLink = -1, toLink = -1;
    Turn turn = Turn::Straight;
    bool preferred = true;           // false = the vehicle is in an awkward lane for this move
    std::vector<Vec2> pts;           // sampled curve
    std::vector<double> cum;         // distance along the curve at each sample
    double length = 0;
    double speedLimit = 5;
    std::vector<int> conflicts;      // connectors that cross or merge with this one
};

struct Junction {
    int node = -1;
    JunctionControl control = JunctionControl::None;
    std::vector<int> inLinks, outLinks, connectors;
    int maxRank = 1;                 // highest road class meeting here
    bool equalRanks = true;          // all roads the same class (no major road)
};

class Network {
public:
    void build(const City& city);

    std::vector<Link> links;
    std::vector<Lane> lanes;
    std::vector<Connector> connectors;
    std::vector<Junction> junctions;              // indexed by node id
    std::vector<std::array<int, 2>> edgeLinks;    // [edge] -> {link a->b, link b->a}
    bool driveLeft = true;

    // An element id is a lane id, or lanes.size() + connector id.
    int elementCount() const { return static_cast<int>(lanes.size() + connectors.size()); }
    bool isConnector(int e) const { return e >= static_cast<int>(lanes.size()); }
    int connectorElement(int c) const { return static_cast<int>(lanes.size()) + c; }
    int connectorIndex(int e) const { return e - static_cast<int>(lanes.size()); }
    double elementLength(int e) const;
    double elementSpeed(int e) const;

    // Position and heading at distance s along an element.
    void pose(int e, double s, Vec2& pos, double& angle) const;

    Turn turnBetween(int inLink, int outLink) const;
    int rank(int link) const { return static_cast<int>(links[link].cls); }

private:
    Turn classify(Vec2 dIn, Vec2 dOut) const;
    void addConnector(int node, const Link& in, int inLane, const Link& out, int outLane, Turn turn, bool preferred);
    void findConflicts();
};

}  // namespace sim
