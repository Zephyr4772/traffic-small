#pragma once
// The static city: road network, land blocks, buildings and landmarks.
// Traffic (later milestones) lives on top of this and never changes it.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Geometry.h"

namespace sim {

// ---- Enumerations (the numeric values are also used by the browser, keep them in sync) ----

enum class RoadClass { Local = 1, Collector = 2, Arterial = 3 };
enum class JunctionControl { None = 0, Priority = 1, AllWayStop = 2, Signal = 3 };
enum class Zone { Water = 0, Park, Residential, Apartments, Commercial, Downtown, Industrial, Civic };
enum class BuildingType {
    House = 0, Apartment, Shop, Office, Warehouse, Factory,
    School, Hospital, FireStation, PoliceStation, BusDepot, Mall, Stadium
};
enum class FeatureType { Park = 0, SportsField, Parking, Plaza, Trees };

const char* toString(RoadClass c);
const char* toString(JunctionControl c);
const char* toString(Zone z);
const char* toString(BuildingType t);

// ---- Generation settings (the "scale" of the city) ----

struct CityConfig {
    uint64_t seed = 1;
    std::string scale = "medium";
    double sizeM = 2400;        // city width/height before warping, metres
    double blockM = 160;        // typical block size, metres
    int arterialSpacing = 5;    // an arterial road every N blocks
    double jitter = 0.18;       // how wobbly local streets are (fraction of a block)
    double irregular = 0.10;    // chance a local street segment is missing
    bool river = true;
    bool driveLeft = true;      // India drives on the left

    static CityConfig preset(const std::string& name);
};

// ---- Road network ----

struct Node {
    int id = -1;
    Vec2 pos;
    int gi = 0, gj = 0;                 // grid line indices (column, row)
    std::vector<int> edges;             // incident edge ids
    JunctionControl control = JunctionControl::None;
};

struct Edge {
    int id = -1;
    int a = -1, b = -1;                 // node ids
    RoadClass cls = RoadClass::Local;
    int lanesPerDir = 1;
    double laneWidth = 3.0;
    double medianWidth = 0.0;
    double speedLimit = 7.0;            // m/s
    bool bridge = false;
    std::string name;
    double length = 0;

    double halfWidth() const { return lanesPerDir * laneWidth + medianWidth * 0.5; }
    int other(int n) const { return n == a ? b : a; }
};

// ---- Land ----

struct Block {
    int id = -1;
    int gi = 0, gj = 0;
    std::vector<Vec2> corners;          // road centre-line corners: TL, TR, BR, BL
    std::array<int, 4> sides{{-1, -1, -1, -1}};  // edge on each side (top, right, bottom, left) or -1
    Zone zone = Zone::Residential;
    int district = -1;
    bool water = false;
};

struct Building {
    int id = -1;
    BuildingType type = BuildingType::House;
    std::vector<Vec2> poly;
    int floors = 1;
    int residents = 0;
    int jobs = 0;
    int capacity = 0;                   // school seats, hospital beds, depot buses, stadium seats
    int block = -1;
    int district = -1;
    int accessEdge = -1;                // the road the entrance faces
    double accessT = 0;                 // position along that edge (0 = node a, 1 = node b)
    int accessSide = 0;                 // +1 / -1: which side of the edge (a->b) the building is on
    std::string name;                   // landmarks only
};

struct Feature {
    FeatureType type = FeatureType::Park;
    std::vector<Vec2> poly;             // for Trees this is a list of tree positions
    bool overBuildings = false;         // drawn on top of buildings (stadium pitch)
};

struct District {
    int id = -1;
    std::string name;
    Vec2 center;
};

struct River {
    std::vector<Vec2> path;
    double width = 0;
    bool exists() const { return path.size() >= 2; }
};

struct CityStats {
    double roadKm = 0, arterialKm = 0, collectorKm = 0, localKm = 0;
    int nodes = 0, edges = 0;
    int signals = 0, allWayStops = 0, priorityJunctions = 0, bridges = 0;
    int buildings = 0, population = 0, jobs = 0, schoolSeats = 0, hospitalBeds = 0;
    int schools = 0, hospitals = 0, fireStations = 0, policeStations = 0, busDepots = 0, malls = 0, stadiums = 0;
    int districts = 0;
    double areaKm2 = 0;
};

class City {
public:
    CityConfig config;
    double width = 0, height = 0;

    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<Block> blocks;
    std::vector<Building> buildings;
    std::vector<Feature> features;
    std::vector<District> districts;
    River river;

    CityStats stats() const;

    // Radius of the open junction box at a node (0 for bends and dead ends).
    double junctionRadius(int node) const;
    // Distance from the node centre to where lanes end (the stop line at controlled junctions).
    // Shared by the traffic network and the renderer so vehicles stop exactly on the painted line.
    double stopDistance(int node) const;
};

}  // namespace sim
