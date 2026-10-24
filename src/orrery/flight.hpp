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

#include "orrery/atmosphere.hpp"
#include "orrery/rotation.hpp"
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
    BodyFrame frame;        // the body's axes at this date; a landed ship turns with them
    bool solid = true;      // has a surface to land on; the Sun and the giant planets do not
    Atmosphere air;         // its air, if any (in real km and kg/m^3)
    double realPerDrawn = 1;  // real km per drawn km: the air is as thick and as fast-burning at every scale
};

// This step's stick and throttle input.
struct FlightControls {
    double throttleRate = 0;          // throttle change per second (W / S)
    double pitch = 0, yaw = 0, roll = 0;  // radians this step, positive = nose up / right / right wing down
};

// Mass lock: moving toward a surface, the ship covers at most this fraction
// of the remaining distance per second. Thrusters are never locked; under any
// bigger drive the lock brings the ship down gently (kLockedFloorKmPerS).
constexpr double kMassLockPerSecond = 0.6;
constexpr double kLockedFloorKmPerS = 0.05;

// Touchdown: the speed into the surface at contact grades the landing. These
// are game thresholds, not physics: on thrusters (2 km/s) a soft landing is
// under 6% throttle, a crash anything over 30%. A body with no solid surface
// cannot be landed on: reaching its drawn radius at any speed loses the ship.
constexpr double kSoftLandingKmPerS = 0.12;
constexpr double kHardLandingKmPerS = 0.6;

// Air: drag slows the ship (dv/dt = -density * v^2 / (2 * ballistic
// coefficient)), and speed in it heats the ship toward burnSpeed(). Heat eases
// toward (speed / burn speed)^3 over kHeatSeconds, and cools the same way out
// of the air; at 1 the ship burns up. Game values, in real units.
constexpr double kBallisticKgPerM2 = 10000;
constexpr double kHeatSeconds = 3;

// Gliding: in air at zero throttle, off autopilot, the ship glides instead of
// stopping. It flies at the speed where its wings hold its weight,
// sqrt(2 g W / density), but never faster than the autopilot's cool speed,
// and sinks at that speed / kGlideRatio. Game values for wing loading and
// glide ratio; g is the body's surface gravity.
constexpr double kWingKgPerM2 = 400;
constexpr double kGlideRatio = 5;

struct Touchdown {
    // Lost: into a body with no solid surface. Burned: too fast in a body's air
    // (impactKmPerS is then the speed, in real km/s).
    enum Grade { None, Soft, Hard, Crash, Lost, Burned };
    Grade grade = None;
    int body = -1;
    double impactKmPerS = 0;  // speed into the surface at contact
};

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
    // Autopilot flies to the target and stops at a few radii from it; from a
    // surface it first climbs straight up.
    void setTarget(int body) { target_ = body; if (body < 0) autopilot_ = false; }
    bool engageAutopilot(bool on);
    // Landing autopilot: flies to the target and sets down on it softly. A
    // target with no solid surface gets the plain autopilot's standoff instead.
    // In air, either autopilot keeps to a speed that stays cool (kAutopilotHeat).
    bool engageLanding(bool on);

    Vec3 position;
    Vec3 forward{1, 0, 0};
    Vec3 up{0, 0, 1};
    double speed = 0;  // drawn km/s along forward; negative is reverse

    double throttle() const { return throttle_; }
    int tier() const { return tier_; }
    int target() const { return target_; }
    bool autopilot() const { return autopilot_; }
    bool landing() const { return autopilot_ && landing_; }
    // The ship's length in drawn km. It follows the distance to the nearest
    // surface, so the ship and its camera fit at every scale.
    double size() const { return size_; }
    int anchor() const { return anchor_; }        // the body the ship moves with, or -1
    int nearest() const { return nearest_; }      // closest surface, or -1
    double altitude() const { return altitude_; }  // above that surface
    double climb() const { return climb_; }        // km/s away from that surface; negative = descending
    int massLock() const { return massLock_; }    // the body holding the drive back, or -1
    int landed() const { return landed_; }        // the body the ship sits on, or -1
    // The last contact with a surface, kept until the ship next moves (a crash
    // or a loss until it moves again after being put back above the surface).
    const Touchdown& touchdown() const { return touchdown_; }
    int arrived() const { return arrived_; }      // the target autopilot last reached, or -1
    int air() const { return air_; }              // the body whose air the ship is in, or -1
    double airDensity() const { return airDensity_; }  // kg/m^3 there
    double heat() const { return heat_; }         // 0 cold .. 1 burning up
    bool gliding() const { return gliding_; }     // in air at zero throttle: gliding down
    // Where autopilot stops: a few radii out from the target.
    static double standoff(const FlightBody& b);

private:
    void ride(const std::vector<FlightBody>& bodies);
    void turn(double pitch, double yaw, double roll);
    void fly(double dt, const std::vector<FlightBody>& bodies);
    void contact(int body, const FlightBody& b, const Vec3& out, const Vec3& velocity);
    void burnUp(int body, const FlightBody& b, double speedKmPerS);
    double airCap(const std::vector<FlightBody>& bodies) const;

    double throttle_ = 0;  // -0.25..1 of the tier's top speed
    int tier_ = 0;
    int target_ = -1;
    bool autopilot_ = false;
    double size_ = 0;
    int anchor_ = -1;
    Vec3 anchorAt_;  // the anchor's centre when last seen
    int nearest_ = -1;
    double altitude_ = 0;
    bool landing_ = false;
    int climbFrom_ = -1;  // autopilot: the surface it is climbing away from
    double climb_ = 0;
    int massLock_ = -1, landed_ = -1, arrived_ = -1;
    int air_ = -1;
    double airDensity_ = 0, heat_ = 0;
    bool gliding_ = false;
    Touchdown touchdown_;
    // While landed: the ship in its body's axes, so it turns with the body.
    Vec3 localPos_, localForward_, localUp_;
};

}  // namespace orrery
