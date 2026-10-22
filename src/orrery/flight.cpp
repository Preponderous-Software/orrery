#include "orrery/flight.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace orrery {
namespace {

constexpr double kSpeedEaseSeconds = 0.45;  // the drive spools in log space: ~1.5 s from rest to any tier
constexpr double kSizeEaseSeconds = 0.3;
constexpr double kSizePerAltitude = 0.01;   // the ship is 1/100 of its altitude long
constexpr double kMinSizeKm = 0.01;         // ... but never under 10 m
constexpr double kRestKmPerS = 1e-3;        // below this, easing toward rest snaps to rest
constexpr double kAutopilotTurn = 1.6;      // radians per second
constexpr double kAutopilotApproach = 0.8;  // fraction of the remaining distance per second

// Eases a magnitude toward another in log space, so the drive takes about as
// long to climb from 2 km/s to 2,000 c as from 2 to 200 km/s.
double easeLog(double from, double to, double a) {
    const double f = std::max(from, kRestKmPerS), t = std::max(to, kRestKmPerS);
    const double r = std::exp(std::log(f) + (std::log(t) - std::log(f)) * a);
    return to <= kRestKmPerS && r <= 1.5 * kRestKmPerS ? 0.0 : r;
}

double sign(double v) { return v < 0 ? -1.0 : 1.0; }

Vec3 rotate(const Vec3& v, const Vec3& axis, double angle) {  // Rodrigues; axis is a unit vector
    const double c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1 - c));
}

}  // namespace

double Ship::standoff(const FlightBody& b) { return b.radius > 0 ? 4 * b.radius : 5000.0; }

void Ship::place(const Vec3& p, const Vec3& fwd) {
    position = p;
    forward = normalize(fwd);
    const Vec3 right = normalize(cross(forward, std::fabs(forward.z) < 0.99 ? Vec3{0, 0, 1} : Vec3{1, 0, 0}));
    up = cross(right, forward);
    speed = 0;
    throttle_ = 0;
    autopilot_ = false;
    size_ = 0;
    anchor_ = -1;
    arrived_ = -1;
}

void Ship::setTier(int tier) { tier_ = std::clamp(tier, 0, kDriveTierCount - 1); }
void Ship::setThrottle(double throttle) { throttle_ = std::clamp(throttle, -0.25, 1.0); }

bool Ship::engageAutopilot(bool on) {
    autopilot_ = on && target_ >= 0;
    if (autopilot_) arrived_ = -1;
    return autopilot_;
}

void Ship::ride(const std::vector<FlightBody>& bodies) {
    const int n = static_cast<int>(bodies.size());
    if (anchor_ >= 0 && anchor_ < n && bodies[static_cast<size_t>(anchor_)].present) {
        position += bodies[static_cast<size_t>(anchor_)].centre - anchorAt_;
    }
    // The most local body whose influence holds the ship: a moon before its
    // planet, a planet before the Sun.
    anchor_ = -1;
    double best = std::numeric_limits<double>::infinity();
    for (int i = 0; i < n; ++i) {
        const FlightBody& b = bodies[static_cast<size_t>(i)];
        if (!b.present || b.influence <= 0 || b.influence >= best) continue;
        if (length(position - b.centre) < b.influence) {
            anchor_ = i;
            best = b.influence;
        }
    }
    if (anchor_ >= 0) anchorAt_ = bodies[static_cast<size_t>(anchor_)].centre;
}

void Ship::turn(double pitch, double yaw, double roll) {
    Vec3 right = cross(forward, up);
    if (pitch != 0) {
        forward = rotate(forward, right, pitch);
        up = rotate(up, right, pitch);
    }
    if (yaw != 0) forward = rotate(forward, up, -yaw);
    if (roll != 0) up = rotate(up, forward, roll);
    forward = normalize(forward);
    right = normalize(cross(forward, up));
    up = cross(right, forward);
}

void Ship::step(double dt, const FlightControls& c, const std::vector<FlightBody>& bodies) {
    ride(bodies);
    if (dt > 0) {
        const bool manual = c.throttleRate != 0 || c.pitch != 0 || c.yaw != 0 || c.roll != 0;
        if (manual) autopilot_ = false;
        turn(c.pitch, c.yaw, c.roll);
        throttle_ = std::clamp(throttle_ + c.throttleRate * dt, -0.25, 1.0);
    }
    fly(dt, bodies);
}

void Ship::fly(double dt, const std::vector<FlightBody>& bodies) {
    const int n = static_cast<int>(bodies.size());
    if (target_ >= n || (target_ >= 0 && !bodies[static_cast<size_t>(target_)].present)) autopilot_ = false;

    // ---- autopilot: turn to the target, choose the drive, slow to a stop at the standoff
    if (autopilot_ && dt > 0) {
        const FlightBody& t = bodies[static_cast<size_t>(target_)];
        const Vec3 rel = t.centre - position;
        const double dist = length(rel);
        const Vec3 want = dist > 0 ? rel / dist : forward;
        const double angle = std::acos(std::clamp(dot(forward, want), -1.0, 1.0));
        if (angle > 1e-9) {
            const Vec3 axis = std::fabs(angle - kPi) < 1e-6 ? up : normalize(cross(forward, want));
            const double by = std::min(angle, kAutopilotTurn * dt);
            forward = rotate(forward, axis, by);
            up = rotate(up, axis, by);
            turn(0, 0, 0);  // re-orthonormalise
        }
        const double remaining = dist - standoff(t);
        if (remaining <= 0.03 * standoff(t)) {
            speed = 0;
            throttle_ = 0;
            autopilot_ = false;
            arrived_ = target_;
        } else {
            const double aligned = angle < 0.3 ? std::cos(angle) : 0.0;
            const double desired = kAutopilotApproach * remaining * aligned;
            int tier = 0;
            while (tier < kDriveTierCount - 1 && kDriveTiers[tier].topKmPerS < desired) ++tier;
            tier_ = tier;
            throttle_ = std::min(desired / topSpeed(), 1.0);
        }
    }

    // ---- the drive eases toward the throttle's speed
    const double wanted = throttle_ * topSpeed();
    if (dt > 0) {
        const double a = 1 - std::exp(-dt / kSpeedEaseSeconds);
        if (wanted == 0 || speed == 0 || (wanted > 0) == (speed > 0)) {
            const double s = wanted != 0 ? sign(wanted) : sign(speed);
            speed = s * easeLog(std::fabs(speed), std::fabs(wanted), a);
        } else {
            speed = sign(speed) * easeLog(std::fabs(speed), 0, a);  // through rest before reversing
        }
    }

    // ---- mass lock: toward a surface, cover at most kMassLockPerSecond of the gap per second
    const Vec3 heading = forward * sign(speed);
    double cap = 1e308;
    int capBody = -1;
    for (int i = 0; i < n; ++i) {
        const FlightBody& b = bodies[static_cast<size_t>(i)];
        if (!b.present || b.radius <= 0) continue;
        const Vec3 rel = b.centre - position;
        const double dist = length(rel);
        const double closing = dist > 0 ? dot(heading, rel / dist) : 1.0;
        if (closing <= 0.02) continue;
        const double limit = kMassLockPerSecond * std::max(dist - b.radius, 0.0) / closing;
        if (limit < cap) {
            cap = limit;
            capBody = i;
        }
    }
    const double allowed = std::max(cap, kDriveTiers[0].topKmPerS);
    if (std::fabs(speed) > allowed) speed = sign(speed) * allowed;
    massLock_ = tier_ > 0 && capBody >= 0 && cap < std::fabs(wanted) ? capBody : -1;

    // ---- move, and stop at any surface
    position += forward * (speed * dt);
    for (int i = 0; i < n; ++i) {
        const FlightBody& b = bodies[static_cast<size_t>(i)];
        if (!b.present || b.radius <= 0) continue;
        const Vec3 rel = position - b.centre;
        const double dist = length(rel);
        const double floor = b.radius + 0.5 * size_;
        if (dist >= floor) continue;
        const Vec3 out = dist > 0 ? rel / dist : -forward;
        position = b.centre + out * floor;
        touched_ = i;
        if (dot(forward * speed, out) < 0) {
            speed = 0;
            throttle_ = 0;
            autopilot_ = false;
        }
    }
    if (speed != 0) {
        arrived_ = -1;
        touched_ = -1;
    }

    // ---- what the ship sees, and its size
    nearest_ = -1;
    altitude_ = 1e308;
    for (int i = 0; i < n; ++i) {
        const FlightBody& b = bodies[static_cast<size_t>(i)];
        if (!b.present || b.radius <= 0) continue;
        const double alt = length(position - b.centre) - b.radius;
        if (alt < altitude_) {
            altitude_ = alt;
            nearest_ = i;
        }
    }
    const double sizeWanted = nearest_ >= 0 ? std::max(kSizePerAltitude * altitude_, kMinSizeKm) : 1.0;
    if (size_ <= 0 || dt <= 0) {
        size_ = sizeWanted;
    } else {
        const double a = 1 - std::exp(-dt / kSizeEaseSeconds);
        size_ = std::exp(std::log(size_) + (std::log(sizeWanted) - std::log(size_)) * a);
    }
}

}  // namespace orrery
