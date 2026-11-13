#pragma once
// Traffic signal controllers. Every controller answers one question: what colour does each
// phase show right now? Milestone 4 adds actuated and max-pressure controllers; milestone 5
// the priority-aware one. They all plug in behind this interface.

#include <cstdint>
#include <memory>
#include <vector>

namespace sim {

enum class LightState : uint8_t { Red = 0, Amber = 1, Green = 2 };

class SignalController {
public:
    virtual ~SignalController() = default;
    virtual void update(double dt) = 0;
    virtual LightState state(int phase) const = 0;
    virtual const char* name() const = 0;
};

// The baseline: each approach gets a fixed green in turn (approach-wise phasing, as used at
// many Indian junctions), then amber, then a short all-red so the junction clears.
class FixedTimeController : public SignalController {
public:
    FixedTimeController(std::vector<double> greens, double amber, double allRed, double offset)
        : greens_(std::move(greens)), amber_(amber), allRed_(allRed) {
        // Start part-way through the cycle so neighbouring junctions are not in lockstep.
        for (double t = 0; t < offset; t += 0.5) update(0.5);
    }

    void update(double dt) override {
        t_ += dt;
        for (;;) {
            double limit = stage_ == Stage::Green ? greens_[phase_] : stage_ == Stage::Amber ? amber_ : allRed_;
            if (t_ < limit) break;
            t_ -= limit;
            if (stage_ == Stage::Green) stage_ = Stage::Amber;
            else if (stage_ == Stage::Amber) stage_ = Stage::AllRed;
            else { stage_ = Stage::Green; phase_ = (phase_ + 1) % static_cast<int>(greens_.size()); }
        }
    }

    LightState state(int phase) const override {
        if (phase != phase_) return LightState::Red;
        if (stage_ == Stage::Green) return LightState::Green;
        if (stage_ == Stage::Amber) return LightState::Amber;
        return LightState::Red;
    }

    const char* name() const override { return "Fixed-time"; }

    double cycleLength() const {
        double c = 0;
        for (double g : greens_) c += g + amber_ + allRed_;
        return c;
    }

private:
    enum class Stage { Green, Amber, AllRed };
    std::vector<double> greens_;
    double amber_, allRed_;
    int phase_ = 0;
    Stage stage_ = Stage::Green;
    double t_ = 0;
};

// Priority-aware controller: serves the approach with the highest priority score
// (vehicle type, size, waiting time, time of day), pre-empts for ambulances, and never
// lets an approach wait longer than the maximum green of the others (starvation guard).
class SmartController : public SignalController {
public:
    explicit SmartController(int phases) : scores_(phases, 0.0), n_(phases) {}

    void setDemand(const std::vector<double>& scores, int emergencyPhase) {
        scores_ = scores;
        emergency_ = emergencyPhase;
    }

    void update(double dt) override {
        t_ += dt;
        if (stage_ == Stage::Green) {
            int best = bestOther();
            bool change = false;
            if (emergency_ >= 0) change = emergency_ != phase_ && t_ >= 3;            // ambulance pre-emption
            else if (t_ >= 60) change = best >= 0 && scores_[best] > 0;                // max green
            else if (t_ >= 7 && best >= 0) change = scores_[best] > scores_[phase_] * 1.3 + 1.0;
            if (change) {
                next_ = emergency_ >= 0 ? emergency_ : best;
                stage_ = Stage::Amber;
                t_ = 0;
            }
        } else if (stage_ == Stage::Amber && t_ >= 3) {
            stage_ = Stage::AllRed;
            t_ = 0;
        } else if (stage_ == Stage::AllRed && t_ >= 2) {
            stage_ = Stage::Green;
            phase_ = next_;
            t_ = 0;
        }
    }

    LightState state(int phase) const override {
        if (phase != phase_) return LightState::Red;
        return stage_ == Stage::Green ? LightState::Green : stage_ == Stage::Amber ? LightState::Amber : LightState::Red;
    }

    const char* name() const override { return "Smart priority"; }

private:
    int bestOther() const {
        int best = -1;
        for (int p = 0; p < n_; ++p)
            if (p != phase_ && (best < 0 || scores_[p] > scores_[best])) best = p;
        return best;
    }
    enum class Stage { Green, Amber, AllRed };
    std::vector<double> scores_;
    int n_;
    int phase_ = 0, next_ = 0, emergency_ = -1;
    Stage stage_ = Stage::Green;
    double t_ = 0;
};

}  // namespace sim
