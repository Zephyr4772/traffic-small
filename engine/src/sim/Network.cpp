#include "sim/Network.h"

#include <algorithm>
#include <cmath>

namespace sim {

namespace {

// Proper intersection of segments ab and cd (touching at the ends does not count).
bool segmentsCross(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    double d1 = cross(b - a, c - a), d2 = cross(b - a, d - a);
    double d3 = cross(d - c, a - c), d4 = cross(d - c, b - c);
    return ((d1 > 1e-9 && d2 < -1e-9) || (d1 < -1e-9 && d2 > 1e-9)) &&
           ((d3 > 1e-9 && d4 < -1e-9) || (d3 < -1e-9 && d4 > 1e-9));
}

bool polylinesCross(const std::vector<Vec2>& p, const std::vector<Vec2>& q) {
    for (size_t i = 0; i + 1 < p.size(); ++i)
        for (size_t j = 0; j + 1 < q.size(); ++j)
            if (segmentsCross(p[i], p[i + 1], q[j], q[j + 1])) return true;
    return false;
}

Vec2 bezier(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, double t) {
    double u = 1 - t;
    return p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t);
}

}  // namespace

void Network::build(const City& city) {
    links.clear();
    lanes.clear();
    connectors.clear();
    junctions.clear();
    driveLeft = city.config.driveLeft;
    const double side = driveLeft ? 1.0 : -1.0;   // lanes sit on the left of the centre line in India

    std::vector<double> stop(city.nodes.size());
    for (const auto& n : city.nodes) stop[n.id] = city.stopDistance(n.id);

    edgeLinks.assign(city.edges.size(), {{-1, -1}});
    for (const Edge& e : city.edges) {
        for (int d = 0; d < 2; ++d) {
            Link L;
            L.id = static_cast<int>(links.size());
            L.edge = e.id;
            L.from = d == 0 ? e.a : e.b;
            L.to = d == 0 ? e.b : e.a;
            Vec2 pa = city.nodes[L.from].pos, pb = city.nodes[L.to].pos;
            L.dir = (pb - pa).normalized();
            L.normal = perpLeft(L.dir);
            L.startTrim = stop[L.from];
            L.endTrim = stop[L.to];
            if (e.length - L.startTrim - L.endTrim < 8.0) {   // keep at least 8 m of lane on short roads
                double k = std::max(0.0, e.length - 8.0) / (L.startTrim + L.endTrim);
                L.startTrim *= k;
                L.endTrim *= k;
            }
            L.length = e.length - L.startTrim - L.endTrim;
            L.lanes = e.lanesPerDir;
            L.speedLimit = e.speedLimit;
            L.cls = e.cls;
            L.firstLane = static_cast<int>(lanes.size());
            for (int k = 0; k < L.lanes; ++k) {
                Lane ln;
                ln.id = static_cast<int>(lanes.size());
                ln.link = L.id;
                ln.index = k;
                ln.offset = side * (e.medianWidth * 0.5 + (L.lanes - k - 0.5) * e.laneWidth);
                ln.start = pa + L.dir * L.startTrim + L.normal * ln.offset;
                ln.end = pb - L.dir * L.endTrim + L.normal * ln.offset;
                ln.dir = L.dir;
                ln.length = distance(ln.start, ln.end);
                lanes.push_back(ln);
            }
            edgeLinks[e.id][d] = L.id;
            links.push_back(L);
        }
    }

    junctions.resize(city.nodes.size());
    for (const auto& n : city.nodes) {
        Junction& J = junctions[n.id];
        J.node = n.id;
        J.control = n.control;
        int lo = 3, hi = 1;
        for (int e : n.edges) {
            int r = static_cast<int>(city.edges[e].cls);
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
        J.maxRank = hi;
        J.equalRanks = lo == hi;
    }
    for (const auto& L : links) {
        junctions[L.to].inLinks.push_back(L.id);
        junctions[L.from].outLinks.push_back(L.id);
    }

    // Connectors: every allowed movement from every incoming lane.
    for (auto& J : junctions) {
        const bool deadEnd = city.nodes[J.node].edges.size() == 1;
        for (int inId : J.inLinks) {
            for (int outId : J.outLinks) {
                Link& in = links[inId];
                const Link& out = links[outId];
                bool uturn = out.edge == in.edge;
                if (uturn && !deadEnd) continue;   // U-turns only where the road ends
                Turn t = uturn ? Turn::UTurn : classify(in.dir, out.dir);
                in.next.push_back({outId, t});

                for (int i = 0; i < in.lanes; ++i) {
                    const int last = in.lanes - 1, outLast = out.lanes - 1;
                    switch (t) {
                        case Turn::Near: addConnector(J.node, in, i, out, 0, t, i == 0); break;
                        case Turn::Far:
                        case Turn::UTurn: addConnector(J.node, in, i, out, outLast, t, i == last); break;
                        case Turn::Straight:
                            for (int j = std::max(0, i - 1); j <= std::min(outLast, i + 1); ++j)
                                addConnector(J.node, in, i, out, j, t, j == std::min(i, outLast) || in.lanes < out.lanes);
                            break;
                    }
                }
            }
        }
    }
    findConflicts();
}

Turn Network::classify(Vec2 dIn, Vec2 dOut) const {
    double angle = std::atan2(cross(dIn, dOut), dot(dIn, dOut));   // > 0 = turning right (y points down)
    if (std::fabs(angle) < 0.6) return Turn::Straight;
    if (std::fabs(angle) > 2.8) return Turn::UTurn;
    bool right = angle > 0;
    return right == driveLeft ? Turn::Far : Turn::Near;
}

Turn Network::turnBetween(int inLink, int outLink) const {
    for (const auto& [id, t] : links[inLink].next)
        if (id == outLink) return t;
    return Turn::Straight;
}

void Network::addConnector(int node, const Link& in, int inLane, const Link& out, int outLane, Turn turn, bool preferred) {
    Lane& a = lanes[in.firstLane + inLane];
    const Lane& b = lanes[out.firstLane + outLane];
    Connector c;
    c.id = static_cast<int>(connectors.size());
    c.node = node;
    c.fromLane = a.id;
    c.toLane = b.id;
    c.fromLink = in.id;
    c.toLink = out.id;
    c.turn = turn;
    c.preferred = preferred;

    Vec2 p0 = a.end, p3 = b.start;
    double d = distance(p0, p3);
    double k = turn == Turn::Straight ? d / 3.0 : turn == Turn::UTurn ? std::max(5.0, d * 0.9) : d * 0.42;
    Vec2 p1 = p0 + in.dir * k, p2 = p3 - out.dir * k;
    const int samples = 14;
    for (int i = 0; i <= samples; ++i) c.pts.push_back(bezier(p0, p1, p2, p3, static_cast<double>(i) / samples));
    c.cum.push_back(0);
    for (size_t i = 1; i < c.pts.size(); ++i) c.cum.push_back(c.cum.back() + distance(c.pts[i - 1], c.pts[i]));
    c.length = std::max(0.5, c.cum.back());

    switch (turn) {
        case Turn::Straight: c.speedLimit = std::min(in.speedLimit, out.speedLimit); break;
        case Turn::Near: c.speedLimit = 4.5; break;     // tight kerb-side turn
        case Turn::Far: c.speedLimit = 6.0; break;
        case Turn::UTurn: c.speedLimit = 3.0; break;
    }
    a.out.push_back(c.id);
    junctions[node].connectors.push_back(c.id);
    connectors.push_back(std::move(c));
}

void Network::findConflicts() {
    for (const auto& J : junctions) {
        const auto& cs = J.connectors;
        for (size_t x = 0; x < cs.size(); ++x)
            for (size_t y = x + 1; y < cs.size(); ++y) {
                Connector& a = connectors[cs[x]];
                Connector& b = connectors[cs[y]];
                if (a.fromLane == b.fromLane) continue;   // same queue, they can't meet
                if (a.toLane == b.toLane || polylinesCross(a.pts, b.pts)) {
                    a.conflicts.push_back(b.id);
                    b.conflicts.push_back(a.id);
                }
            }
    }
}

double Network::elementLength(int e) const {
    return isConnector(e) ? connectors[connectorIndex(e)].length : lanes[e].length;
}

double Network::elementSpeed(int e) const {
    return isConnector(e) ? connectors[connectorIndex(e)].speedLimit : links[lanes[e].link].speedLimit;
}

void Network::pose(int e, double s, Vec2& pos, double& angle) const {
    if (!isConnector(e)) {
        const Lane& ln = lanes[e];
        pos = ln.start + ln.dir * std::clamp(s, 0.0, ln.length);
        angle = std::atan2(ln.dir.y, ln.dir.x);
        return;
    }
    const Connector& c = connectors[connectorIndex(e)];
    s = std::clamp(s, 0.0, c.cum.back());
    size_t i = 1;
    while (i + 1 < c.cum.size() && c.cum[i] < s) ++i;
    double segLen = c.cum[i] - c.cum[i - 1];
    double t = segLen > 1e-9 ? (s - c.cum[i - 1]) / segLen : 0.0;
    pos = lerp(c.pts[i - 1], c.pts[i], t);
    Vec2 d = c.pts[i] - c.pts[i - 1];
    angle = std::atan2(d.y, d.x);
}

}  // namespace sim
