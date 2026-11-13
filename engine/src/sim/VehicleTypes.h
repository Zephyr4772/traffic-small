#pragma once
// Vehicle classes and their physical/driving parameters (Intelligent Driver Model).
// Values are typical for Indian city traffic.

#include <cstdint>

namespace sim {

enum class VehicleKind : uint8_t { Car = 0, Motorbike, AutoRickshaw, Van, Truck, Bus, SchoolBus, Ambulance };
constexpr int kVehicleKinds = 8;

struct VehicleSpec {
    const char* name;
    double length;        // m
    double width;         // m
    double maxAccel;      // m/s^2   (IDM a)
    double comfortDecel;  // m/s^2   (IDM b)
    double maxSpeed;      // m/s     (the vehicle's own top speed in town)
    double minGap;        // m       (IDM s0, gap kept when stopped)
    double headway;       // s       (IDM T, time gap kept when moving)
};

inline const VehicleSpec& specOf(VehicleKind k) {
    static const VehicleSpec specs[kVehicleKinds] = {
        {"Car",            4.4, 1.8, 1.8, 3.0, 22.0, 2.0, 1.3},
        {"Motorbike",      2.0, 0.8, 2.4, 4.0, 22.0, 1.2, 1.0},
        {"Auto-rickshaw",  2.8, 1.4, 1.1, 2.5, 12.5, 1.6, 1.2},
        {"Van",            5.5, 2.0, 1.4, 2.8, 20.0, 2.2, 1.4},
        {"Truck",         10.0, 2.5, 0.7, 2.0, 16.0, 3.0, 1.8},
        {"Bus",           12.0, 2.6, 0.9, 2.0, 16.0, 3.0, 1.6},
        {"School bus",    10.0, 2.5, 0.9, 2.0, 14.0, 3.0, 1.6},
        {"Ambulance",      5.5, 2.0, 2.0, 3.5, 25.0, 2.0, 1.0},
    };
    return specs[static_cast<int>(k)];
}

}  // namespace sim
