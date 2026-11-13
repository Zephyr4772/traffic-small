#include "sim/Router.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace sim {

namespace {

uint32_t mix(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return static_cast<uint32_t>(x);
}

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kMaxSpeed = 50 / 3.6;   // fastest road, for the A* heuristic

}  // namespace

double Router::positionOnLink(int link, double edgeT) const {
    const Link& L = net_.links[link];
    const Edge& E = city_.edges[L.edge];
    double along = (L.from == E.a ? edgeT : 1.0 - edgeT) * E.length - L.startTrim;
    return std::clamp(along, std::min(2.0, L.length * 0.5), std::max(L.length - 2.0, L.length * 0.5));
}

double Router::junctionCost(int inLink, Turn turn) const {
    const Link& L = net_.links[inLink];
    const Junction& J = net_.junctions[L.to];
    double c = 0;
    switch (J.control) {
        case JunctionControl::Signal: c = 12; break;
        case JunctionControl::AllWayStop: c = 6; break;
        case JunctionControl::Priority: c = net_.rank(inLink) < J.maxRank || J.equalRanks ? 4 : 1; break;
        case JunctionControl::None: c = 0; break;
    }
    switch (turn) {
        case Turn::Straight: break;
        case Turn::Near: c += 2; break;
        case Turn::Far: c += 6; break;
        case Turn::UTurn: c += 20; break;
    }
    return c;
}

bool Router::route(const Building& from, const Building& to, uint32_t salt, Route& out) const {
    const int n = static_cast<int>(net_.links.size());
    const int originEdge = from.accessEdge, destEdge = to.accessEdge;
    if (originEdge < 0 || destEdge < 0) return false;

    const Edge& De = city_.edges[destEdge];
    const Vec2 target = lerp(city_.nodes[De.a].pos, city_.nodes[De.b].pos, to.accessT);
    auto heuristic = [&](int link) { return distance(city_.nodes[net_.links[link].to].pos, target) / kMaxSpeed; };
    auto travel = [&](int link, double dist) {
        double jitter = 0.85 + 0.3 * (mix(static_cast<uint64_t>(salt) * 1315423911ULL + link) / 4294967295.0);
        return dist / net_.links[link].speedLimit * jitter;
    };

    std::vector<double> g(n, kInf);
    std::vector<int> parent(n, -1);
    std::vector<double> startS(n, 0);
    struct Item { double f, g; int link; bool operator>(const Item& o) const { return f > o.f; } };
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;

    double best = kInf;
    int bestLink = -1, bestParent = -1;
    bool direct = false;

    for (int d = 0; d < 2; ++d) {
        int L = net_.edgeLinks[originEdge][d];
        double s0 = positionOnLink(L, from.accessT);
        if (originEdge == destEdge) {
            double ts = positionOnLink(L, to.accessT);
            if (ts > s0 + 3 && travel(L, ts - s0) < best) {
                best = travel(L, ts - s0);
                bestLink = L;
                direct = true;
            }
        }
        double g0 = travel(L, net_.links[L].length - s0);
        if (g0 < g[L]) {
            g[L] = g0;
            startS[L] = s0;
            open.push({g0 + heuristic(L), g0, L});
        }
    }

    while (!open.empty()) {
        Item it = open.top();
        open.pop();
        if (it.g > g[it.link]) continue;
        if (it.f >= best) break;
        const Link& L = net_.links[it.link];
        for (const auto& [next, turn] : L.next) {
            double step = g[it.link] + junctionCost(it.link, turn);
            const Link& N = net_.links[next];
            if (N.edge == destEdge) {
                double total = step + travel(next, positionOnLink(next, to.accessT));
                if (total < best) {
                    best = total;
                    bestLink = next;
                    bestParent = it.link;
                    direct = false;
                }
            }
            double gn = step + travel(next, N.length);
            if (gn < g[next]) {
                g[next] = gn;
                parent[next] = it.link;
                open.push({gn + heuristic(next), gn, next});
            }
        }
    }
    if (bestLink < 0) return false;

    out = Route();
    if (direct) {
        out.links = {bestLink};
    } else {
        out.links.push_back(bestLink);
        for (int cur = bestParent; cur >= 0; cur = parent[cur]) out.links.push_back(cur);
        std::reverse(out.links.begin(), out.links.end());
    }
    if (out.links.size() > 400) return false;

    for (size_t k = 0; k + 1 < out.links.size(); ++k) out.turns.push_back(net_.turnBetween(out.links[k], out.links[k + 1]));
    out.startS = direct ? positionOnLink(bestLink, from.accessT) : startS[out.links.front()];
    out.targetS = positionOnLink(out.links.back(), to.accessT);

    // Free-flow time: what the trip would take on empty roads with every light green.
    if (out.links.size() == 1) {
        out.freeFlowTime = (out.targetS - out.startS) / net_.links[out.links[0]].speedLimit;
    } else {
        out.freeFlowTime = (net_.links[out.links[0]].length - out.startS) / net_.links[out.links[0]].speedLimit;
        for (size_t k = 1; k + 1 < out.links.size(); ++k) out.freeFlowTime += net_.links[out.links[k]].length / net_.links[out.links[k]].speedLimit;
        out.freeFlowTime += out.targetS / net_.links[out.links.back()].speedLimit;
        out.freeFlowTime += 2.5 * static_cast<double>(out.links.size() - 1);   // slowing for each junction
    }
    return true;
}

}  // namespace sim
