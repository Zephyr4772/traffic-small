#pragma once
// Deterministic random numbers. Every random decision in the simulator goes through one of
// these, so the same seed always produces the same city and the same traffic.

#include <cmath>
#include <cstdint>
#include <vector>

namespace sim {

class Rng {
public:
    explicit Rng(uint64_t seed = 1) { reseed(seed); }

    void reseed(uint64_t seed) {
        uint64_t z = seed;
        for (auto& s : s_) s = static_cast<uint32_t>(splitmix(z) >> 16);
        if ((s_[0] | s_[1] | s_[2] | s_[3]) == 0) s_[0] = 1;
    }

    // xoshiro128** — small, fast and good enough for simulation.
    uint32_t next() {
        const uint32_t result = rotl(s_[1] * 5u, 7) * 9u;
        const uint32_t t = s_[1] << 9;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 11);
        return result;
    }

    double uniform() { return next() * (1.0 / 4294967296.0); }               // [0, 1)
    double range(double a, double b) { return a + (b - a) * uniform(); }      // [a, b)
    int rangeInt(int a, int b) { return a + static_cast<int>(uniform() * (b - a + 1)); }  // [a, b]
    bool chance(double p) { return uniform() < p; }

    double normal(double mean, double sd) {
        double u1 = uniform(), u2 = uniform();
        if (u1 < 1e-12) u1 = 1e-12;
        return mean + sd * std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }

    // An independent stream, so one subsystem's draws don't shift another's.
    Rng fork(uint64_t salt) { return Rng((static_cast<uint64_t>(next()) << 32 | next()) ^ (salt * 0x9E3779B97F4A7C15ull)); }

    template <class T>
    void shuffle(std::vector<T>& v) {
        for (size_t i = v.size(); i > 1; --i) {
            size_t j = static_cast<size_t>(uniform() * i);
            std::swap(v[i - 1], v[j]);
        }
    }

private:
    static uint64_t splitmix(uint64_t& x) {
        uint64_t z = (x += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    static uint32_t rotl(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }

    uint32_t s_[4];
};

}  // namespace sim
