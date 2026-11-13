#include "world/CityGenerator.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace sim {

namespace {

constexpr double kPi = 3.14159265358979323846;

const std::vector<std::string> kArterialNames = {
    "Mahatma Gandhi Road", "Nehru Marg", "Netaji Subhash Road", "Sardar Patel Road", "Tilak Marg",
    "Ashoka Road", "Ambedkar Road", "Rajaji Salai", "Station Road", "Lake View Road",
    "Maharana Pratap Road", "Shivaji Marg"};

const std::vector<std::string> kCollectorNames = {
    "Temple Street", "Market Road", "Church Street", "College Road", "Park Street", "Mill Road",
    "Canal Road", "Hospital Road", "Bazaar Street", "Fort Road", "Old Post Office Road", "Court Road",
    "Railway Feeder Road", "Gandhi Bazaar Road", "Library Road", "Tank Bund Road"};

const std::vector<std::string> kDistrictNames = {
    "Shanti Nagar", "Gandhi Nagar", "Rajendra Nagar", "Model Town", "Civil Lines", "Indira Colony",
    "Nehru Colony", "Old Town", "Lake Side", "Vijay Nagar", "Ashok Vihar", "Malviya Nagar",
    "Patel Nagar", "Sarojini Colony", "Kamla Nagar", "Jubilee Hills"};

const std::vector<std::string> kSchoolPrefixes = {
    "Saraswati", "Sunrise", "Green Valley", "St. Mary's", "Little Flower", "Vidya Bharati",
    "Modern", "Lotus", "Bal Bharati", "Holy Cross", "Navodaya", "Gurukul"};

const std::vector<std::string> kSchoolSuffixes = {"Public School", "High School", "Vidya Mandir", "Convent School"};

const std::vector<std::string> kHospitalNames = {
    "City General Hospital", "Lifeline Hospital", "Sanjeevani Hospital", "District Hospital",
    "Arogya Hospital", "Care & Cure Hospital", "Dhanvantari Hospital", "Seva Sadan Hospital"};

std::string ordinal(int n) {
    int mod100 = n % 100;
    const char* suffix = "th";
    if (mod100 < 11 || mod100 > 13) {
        switch (n % 10) {
            case 1: suffix = "st"; break;
            case 2: suffix = "nd"; break;
            case 3: suffix = "rd"; break;
            default: break;
        }
    }
    return std::to_string(n) + suffix;
}

int rank(RoadClass c) { return static_cast<int>(c); }

}  // namespace

// ------------------------------------------------------------------------------------------

City CityGenerator::generate(const CityConfig& config) {
    cfg_ = config;
    rng_.reseed(cfg_.seed * 0x9E3779B97F4A7C15ull + 0x5EED);
    city_ = City();
    city_.config = cfg_;

    planLines();
    placeNodes();
    makeRiver();
    connectEdges();
    pruneDisconnected();
    assignJunctionControl();
    nameRoads();
    makeBlocks();
    makeDistricts();
    zoneBlocks();
    placeLandmarks();
    buildBlocks();
    assignAccess();

    return std::move(city_);
}

// ---- 1. Road hierarchy -------------------------------------------------------------------

void CityGenerator::planLines() {
    n_ = std::max(4, static_cast<int>(std::lround(cfg_.sizeM / cfg_.blockM)) + 1);
    block_ = cfg_.sizeM / (n_ - 1);

    const int sp = std::max(3, cfg_.arterialSpacing);
    const int off = sp / 2;
    lineClass_.assign(n_, RoadClass::Local);
    for (int k = 0; k < n_; ++k) {
        int m = ((k - off) % sp + sp) % sp;
        if (k == 0 || k == n_ - 1) lineClass_[k] = RoadClass::Collector;           // ring road
        else if (m == 0) lineClass_[k] = RoadClass::Arterial;
        else if (m == sp / 2 && k > 1 && k < n_ - 2) lineClass_[k] = RoadClass::Collector;
    }
}

// ---- 2. Junction positions ---------------------------------------------------------------

void CityGenerator::placeNodes() {
    gridPos_.assign(n_, std::vector<Vec2>(n_));

    // A gentle large-scale warp so streets curve, plus per-junction jitter on local streets.
    const double warp = std::min(1.0, cfg_.jitter / 0.18);   // calmer streets when jitter is low (demo city)
    const double ampX = block_ * rng_.range(0.15, 0.4) * warp * warp, ampY = block_ * rng_.range(0.15, 0.4) * warp * warp;
    const double fX = 2 * kPi / (cfg_.sizeM * rng_.range(0.7, 1.4));
    const double fY = 2 * kPi / (cfg_.sizeM * rng_.range(0.7, 1.4));
    const double phX = rng_.range(0, 2 * kPi), phY = rng_.range(0, 2 * kPi);

    for (int i = 0; i < n_; ++i) {
        for (int j = 0; j < n_; ++j) {
            double bx = i * block_, by = j * block_;
            double jx = (lineClass_[i] == RoadClass::Local ? cfg_.jitter : cfg_.jitter * 0.12) * block_ * rng_.range(-0.5, 0.5);
            double jy = (lineClass_[j] == RoadClass::Local ? cfg_.jitter : cfg_.jitter * 0.12) * block_ * rng_.range(-0.5, 0.5);
            gridPos_[i][j] = Vec2(bx + jx + ampX * std::sin(by * fX + phX), by + jy + ampY * std::sin(bx * fY + phY));
        }
    }

    // Shift everything so the city starts at a fixed margin from the origin.
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    for (auto& col : gridPos_)
        for (auto& p : col) {
            minX = std::min(minX, p.x); minY = std::min(minY, p.y);
            maxX = std::max(maxX, p.x); maxY = std::max(maxY, p.y);
        }
    const double margin = 90;
    for (auto& col : gridPos_)
        for (auto& p : col) p = p - Vec2(minX - margin, minY - margin);
    city_.width = maxX - minX + 2 * margin;
    city_.height = maxY - minY + 2 * margin;

    // Smooth noise used for zoning.
    waves_.clear();
    for (int k = 0; k < 4; ++k) {
        double angle = rng_.range(0, 2 * kPi);
        double freq = 2 * kPi / (cfg_.sizeM * rng_.range(0.35, 0.9));
        waves_.push_back({std::cos(angle) * freq, std::sin(angle) * freq, rng_.range(0, 2 * kPi), 0.03 + 0.02 * rng_.uniform()});
    }
}

double CityGenerator::noise(Vec2 p) const {
    double v = 0;
    for (const auto& w : waves_) v += w.amp * std::sin(p.x * w.kx + p.y * w.ky + w.phase);
    return v;
}

// ---- 3. River ----------------------------------------------------------------------------

void CityGenerator::makeRiver() {
    if (!cfg_.river || n_ < 8) return;

    const bool horizontal = rng_.chance(0.5);
    const double along = horizontal ? city_.width : city_.height;
    const double across = horizontal ? city_.height : city_.width;
    const double c0 = across * rng_.range(0.36, 0.64);
    const double a1 = across * rng_.range(0.05, 0.11), f1 = 2 * kPi / (along * rng_.range(0.6, 1.2)), p1 = rng_.range(0, 2 * kPi);
    const double a2 = across * rng_.range(0.015, 0.035), f2 = f1 * rng_.range(2.2, 3.4), p2 = rng_.range(0, 2 * kPi);

    for (double s = -80; s <= along + 80; s += 20) {
        double c = c0 + a1 * std::sin(s * f1 + p1) + a2 * std::sin(s * f2 + p2);
        city_.river.path.push_back(horizontal ? Vec2(s, c) : Vec2(c, s));
    }
    city_.river.width = std::clamp(block_ * 0.42, 40.0, 85.0);
}

double CityGenerator::riverDistance(Vec2 p) const {
    if (!city_.river.exists()) return 1e300;
    return distPointPolyline(p, city_.river.path);
}

Vec2 CityGenerator::riverDirectionNear(Vec2 p) const {
    size_t seg = 0;
    distPointPolyline(p, city_.river.path, &seg);
    return (city_.river.path[seg + 1] - city_.river.path[seg]).normalized();
}

bool CityGenerator::crossesWater(Vec2 a, Vec2 b) const {
    if (!city_.river.exists()) return false;
    const double half = city_.river.width * 0.5;
    for (int k = 0; k <= 12; ++k)
        if (riverDistance(lerp(a, b, k / 12.0)) < half) return true;
    return false;
}

// ---- 4. Road segments --------------------------------------------------------------------

void CityGenerator::configureEdge(Edge& e, RoadClass cls) const {
    e.cls = cls;
    switch (cls) {
        case RoadClass::Arterial:  e.lanesPerDir = 2; e.laneWidth = 3.5;  e.medianWidth = 2.0; e.speedLimit = 50 / 3.6; break;
        case RoadClass::Collector: e.lanesPerDir = 1; e.laneWidth = 3.75; e.medianWidth = 0.0; e.speedLimit = 40 / 3.6; break;
        case RoadClass::Local:     e.lanesPerDir = 1; e.laneWidth = 3.0;  e.medianWidth = 0.0; e.speedLimit = 25 / 3.6; break;
    }
}

void CityGenerator::connectEdges() {
    city_.nodes.clear();
    city_.edges.clear();
    nodeAt_.assign(n_, std::vector<int>(n_, -1));
    for (int j = 0; j < n_; ++j)
        for (int i = 0; i < n_; ++i) {
            Node node;
            node.id = static_cast<int>(city_.nodes.size());
            node.pos = gridPos_[i][j];
            node.gi = i;
            node.gj = j;
            nodeAt_[i][j] = node.id;
            city_.nodes.push_back(node);
        }

    hEdge_.assign(n_, std::vector<int>(n_, -1));
    vEdge_.assign(n_, std::vector<int>(n_, -1));

    auto tryEdge = [&](int i0, int j0, int i1, int j1, RoadClass cls) -> int {
        if (cls == RoadClass::Local && rng_.chance(cfg_.irregular)) return -1;
        Vec2 a = gridPos_[i0][j0], b = gridPos_[i1][j1];
        bool bridge = false;
        if (crossesWater(a, b)) {
            // How parallel is the road to the river here? 0 = straight across, 1 = running alongside.
            double along = std::fabs(dot((b - a).normalized(), riverDirectionNear(lerp(a, b, 0.5))));
            // Arterials always get a bridge unless they run along the river; some collectors get one too.
            bool keep = (cls == RoadClass::Arterial && along < 0.9) ||
                        (cls == RoadClass::Collector && along < 0.6 && rng_.chance(0.5));
            if (!keep) return -1;
            bridge = true;
        }
        Edge e;
        e.id = static_cast<int>(city_.edges.size());
        e.a = nodeAt_[i0][j0];
        e.b = nodeAt_[i1][j1];
        configureEdge(e, cls);
        e.bridge = bridge;
        e.length = distance(a, b);
        city_.edges.push_back(e);
        return e.id;
    };

    for (int j = 0; j < n_; ++j)
        for (int i = 0; i + 1 < n_; ++i) hEdge_[i][j] = tryEdge(i, j, i + 1, j, lineClass_[j]);
    for (int i = 0; i < n_; ++i)
        for (int j = 0; j + 1 < n_; ++j) vEdge_[i][j] = tryEdge(i, j, i, j + 1, lineClass_[i]);
}

// ---- 5. Keep the largest connected network -----------------------------------------------

void CityGenerator::pruneDisconnected() {
    const int N = static_cast<int>(city_.nodes.size());
    std::vector<std::vector<int>> adj(N);
    for (const auto& e : city_.edges) {
        adj[e.a].push_back(e.b);
        adj[e.b].push_back(e.a);
    }

    // Grow components and keep the biggest one.
    std::vector<int> comp(N, -1);
    int bestComp = -1, bestSize = 0, compCount = 0;
    for (int s = 0; s < N; ++s) {
        if (comp[s] >= 0 || adj[s].empty()) continue;
        int size = 0;
        std::queue<int> q;
        q.push(s);
        comp[s] = compCount;
        while (!q.empty()) {
            int u = q.front(); q.pop();
            ++size;
            for (int v : adj[u])
                if (comp[v] < 0) { comp[v] = compCount; q.push(v); }
        }
        if (size > bestSize) { bestSize = size; bestComp = compCount; }
        ++compCount;
    }

    std::vector<int> nodeMap(N, -1);
    std::vector<Node> nodes;
    for (int u = 0; u < N; ++u) {
        if (comp[u] != bestComp) continue;
        Node n = city_.nodes[u];
        n.id = static_cast<int>(nodes.size());
        n.edges.clear();
        nodeMap[u] = n.id;
        nodes.push_back(n);
    }

    std::vector<int> edgeMap(city_.edges.size(), -1);
    std::vector<Edge> edges;
    for (const auto& old : city_.edges) {
        if (nodeMap[old.a] < 0 || nodeMap[old.b] < 0) continue;
        Edge e = old;
        e.id = static_cast<int>(edges.size());
        e.a = nodeMap[old.a];
        e.b = nodeMap[old.b];
        edgeMap[old.id] = e.id;
        nodes[e.a].edges.push_back(e.id);
        nodes[e.b].edges.push_back(e.id);
        edges.push_back(e);
    }

    for (auto& col : nodeAt_)
        for (int& id : col) id = id >= 0 ? nodeMap[id] : -1;
    for (auto* grid : {&hEdge_, &vEdge_})
        for (auto& col : *grid)
            for (int& id : col) id = id >= 0 ? edgeMap[id] : -1;

    city_.nodes = std::move(nodes);
    city_.edges = std::move(edges);
}

// ---- 6. Junction control -----------------------------------------------------------------

void CityGenerator::assignJunctionControl() {
    for (auto& node : city_.nodes) {
        node.control = JunctionControl::None;
        if (node.edges.size() <= 2) continue;   // a bend or a straight continuation

        bool hasH = false, hasV = false;
        for (int eid : node.edges) {
            const Node& other = city_.nodes[city_.edges[eid].other(node.id)];
            if (other.gj == node.gj) hasH = true; else hasV = true;
        }
        if (!hasH || !hasV) continue;

        int h = rank(lineClass_[node.gj]);
        int v = rank(lineClass_[node.gi]);
        int hi = std::max(h, v), lo = std::min(h, v);
        if (hi == rank(RoadClass::Arterial) && lo >= rank(RoadClass::Collector)) node.control = JunctionControl::Signal;
        else if (hi == rank(RoadClass::Collector) && lo == rank(RoadClass::Collector)) node.control = JunctionControl::AllWayStop;
        else node.control = JunctionControl::Priority;   // the smaller road gives way
    }
}

// ---- 7. Names, blocks, districts, zoning -------------------------------------------------

void CityGenerator::nameRoads() {
    std::vector<std::string> arterial = kArterialNames, collector = kCollectorNames;
    rng_.shuffle(arterial);
    rng_.shuffle(collector);

    std::vector<std::string> colName(n_), rowName(n_);
    size_t ai = 0, ci = 0;
    int mainNo = 1, crossNo = 1;
    auto nameFor = [&](int k, bool vertical) -> std::string {
        switch (lineClass_[k]) {
            case RoadClass::Arterial: {
                std::string s = arterial[ai % arterial.size()];
                if (ai++ >= arterial.size()) s += " Extension";
                return s;
            }
            case RoadClass::Collector: {
                std::string s = collector[ci % collector.size()];
                if (ci++ >= collector.size()) s += " East";
                return s;
            }
            case RoadClass::Local:
                return vertical ? ordinal(mainNo++) + " Main Road" : ordinal(crossNo++) + " Cross Road";
        }
        return "";
    };
    for (int k = 0; k < n_; ++k) colName[k] = nameFor(k, true);
    for (int k = 0; k < n_; ++k) rowName[k] = nameFor(k, false);

    for (auto& e : city_.edges) {
        const Node& a = city_.nodes[e.a];
        const Node& b = city_.nodes[e.b];
        e.name = a.gj == b.gj ? rowName[a.gj] : colName[a.gi];
    }
}

Vec2 CityGenerator::blockCenter(const Block& b) const { return centroid(b.corners); }

bool CityGenerator::blockTouches(const Block& b, RoadClass cls) const {
    for (int s : b.sides)
        if (s >= 0 && city_.edges[s].cls == cls) return true;
    return false;
}

void CityGenerator::makeBlocks() {
    const double half = city_.river.width * 0.5;
    for (int j = 0; j + 1 < n_; ++j)
        for (int i = 0; i + 1 < n_; ++i) {
            Block b;
            b.id = static_cast<int>(city_.blocks.size());
            b.gi = i;
            b.gj = j;
            b.corners = {gridPos_[i][j], gridPos_[i + 1][j], gridPos_[i + 1][j + 1], gridPos_[i][j + 1]};
            b.sides = {{hEdge_[i][j], vEdge_[i + 1][j], hEdge_[i][j + 1], vEdge_[i][j]}};
            if (city_.river.exists()) {
                int wet = 0;
                for (const auto& c : b.corners) wet += riverDistance(c) < half ? 1 : 0;
                b.water = riverDistance(blockCenter(b)) < half + 5 || wet >= 3;
            }
            city_.blocks.push_back(b);
        }
}

void CityGenerator::makeDistricts() {
    std::vector<int> land;
    for (const auto& b : city_.blocks)
        if (!b.water) land.push_back(b.id);
    if (land.empty()) return;

    int count = std::clamp(static_cast<int>(land.size()) / 22, 3, 12);
    std::vector<Vec2> seeds;
    seeds.push_back(blockCenter(city_.blocks[land[rng_.rangeInt(0, static_cast<int>(land.size()) - 1)]]));
    while (static_cast<int>(seeds.size()) < count) {
        double best = -1;
        Vec2 pick;
        for (int id : land) {
            Vec2 c = blockCenter(city_.blocks[id]);
            double md = 1e300;
            for (const auto& s : seeds) md = std::min(md, distance(c, s));
            md += rng_.range(0, block_);
            if (md > best) { best = md; pick = c; }
        }
        seeds.push_back(pick);
    }

    std::vector<std::string> names = kDistrictNames;
    rng_.shuffle(names);
    std::vector<Vec2> sum(count);
    std::vector<int> cnt(count, 0);
    for (auto& b : city_.blocks) {
        Vec2 c = blockCenter(b);
        int best = 0;
        for (int k = 1; k < count; ++k)
            if (distance(c, seeds[k]) < distance(c, seeds[best])) best = k;
        b.district = best;
        if (!b.water) { sum[best] += c; cnt[best]++; }
    }
    for (int k = 0; k < count; ++k) {
        District d;
        d.id = k;
        d.name = names[k % names.size()];
        d.center = cnt[k] ? sum[k] / cnt[k] : seeds[k];
        city_.districts.push_back(d);
    }
}

void CityGenerator::zoneBlocks() {
    const Vec2 center(city_.width * 0.5, city_.height * 0.5);
    const double radius = 0.5 * std::min(city_.width, city_.height);
    industrialSide_ = rng_.rangeInt(0, 3);

    for (auto& b : city_.blocks) {
        if (b.water) { b.zone = Zone::Water; continue; }
        bool noRoads = std::all_of(b.sides.begin(), b.sides.end(), [](int s) { return s < 0; });
        if (noRoads) { b.zone = Zone::Park; continue; }

        Vec2 c = blockCenter(b);
        double d = distance(c, center) / radius;
        double n = noise(c);
        double dn = d + n;
        double sideDist = industrialSide_ == 0 ? c.x : industrialSide_ == 1 ? city_.width - c.x
                        : industrialSide_ == 2 ? c.y : city_.height - c.y;
        bool arterial = blockTouches(b, RoadClass::Arterial);
        bool nearRiver = city_.river.exists() && riverDistance(c) < city_.river.width * 0.5 + block_ * 0.7;

        if (dn < 0.22) b.zone = Zone::Downtown;
        else if (d > 0.45 && sideDist < radius * (0.42 + n * 1.5)) b.zone = Zone::Industrial;
        else if (nearRiver && rng_.chance(0.3)) b.zone = Zone::Park;
        else if (rng_.chance(0.045)) b.zone = Zone::Park;
        else if (arterial && dn < 0.8 && rng_.chance(0.55)) b.zone = Zone::Commercial;
        else if (dn < 0.55) b.zone = Zone::Apartments;
        else b.zone = Zone::Residential;
    }
}

// ---- 8. Landmarks ------------------------------------------------------------------------

std::vector<int> CityGenerator::pickSpread(const std::function<bool(const Block&)>& ok, int count) {
    std::vector<int> cand;
    for (const auto& b : city_.blocks)
        if (landmark_[b.id] < 0 && !b.water && b.zone != Zone::Park && ok(b)) cand.push_back(b.id);

    std::vector<int> chosen;
    while (static_cast<int>(chosen.size()) < count && chosen.size() < cand.size()) {
        int best = -1;
        if (chosen.empty()) {
            best = cand[rng_.rangeInt(0, static_cast<int>(cand.size()) - 1)];
        } else {
            double bestScore = -1;
            for (int id : cand) {
                if (std::find(chosen.begin(), chosen.end(), id) != chosen.end()) continue;
                Vec2 c = blockCenter(city_.blocks[id]);
                double md = 1e300;
                for (int o : chosen) md = std::min(md, distance(c, blockCenter(city_.blocks[o])));
                double score = md + rng_.range(0, block_ * 0.8);
                if (score > bestScore) { bestScore = score; best = id; }
            }
        }
        if (best < 0) break;
        chosen.push_back(best);
    }
    return chosen;
}

void CityGenerator::placeLandmarks() {
    landmark_.assign(city_.blocks.size(), -1);
    landmarkName_.assign(city_.blocks.size(), "");

    int land = 0, resid = 0;
    for (const auto& b : city_.blocks) {
        if (b.water || b.zone == Zone::Park) continue;
        ++land;
        if (b.zone == Zone::Residential || b.zone == Zone::Apartments) ++resid;
    }
    const Vec2 center(city_.width * 0.5, city_.height * 0.5);
    const double radius = 0.5 * std::min(city_.width, city_.height);
    auto distNorm = [&](const Block& b) { return distance(blockCenter(b), center) / radius; };
    auto districtName = [&](const Block& b) {
        return b.district >= 0 && b.district < static_cast<int>(city_.districts.size()) ? city_.districts[b.district].name : std::string("City");
    };
    auto mark = [&](const std::vector<int>& ids, BuildingType t, const std::function<std::string(const Block&, int)>& name, bool civic) {
        for (size_t k = 0; k < ids.size(); ++k) {
            Block& b = city_.blocks[ids[k]];
            landmark_[b.id] = static_cast<int>(t);
            landmarkName_[b.id] = name(b, static_cast<int>(k));
            if (civic) b.zone = Zone::Civic;
        }
    };

    // Schools: in neighbourhoods, spread out so every area has one nearby.
    std::vector<std::string> schoolNames;
    for (const auto& p : kSchoolPrefixes)
        for (const auto& s : kSchoolSuffixes) schoolNames.push_back(p + " " + s);
    rng_.shuffle(schoolNames);
    int schools = std::max(2, static_cast<int>(std::lround(resid / 16.0)));
    mark(pickSpread([](const Block& b) { return b.zone == Zone::Residential || b.zone == Zone::Apartments; }, schools),
         BuildingType::School, [&](const Block&, int k) { return schoolNames[k % schoolNames.size()]; }, true);

    // Hospitals: on arterials, so ambulances get out fast.
    std::vector<std::string> hospitalNames = kHospitalNames;
    rng_.shuffle(hospitalNames);
    int hospitals = std::max(1, static_cast<int>(std::lround(land / 90.0)));
    mark(pickSpread([&](const Block& b) { return blockTouches(b, RoadClass::Arterial) && b.zone != Zone::Industrial; }, hospitals),
         BuildingType::Hospital, [&](const Block&, int k) { return hospitalNames[k % hospitalNames.size()]; }, true);

    int fire = std::max(1, static_cast<int>(std::lround(land / 110.0)));
    mark(pickSpread([&](const Block& b) {
             return (blockTouches(b, RoadClass::Arterial) || blockTouches(b, RoadClass::Collector)) && b.zone != Zone::Downtown;
         }, fire),
         BuildingType::FireStation, [](const Block&, int k) { return "Fire Station No. " + std::to_string(k + 1); }, false);

    int police = std::max(1, static_cast<int>(std::lround(land / 100.0)));
    mark(pickSpread([](const Block& b) { return b.zone != Zone::Industrial; }, police),
         BuildingType::PoliceStation, [&](const Block& b, int) { return districtName(b) + " Police Station"; }, false);

    int depots = land >= 150 ? 2 : 1;
    auto depotIds = pickSpread([&](const Block& b) { return b.zone == Zone::Industrial && blockTouches(b, RoadClass::Arterial); }, depots);
    if (depotIds.empty())
        depotIds = pickSpread([&](const Block& b) { return distNorm(b) > 0.55 && blockTouches(b, RoadClass::Arterial); }, depots);
    mark(depotIds, BuildingType::BusDepot,
         [&](const Block& b, int k) { return k == 0 ? std::string("City Bus Depot") : districtName(b) + " Bus Depot"; }, false);

    if (land >= 90)
        mark(pickSpread([&](const Block& b) {
                 return (b.zone == Zone::Commercial || b.zone == Zone::Downtown) && blockTouches(b, RoadClass::Arterial);
             }, 1),
             BuildingType::Mall, [&](const Block& b, int) { return districtName(b) + " City Centre Mall"; }, false);

    if (land >= 140)
        mark(pickSpread([&](const Block& b) {
                 return blockTouches(b, RoadClass::Arterial) && b.zone != Zone::Industrial && b.zone != Zone::Downtown;
             }, 1),
             BuildingType::Stadium, [](const Block&, int) { return std::string("Ashoka Stadium"); }, true);
}

// ---- 9. Plots and buildings --------------------------------------------------------------

void CityGenerator::addFeature(FeatureType type, std::vector<Vec2> poly, bool overBuildings) {
    Feature f;
    f.type = type;
    f.poly = std::move(poly);
    f.overBuildings = overBuildings;
    city_.features.push_back(std::move(f));
}

Building& CityGenerator::addBuilding(BuildingType type, std::vector<Vec2> poly, const Block& block, Zone zone) {
    Building b;
    b.id = static_cast<int>(city_.buildings.size());
    b.type = type;
    b.poly = std::move(poly);
    b.block = block.id;
    b.district = block.district;
    const double area = polygonArea(b.poly);

    switch (type) {
        case BuildingType::House: {
            double r = rng_.uniform();
            b.floors = r < 0.25 ? 1 : r < 0.7 ? 2 : 3;
            b.residents = b.floors * rng_.rangeInt(3, 6);   // typically one family per floor
            break;
        }
        case BuildingType::Apartment:
            b.floors = rng_.rangeInt(4, zone == Zone::Downtown ? 16 : 10);
            b.residents = static_cast<int>(area * b.floors / 30.0);
            break;
        case BuildingType::Shop:
            b.floors = rng_.rangeInt(1, 3);
            b.jobs = std::max(2, static_cast<int>(area / 35.0));
            b.residents = (b.floors - 1) * rng_.rangeInt(0, 4);  // families living above the shop
            break;
        case BuildingType::Office:
            b.floors = zone == Zone::Downtown ? rng_.rangeInt(6, 22) : rng_.rangeInt(3, 8);
            b.jobs = static_cast<int>(area * b.floors / 16.0);
            break;
        case BuildingType::Warehouse:
            b.floors = 1;
            b.jobs = std::max(4, static_cast<int>(area / 180.0));
            break;
        case BuildingType::Factory:
            b.floors = rng_.rangeInt(1, 2);
            b.jobs = std::max(10, static_cast<int>(area * b.floors / 70.0));
            break;
        default:
            break;  // landmarks are filled in by layoutLandmark
    }
    city_.buildings.push_back(std::move(b));
    return city_.buildings.back();
}

void CityGenerator::addPark(const std::vector<Vec2>& quad) {
    addFeature(FeatureType::Park, quad);
    int trees = std::clamp(static_cast<int>(polygonArea(quad) / 350.0), 3, 140);
    std::vector<Vec2> pts;
    for (int k = 0; k < trees; ++k) pts.push_back(quadPoint(quad, rng_.range(0.05, 0.95), rng_.range(0.05, 0.95)));
    addFeature(FeatureType::Trees, pts);
}

void CityGenerator::addLots(const Block& block, const std::vector<Vec2>& q, Zone zone) {
    struct ZoneSpec { double lotW, minRow; };
    struct TypeSpec { double depthMin, depthMax, fillMin, fillMax, setback; };

    ZoneSpec zs{18, 22};
    switch (zone) {
        case Zone::Apartments: zs = {32, 30}; break;
        case Zone::Commercial: zs = {16, 24}; break;
        case Zone::Downtown:   zs = {40, 36}; break;
        case Zone::Industrial: zs = {65, 50}; break;
        default: break;
    }
    auto typeSpec = [](BuildingType t) -> TypeSpec {
        switch (t) {
            case BuildingType::House:     return {12, 18, 0.85, 1.0, 2};
            case BuildingType::Apartment: return {18, 26, 0.70, 0.9, 5};
            case BuildingType::Shop:      return {12, 20, 0.90, 1.0, 1};
            case BuildingType::Office:    return {22, 34, 0.70, 0.9, 6};
            case BuildingType::Warehouse: return {30, 50, 0.75, 0.9, 10};
            case BuildingType::Factory:   return {25, 45, 0.60, 0.85, 12};
            default:                      return {12, 18, 0.8, 1.0, 3};
        }
    };
    const bool onMajorRoad = blockTouches(block, RoadClass::Arterial) || blockTouches(block, RoadClass::Collector);
    auto pickType = [&]() -> BuildingType {
        double r = rng_.uniform();
        switch (zone) {
            case Zone::Apartments: return r < 0.70 ? BuildingType::Apartment : r < 0.85 ? BuildingType::House : BuildingType::Shop;
            case Zone::Commercial: return r < 0.60 ? BuildingType::Shop : r < 0.75 ? BuildingType::Office : BuildingType::Apartment;
            case Zone::Downtown:   return r < 0.60 ? BuildingType::Office : r < 0.80 ? BuildingType::Apartment : BuildingType::Shop;
            case Zone::Industrial: return r < 0.55 ? BuildingType::Warehouse : BuildingType::Factory;
            default: {
                double shop = onMajorRoad ? 0.12 : 0.06;
                return r < shop ? BuildingType::Shop : r < shop + 0.10 ? BuildingType::Apartment : BuildingType::House;
            }
        }
    };

    const double wT = (distance(q[0], q[1]) + distance(q[3], q[2])) * 0.5;
    const double hL = (distance(q[0], q[3]) + distance(q[1], q[2])) * 0.5;
    if (wT < 8 || hL < 8) return;

    const int u = std::max(1, static_cast<int>(std::lround(wT / zs.lotW)));
    const int rows = hL >= 2 * zs.minRow ? 2 : 1;
    const double rowDepth = hL / rows;
    const double halfRiver = city_.river.width * 0.5;

    for (int row = 0; row < rows; ++row) {
        for (int a = 0; a < u; ++a) {
            if (rng_.chance(0.04)) continue;  // an empty plot
            BuildingType t = pickType();
            TypeSpec ts = typeSpec(t);
            double depth = std::min(rng_.range(ts.depthMin, ts.depthMax), rowDepth - 2.0);
            if (depth < 6) continue;

            double u0 = static_cast<double>(a) / u, u1 = static_cast<double>(a + 1) / u;
            double lotW = u1 - u0;
            double shrink = lotW * (1.0 - rng_.range(ts.fillMin, ts.fillMax)) * 0.5;
            double gap = std::min(0.75 / wT, lotW * 0.1);
            double sF = ts.setback / hL, dF = depth / hL;
            double v0, v1;
            if (row == 0) { v0 = sF; v1 = sF + dF; }           // faces the top road
            else { v1 = 1.0 - sF; v0 = v1 - dF; }              // faces the bottom road
            auto poly = quadSub(q, u0 + shrink + gap, u1 - shrink - gap, v0, v1);
            if (city_.river.exists() && riverDistance(centroid(poly)) < halfRiver + 10) continue;
            addBuilding(t, poly, block, zone);
        }
    }
}

void CityGenerator::layoutLandmark(const Block& block, const std::vector<Vec2>& q, BuildingType type, const std::string& name) {
    auto make = [&](std::vector<Vec2> poly, int floors, int jobs, int capacity) {
        Building& b = addBuilding(type, std::move(poly), block, block.zone);
        b.floors = floors;
        b.jobs = jobs;
        b.capacity = capacity;
        b.name = name;
    };
    const Zone restZone = block.zone == Zone::Civic ? Zone::Residential : block.zone;

    switch (type) {
        case BuildingType::School: {
            int seats = rng_.rangeInt(6, 14) * 100;
            make(quadSub(q, 0.08, 0.62, 0.05, 0.33), rng_.rangeInt(2, 4), seats / 22, seats);
            addFeature(FeatureType::SportsField, quadSub(q, 0.14, 0.86, 0.46, 0.92));
            addFeature(FeatureType::Plaza, quadSub(q, 0.68, 0.92, 0.06, 0.33));
            break;
        }
        case BuildingType::Hospital: {
            int beds = rng_.rangeInt(15, 45) * 10;
            make(quadSub(q, 0.10, 0.90, 0.06, 0.52), rng_.rangeInt(5, 9), beds * 2, beds);
            addFeature(FeatureType::Parking, quadSub(q, 0.10, 0.58, 0.62, 0.94));
            addBuilding(BuildingType::Apartment, quadSub(q, 0.66, 0.90, 0.62, 0.94), block, Zone::Apartments);  // staff quarters
            break;
        }
        case BuildingType::FireStation:
        case BuildingType::PoliceStation: {
            addFeature(FeatureType::Plaza, quadSub(q, 0.04, 0.40, 0.03, 0.20));   // apron for vehicles
            int staff = type == BuildingType::FireStation ? rng_.rangeInt(30, 50) : rng_.rangeInt(40, 80);
            make(quadSub(q, 0.04, 0.40, 0.20, 0.50), rng_.rangeInt(2, 3), staff, type == BuildingType::FireStation ? rng_.rangeInt(3, 6) : rng_.rangeInt(4, 10));
            addLots(block, quadSub(q, 0.46, 1.0, 0.0, 1.0), restZone);
            break;
        }
        case BuildingType::BusDepot:
            make(quadSub(q, 0.05, 0.45, 0.04, 0.24), 2, rng_.rangeInt(80, 160), rng_.rangeInt(40, 80));
            addFeature(FeatureType::Parking, quadSub(q, 0.05, 0.95, 0.30, 0.95));
            break;
        case BuildingType::Mall: {
            auto poly = quadSub(q, 0.08, 0.92, 0.05, 0.60);
            int floors = rng_.rangeInt(3, 5);
            make(poly, floors, static_cast<int>(polygonArea(poly) * floors / 40.0), 0);
            addFeature(FeatureType::Parking, quadSub(q, 0.08, 0.92, 0.66, 0.95));
            break;
        }
        case BuildingType::Stadium: {
            Vec2 c = quadPoint(q, 0.5, 0.5);
            Vec2 uDir = ((q[1] - q[0]) + (q[2] - q[3])).normalized();
            Vec2 vDir = ((q[3] - q[0]) + (q[2] - q[1])).normalized();
            double rx = 0.42 * (distance(q[0], q[1]) + distance(q[3], q[2])) * 0.5;
            double ry = 0.40 * (distance(q[0], q[3]) + distance(q[1], q[2])) * 0.5;
            std::vector<Vec2> ring;
            for (int k = 0; k < 32; ++k) {
                double t = 2 * kPi * k / 32;
                ring.push_back(c + uDir * (std::cos(t) * rx) + vDir * (std::sin(t) * ry));
            }
            make(ring, 3, 60, rng_.rangeInt(20, 45) * 1000);
            addFeature(FeatureType::SportsField,
                       {c - uDir * (rx * 0.55) - vDir * (ry * 0.5), c + uDir * (rx * 0.55) - vDir * (ry * 0.5),
                        c + uDir * (rx * 0.55) + vDir * (ry * 0.5), c - uDir * (rx * 0.55) + vDir * (ry * 0.5)},
                       true);
            break;
        }
        default:
            break;
    }
}

void CityGenerator::buildBlocks() {
    for (const auto& b : city_.blocks) {
        if (b.water) continue;
        std::vector<double> d(4);
        for (int s = 0; s < 4; ++s) d[s] = b.sides[s] >= 0 ? city_.edges[b.sides[s]].halfWidth() + 3.0 : 1.5;  // road + footpath
        auto q = insetPolygon(b.corners, d);
        if ((signedArea(q) > 0) != (signedArea(b.corners) > 0) || polygonArea(q) < 300) continue;

        if (landmark_[b.id] >= 0) layoutLandmark(b, q, static_cast<BuildingType>(landmark_[b.id]), landmarkName_[b.id]);
        else if (b.zone == Zone::Park) addPark(q);
        else addLots(b, q, b.zone);
    }
}

// Every building gets an entrance on a road; trips will start and end there.
void CityGenerator::assignAccess() {
    for (auto& bd : city_.buildings) {
        Vec2 c = centroid(bd.poly);
        const Block& block = city_.blocks[bd.block];
        int best = -1;
        double bestDist = 1e300;
        auto consider = [&](int eid) {
            const Edge& e = city_.edges[eid];
            double dd = distPointSegment(c, city_.nodes[e.a].pos, city_.nodes[e.b].pos);
            if (dd < bestDist) { bestDist = dd; best = eid; }
        };
        for (int s : block.sides)
            if (s >= 0) consider(s);
        if (best < 0)
            for (const auto& e : city_.edges) consider(e.id);
        if (best < 0) continue;

        const Edge& e = city_.edges[best];
        Vec2 a = city_.nodes[e.a].pos, b = city_.nodes[e.b].pos;
        bd.accessEdge = best;
        bd.accessT = projectT(c, a, b);
        bd.accessSide = cross(b - a, c - a) > 0 ? 1 : -1;
    }
}

}  // namespace sim
