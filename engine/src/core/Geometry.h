#pragma once
// 2D geometry helpers. World units are metres; x grows to the east, y grows to the south
// (the same orientation as the screen, which keeps the renderer simple).

#include <algorithm>
#include <cmath>
#include <vector>

namespace sim {

struct Vec2 {
    double x = 0, y = 0;

    Vec2() = default;
    Vec2(double x_, double y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    Vec2 operator/(double s) const { return {x / s, y / s}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }

    double length() const { return std::sqrt(x * x + y * y); }
    Vec2 normalized() const {
        double l = length();
        return l > 1e-12 ? Vec2(x / l, y / l) : Vec2(0, 0);
    }
};

inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline double distance(Vec2 a, Vec2 b) { return (a - b).length(); }
inline Vec2 lerp(Vec2 a, Vec2 b, double t) { return a + (b - a) * t; }

// Left-hand side of a direction of travel, in screen coordinates (y down).
// Facing east (1,0) the left is north (0,-1).
inline Vec2 perpLeft(Vec2 d) { return {d.y, -d.x}; }

// Parameter t in [0,1] of the point on segment ab closest to p.
inline double projectT(Vec2 p, Vec2 a, Vec2 b) {
    Vec2 ab = b - a;
    double l2 = dot(ab, ab);
    if (l2 < 1e-12) return 0.0;
    return std::clamp(dot(p - a, ab) / l2, 0.0, 1.0);
}

inline double distPointSegment(Vec2 p, Vec2 a, Vec2 b) { return distance(p, lerp(a, b, projectT(p, a, b))); }

inline double distPointPolyline(Vec2 p, const std::vector<Vec2>& pts, size_t* segment = nullptr) {
    double best = 1e300;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        double d = distPointSegment(p, pts[i], pts[i + 1]);
        if (d < best) {
            best = d;
            if (segment) *segment = i;
        }
    }
    return best;
}

// Intersection of the infinite lines p + t*d and q + s*e. False if they are parallel.
inline bool lineIntersect(Vec2 p, Vec2 d, Vec2 q, Vec2 e, Vec2& out) {
    double den = cross(d, e);
    if (std::fabs(den) < 1e-9) return false;
    double t = cross(q - p, e) / den;
    out = p + d * t;
    return true;
}

// Shoelace area. Positive when the points go clockwise on screen (because y points down).
inline double signedArea(const std::vector<Vec2>& poly) {
    double a = 0;
    for (size_t i = 0; i < poly.size(); ++i) {
        const Vec2& p = poly[i];
        const Vec2& q = poly[(i + 1) % poly.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return a * 0.5;
}

inline double polygonArea(const std::vector<Vec2>& poly) { return std::fabs(signedArea(poly)); }

inline Vec2 centroid(const std::vector<Vec2>& poly) {
    Vec2 c;
    for (const auto& p : poly) c += p;
    return poly.empty() ? c : c / static_cast<double>(poly.size());
}

inline bool pointInPolygon(Vec2 p, const std::vector<Vec2>& poly) {
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
}

// Moves every side of a convex polygon inwards by its own distance d[i]
// (side i runs from poly[i] to poly[i+1]). Used to shrink a block to the area behind the kerbs.
inline std::vector<Vec2> insetPolygon(const std::vector<Vec2>& poly, const std::vector<double>& d) {
    const size_t n = poly.size();
    const bool clockwise = signedArea(poly) > 0;
    std::vector<Vec2> base(n), dir(n);
    for (size_t i = 0; i < n; ++i) {
        Vec2 a = poly[i], b = poly[(i + 1) % n];
        dir[i] = (b - a).normalized();
        Vec2 inward = clockwise ? perpLeft(dir[i]) * -1.0 : perpLeft(dir[i]);
        base[i] = a + inward * d[i];
    }
    std::vector<Vec2> out(n);
    for (size_t i = 0; i < n; ++i) {
        size_t prev = (i + n - 1) % n;
        if (!lineIntersect(base[prev], dir[prev], base[i], dir[i], out[i])) out[i] = base[i];
    }
    return out;
}

// Bilinear point inside a quad given as top-left, top-right, bottom-right, bottom-left.
inline Vec2 quadPoint(const std::vector<Vec2>& q, double u, double v) {
    Vec2 top = lerp(q[0], q[1], u);
    Vec2 bottom = lerp(q[3], q[2], u);
    return lerp(top, bottom, v);
}

// Sub-quad of a quad, in the quad's own (u, v) parameter space.
inline std::vector<Vec2> quadSub(const std::vector<Vec2>& q, double u0, double u1, double v0, double v1) {
    return {quadPoint(q, u0, v0), quadPoint(q, u1, v0), quadPoint(q, u1, v1), quadPoint(q, u0, v1)};
}

}  // namespace sim
