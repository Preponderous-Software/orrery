// A spaceship flown through the drawn solar system, in the spirit of Rodina:
// one continuous space from a moon's surface to the Kuiper belt, crossed by
// stepping the drive up through speed tiers. The drive is mass locked near a
// body, so it slows down by itself on approach and never flies through a
// surface. No GPU here, so the flight model is tested on its own.
//
// Everything is in drawn kilometres (the renderer's space): in TRUE mode those
// are real kilometres and speeds are real; in COMPRESSED mode they are not.
#pragma once

#include <vector>

#include "orrery/vec.hpp"

namespace orrery {

constexpr double kLightKmPerS = 299792.458;

struct DriveTier {
    const char* name;
    double topKmPerS;
};
// Thrusters, cruise, then three warp steps: 1 c, 50 c and 2,000 c.
constexpr DriveTier kDriveTiers[] = {
    {"thrusters", 2.0},
    {"cruise", 200.0},
    {"warp 1", kLightKmPerS},
    {"warp 2", 50 * kLightKmPerS},
    {"warp 3", 2000 * kLightKmPerS},
};
constexpr int kDriveTierCount = sizeof kDriveTiers / sizeof kDriveTiers[0];

// One body as the ship sees it this instant, indexed like the catalogue.
struct FlightBody {
    Vec3 centre;            // drawn
    double radius = 0;      // drawn; 0 = no surface (comets, spacecraft)
    double influence = 0;   // the ship moves with the body inside this distance; 0 = never
    bool present = false;   // has a position at this date
};

// This step's stick and throttle input.
struct FlightControls {
    double throttleRate = 0;          // throttle change per second (W / S)
    double pitch = 0, yaw = 0, roll = 0;  // radians this step, positive = nose up / right / right wing down
};

// Mass lock: moving toward a surface, the ship covers at most this fraction
// of the remaining distance per second. Thrusters are never locked.
constexpr double kMassLockPerSecond = 0.6;

class Ship {
public:
    // Puts the ship at rest at a drawn position, nose along `forward`.
    void place(const Vec3& position, const Vec3& forward);
    // Advances dt seconds: moves with the local body, steers, eases the speed
    // toward the throttle's, applies mass lock, moves, and stops at surfaces.
    // dt = 0 only carries the ship with its body and refreshes what it sees.
    void step(double dt, const FlightControls& controls, const std::vector<FlightBody>& bodies);

    void setTier(int tier);
    void setThrottle(double throttle);  // -0.25..1
    double topSpeed() const { return kDriveTiers[tier_].topKmPerS; }
    void allStop() { throttle_ = 0; autopilot_ = false; }
    // Autopilot flies to the target and stops at a few radii from it.
    void setTarget(int body) { target_ = body; if (body < 0) autopilot_ = false; }
    bool engageAutopilot(bool on);

    Vec3 position;
    Vec3 forward{1, 0, 0};
    Vec3 up{0, 0, 1};
    double speed = 0;  // drawn km/s along forward; negative is reverse

    double throttle() const { return throttle_; }
    int tier() const { return tier_; }
    int target() const { return target_; }
    bool autopilot() const { return autopilot_; }
    // The ship's length in drawn km. It follows the distance to the nearest
    // surface, so the ship and its camera fit at every scale.
    double size() const { return size_; }
    int anchor() const { return anchor_; }        // the body the ship moves with, or -1
    int nearest() const { return nearest_; }      // closest surface, or -1
    double altitude() const { return altitude_; }  // above that surface
    int massLock() const { return massLock_; }    // the body holding the drive back, or -1
    int touched() const { return touched_; }      // the surface the ship last stopped at, or -1
    int arrived() const { return arrived_; }      // the target autopilot last reached, or -1
    // Where autopilot stops: a few radii out from the target.
    static double standoff(const FlightBody& b);

private:
    void ride(const std::vector<FlightBody>& bodies);
    void turn(double pitch, double yaw, double roll);
    void fly(double dt, const std::vector<FlightBody>& bodies);

    double throttle_ = 0;  // -0.25..1 of the tier's top speed
    int tier_ = 0;
    int target_ = -1;
    bool autopilot_ = false;
    double size_ = 0;
    int anchor_ = -1;
    Vec3 anchorAt_;  // the anchor's centre when last seen
    int nearest_ = -1;
    double altitude_ = 0;
    int massLock_ = -1, touched_ = -1, arrived_ = -1;
};

}  // namespace orrery
