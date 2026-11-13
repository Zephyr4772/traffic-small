#include "world/CityJson.h"

#include <unordered_map>

#include "core/Json.h"

namespace sim {

namespace {

void writePoints(JsonWriter& w, const std::vector<Vec2>& pts) {
    for (const auto& p : pts) {
        w.value(p.x, 1);
        w.value(p.y, 1);
    }
}

}  // namespace

std::string cityToJson(const City& city) {
    JsonWriter w;
    const CityConfig& c = city.config;
    w.beginObject();
    w.key("config").beginObject()
        .field("seed", static_cast<unsigned long long>(c.seed))
        .field("scale", c.scale)
        .field("size", c.sizeM)
        .field("block", c.blockM)
        .field("spacing", c.arterialSpacing)
        .field("jitter", c.jitter)
        .field("irregular", c.irregular)
        .field("river", c.river)
        .field("driveLeft", c.driveLeft)
        .endObject();
    w.field("width", city.width).field("height", city.height);

    // Road names are shared by many edges, so send each once.
    std::vector<std::string> roadNames;
    std::unordered_map<std::string, int> roadIndex;
    for (const auto& e : city.edges)
        if (roadIndex.emplace(e.name, static_cast<int>(roadNames.size())).second) roadNames.push_back(e.name);
    w.key("roadNames").beginArray();
    for (const auto& n : roadNames) w.value(n);
    w.endArray();

    // node: [x, y, control, stopDistance]
    w.key("nodes").beginArray();
    for (const auto& n : city.nodes) {
        w.beginArray().value(n.pos.x, 1).value(n.pos.y, 1).value(static_cast<int>(n.control))
            .value(city.stopDistance(n.id), 2).endArray();
    }
    w.endArray();

    // edge: [a, b, class, lanesPerDir, laneWidth, medianWidth, bridge, nameIndex, speedLimit]
    w.key("edges").beginArray();
    for (const auto& e : city.edges) {
        w.beginArray()
            .value(e.a).value(e.b).value(static_cast<int>(e.cls)).value(e.lanesPerDir)
            .value(e.laneWidth).value(e.medianWidth).value(e.bridge ? 1 : 0)
            .value(roadIndex[e.name]).value(e.speedLimit)
            .endArray();
    }
    w.endArray();

    w.key("river").beginObject().field("width", city.river.width);
    w.key("path").beginArray();
    writePoints(w, city.river.path);
    w.endArray().endObject();

    // block: [zone, district, x0, y0, ... x3, y3]
    w.key("blocks").beginArray();
    for (const auto& b : city.blocks) {
        w.beginArray().value(static_cast<int>(b.zone)).value(b.district);
        writePoints(w, b.corners);
        w.endArray();
    }
    w.endArray();

    // building: [type, floors, residents, jobs, capacity, district, nameIndex, accessEdge, accessT, accessSide, x0, y0, ...]
    std::vector<std::string> names;
    w.key("buildings").beginArray();
    for (const auto& b : city.buildings) {
        int nameIdx = -1;
        if (!b.name.empty()) { nameIdx = static_cast<int>(names.size()); names.push_back(b.name); }
        w.beginArray()
            .value(static_cast<int>(b.type)).value(b.floors).value(b.residents).value(b.jobs).value(b.capacity)
            .value(b.district).value(nameIdx).value(b.accessEdge).value(b.accessT, 3).value(b.accessSide);
        writePoints(w, b.poly);
        w.endArray();
    }
    w.endArray();
    w.key("names").beginArray();
    for (const auto& n : names) w.value(n);
    w.endArray();

    // feature: [type, overBuildings, x0, y0, ...]
    w.key("features").beginArray();
    for (const auto& f : city.features) {
        w.beginArray().value(static_cast<int>(f.type)).value(f.overBuildings ? 1 : 0);
        writePoints(w, f.poly);
        w.endArray();
    }
    w.endArray();

    // district: [name, x, y]
    w.key("districts").beginArray();
    for (const auto& d : city.districts) w.beginArray().value(d.name).value(d.center.x, 1).value(d.center.y, 1).endArray();
    w.endArray();

    CityStats s = city.stats();
    w.key("stats").beginObject()
        .field("areaKm2", s.areaKm2).field("roadKm", s.roadKm).field("arterialKm", s.arterialKm)
        .field("collectorKm", s.collectorKm).field("localKm", s.localKm)
        .field("nodes", s.nodes).field("edges", s.edges)
        .field("signals", s.signals).field("allWayStops", s.allWayStops).field("priorityJunctions", s.priorityJunctions)
        .field("bridges", s.bridges).field("buildings", s.buildings).field("population", s.population).field("jobs", s.jobs)
        .field("schoolSeats", s.schoolSeats).field("hospitalBeds", s.hospitalBeds)
        .field("schools", s.schools).field("hospitals", s.hospitals).field("fireStations", s.fireStations)
        .field("policeStations", s.policeStations).field("busDepots", s.busDepots).field("malls", s.malls)
        .field("stadiums", s.stadiums).field("districts", s.districts)
        .endObject();

    w.endObject();
    return w.str();
}

}  // namespace sim
