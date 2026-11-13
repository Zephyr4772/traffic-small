#include "world/City.h"

#include <algorithm>

namespace sim {

const char* toString(RoadClass c) {
    switch (c) {
        case RoadClass::Local: return "local";
        case RoadClass::Collector: return "collector";
        case RoadClass::Arterial: return "arterial";
    }
    return "?";
}

const char* toString(JunctionControl c) {
    switch (c) {
        case JunctionControl::None: return "none";
        case JunctionControl::Priority: return "priority";
        case JunctionControl::AllWayStop: return "all-way stop";
        case JunctionControl::Signal: return "signal";
    }
    return "?";
}

const char* toString(Zone z) {
    switch (z) {
        case Zone::Water: return "water";
        case Zone::Park: return "park";
        case Zone::Residential: return "residential";
        case Zone::Apartments: return "apartments";
        case Zone::Commercial: return "commercial";
        case Zone::Downtown: return "downtown";
        case Zone::Industrial: return "industrial";
        case Zone::Civic: return "civic";
    }
    return "?";
}

const char* toString(BuildingType t) {
    switch (t) {
        case BuildingType::House: return "house";
        case BuildingType::Apartment: return "apartment";
        case BuildingType::Shop: return "shop";
        case BuildingType::Office: return "office";
        case BuildingType::Warehouse: return "warehouse";
        case BuildingType::Factory: return "factory";
        case BuildingType::School: return "school";
        case BuildingType::Hospital: return "hospital";
        case BuildingType::FireStation: return "fire station";
        case BuildingType::PoliceStation: return "police station";
        case BuildingType::BusDepot: return "bus depot";
        case BuildingType::Mall: return "mall";
        case BuildingType::Stadium: return "stadium";
    }
    return "?";
}

CityConfig CityConfig::preset(const std::string& name) {
    CityConfig c;
    c.scale = name;
    if (name == "demo") {   // tiny showcase city: a handful of junctions
        c.sizeM = 700; c.blockM = 230; c.arterialSpacing = 3; c.river = false; c.irregular = 0.0; c.jitter = 0.08;
    } else if (name == "small") {
        c.sizeM = 1600; c.blockM = 150; c.arterialSpacing = 5;
    } else if (name == "large") {
        c.sizeM = 3600; c.blockM = 170; c.arterialSpacing = 5;
    } else {
        c.scale = name == "custom" ? "custom" : "medium";
        c.sizeM = 2400; c.blockM = 160; c.arterialSpacing = 5;
    }
    return c;
}

double City::junctionRadius(int node) const {
    const Node& n = nodes[node];
    if (n.edges.size() < 3) return 0.0;
    double widest = 0;
    for (int e : n.edges) widest = std::max(widest, edges[e].halfWidth());
    return widest + 1.5;
}

double City::stopDistance(int node) const {
    const Node& n = nodes[node];
    if (n.edges.size() >= 3) {
        double r = junctionRadius(node);
        switch (n.control) {
            case JunctionControl::Signal: return r + 4.2;       // room for the zebra crossing
            case JunctionControl::AllWayStop: return r + 0.8;
            default: return r + 0.3;
        }
    }
    if (n.edges.size() == 2) {
        // A bend needs room for lanes to curve; a straight continuation barely any.
        const Edge& e1 = edges[n.edges[0]];
        const Edge& e2 = edges[n.edges[1]];
        Vec2 d1 = (nodes[e1.other(node)].pos - n.pos).normalized();
        Vec2 d2 = (nodes[e2.other(node)].pos - n.pos).normalized();
        if (dot(d1, d2) > -0.94) return std::max(e1.halfWidth(), e2.halfWidth()) + 1.0;
        return 0.5;
    }
    return 1.0;  // dead end
}

CityStats City::stats() const {
    CityStats s;
    s.nodes = static_cast<int>(nodes.size());
    s.edges = static_cast<int>(edges.size());
    for (const auto& e : edges) {
        double km = e.length / 1000.0;
        s.roadKm += km;
        if (e.cls == RoadClass::Arterial) s.arterialKm += km;
        else if (e.cls == RoadClass::Collector) s.collectorKm += km;
        else s.localKm += km;
        if (e.bridge) s.bridges++;
    }
    for (const auto& n : nodes) {
        if (n.control == JunctionControl::Signal) s.signals++;
        else if (n.control == JunctionControl::AllWayStop) s.allWayStops++;
        else if (n.control == JunctionControl::Priority) s.priorityJunctions++;
    }
    for (const auto& b : buildings) {
        s.buildings++;
        s.population += b.residents;
        s.jobs += b.jobs;
        switch (b.type) {
            case BuildingType::School: s.schools++; s.schoolSeats += b.capacity; break;
            case BuildingType::Hospital: s.hospitals++; s.hospitalBeds += b.capacity; break;
            case BuildingType::FireStation: s.fireStations++; break;
            case BuildingType::PoliceStation: s.policeStations++; break;
            case BuildingType::BusDepot: s.busDepots++; break;
            case BuildingType::Mall: s.malls++; break;
            case BuildingType::Stadium: s.stadiums++; break;
            default: break;
        }
    }
    s.districts = static_cast<int>(districts.size());
    s.areaKm2 = width * height / 1e6;
    return s;
}

}  // namespace sim
