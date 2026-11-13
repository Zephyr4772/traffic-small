#pragma once
// Builds a City from a CityConfig. Same config + seed => identical city.
//
// Steps:
//   1. plan which grid lines are arterials, collectors or local streets
//   2. place junctions on a warped, jittered grid
//   3. carve a river (optional) — only arterials and some collectors get bridges
//   4. connect junctions with road segments, drop some local streets for irregularity
//   5. keep the largest connected road network
//   6. decide how each junction is controlled (signal, all-way stop, priority)
//   7. name the roads, split the land into blocks and districts, zone the blocks
//   8. place landmarks (schools, hospitals, stations, depots, mall, stadium)
//   9. subdivide blocks into plots and buildings, link every building to its road

#include <functional>
#include <string>
#include <vector>

#include "core/Rng.h"
#include "world/City.h"

namespace sim {

class CityGenerator {
public:
    City generate(const CityConfig& config);

private:
    struct Wave { double kx, ky, phase, amp; };

    void planLines();
    void placeNodes();
    void makeRiver();
    void connectEdges();
    void pruneDisconnected();
    void assignJunctionControl();
    void nameRoads();
    void makeBlocks();
    void makeDistricts();
    void zoneBlocks();
    void placeLandmarks();
    void buildBlocks();
    void assignAccess();

    void configureEdge(Edge& e, RoadClass cls) const;
    bool crossesWater(Vec2 a, Vec2 b) const;
    double riverDistance(Vec2 p) const;
    Vec2 riverDirectionNear(Vec2 p) const;
    double noise(Vec2 p) const;
    Vec2 blockCenter(const Block& b) const;
    bool blockTouches(const Block& b, RoadClass cls) const;
    std::vector<int> pickSpread(const std::function<bool(const Block&)>& ok, int count);

    void addLots(const Block& block, const std::vector<Vec2>& quad, Zone zone);
    void addPark(const std::vector<Vec2>& quad);
    void layoutLandmark(const Block& block, const std::vector<Vec2>& quad, BuildingType type, const std::string& name);
    Building& addBuilding(BuildingType type, std::vector<Vec2> poly, const Block& block, Zone zone);
    void addFeature(FeatureType type, std::vector<Vec2> poly, bool overBuildings = false);

    CityConfig cfg_;
    Rng rng_;
    City city_;

    int n_ = 0;                 // grid lines per axis
    double block_ = 0;          // actual spacing between grid lines
    std::vector<RoadClass> lineClass_;
    std::vector<std::vector<Vec2>> gridPos_;   // [i][j]
    std::vector<std::vector<int>> nodeAt_;     // [i][j] -> node id or -1
    std::vector<std::vector<int>> hEdge_;      // [i][j] -> edge (i,j)-(i+1,j)
    std::vector<std::vector<int>> vEdge_;      // [i][j] -> edge (i,j)-(i,j+1)
    std::vector<Wave> waves_;
    int industrialSide_ = 0;
    std::vector<int> landmark_;                // per block: BuildingType as int, or -1
    std::vector<std::string> landmarkName_;
};

}  // namespace sim
