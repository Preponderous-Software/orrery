// The ship: drive tiers, spooling, mass lock, surfaces, moving with a body,
// steering and autopilot. All in drawn km, as the app flies it.
#include <cmath>
#include <vector>

#include "check.hpp"
#include "orrery/flight.hpp"

using namespace orrery;

namespace {

constexpr double kDt = 1.0 / 60;
constexpr double kEarthR = 6371;

// The Sun at the origin and an Earth 1 au out along +X.
std::vector<FlightBody> world(Vec3 earthAt = {kAuKm, 0, 0}) {
    std::vector<FlightBody> b(2);
    b[0] = {{0, 0, 0}, 695700, 1e308, true};
    b[1] = {earthAt, kEarthR, 25 * kEarthR, true};
    return b;
}

void run(Ship& ship, const std::vector<FlightBody>& bodies, double seconds, FlightControls c = {}) {
    for (double t = 0; t < seconds; t += kDt) ship.step(kDt, c, bodies);
}

bool orthonormal(const Ship& s) {
    return std::fabs(length(s.forward) - 1) < 1e-9 && std::fabs(length(s.up) - 1) < 1e-9 &&
           std::fabs(dot(s.forward, s.up)) < 1e-9;
}

}  // namespace

int main() {
    SECTION("tiers climb, and warp 1 is the speed of light");
    for (int i = 1; i < kDriveTierCount; ++i) CHECK(kDriveTiers[i].topKmPerS > kDriveTiers[i - 1].topKmPerS);
    CHECK(kDriveTiers[2].topKmPerS == kLightKmPerS);

    SECTION("the drive spools from rest to warp 3 within a few seconds in empty space");
    {
        Ship ship;
        ship.place({40 * kAuKm, 0, 0}, {0, 1, 0});  // far from everything, heading along an orbit
        ship.setTier(4);
        run(ship, world(), 1.0, {1.0, 0, 0, 0});  // throttle to full in a second
        run(ship, world(), 2.0);
        CHECK_MSG(ship.speed > 0.5 * ship.topSpeed(), std::to_string(ship.speed));
        CHECK(ship.massLock() < 0);
        ship.allStop();
        run(ship, world(), 4.0);
        CHECK_MSG(ship.speed == 0, std::to_string(ship.speed));
    }

    SECTION("mass lock: at warp straight at Earth, the ship slows on its own and settles on the surface at thruster speed");
    {
        Ship ship;
        const Vec3 earth{kAuKm, 0, 0};
        ship.place(earth - Vec3{0.3 * kAuKm, 0, 0}, {1, 0, 0});
        ship.setTier(3);
        run(ship, world(), 1.0, {1.0, 0, 0, 0});
        bool locked = false, clean = true;
        for (int k = 0; k < 60 * 30; ++k) {
            const double before = length(ship.position - earth) - kEarthR;
            ship.step(kDt, {}, world());
            locked = locked || ship.massLock() == 1;
            const double after = length(ship.position - earth) - kEarthR;
            if (before - after > kMassLockPerSecond * before * kDt * 1.0001 + kDriveTiers[0].topKmPerS * kDt) clean = false;
        }
        CHECK(locked && clean);
        CHECK_MSG(ship.touched() == 1 && ship.altitude() > 0, std::to_string(ship.altitude()));
    }

    SECTION("thrusters into a surface stop at it");
    {
        Ship ship;
        const Vec3 earth{kAuKm, 0, 0};
        ship.place(earth - Vec3{kEarthR + 20, 0, 0}, {1, 0, 0});
        ship.setTier(0);
        run(ship, world(), 1.0, {1.0, 0, 0, 0});
        run(ship, world(), 30.0);
        CHECK(ship.touched() == 1 && ship.speed == 0 && ship.throttle() == 0);
        CHECK(length(ship.position - earth) >= kEarthR);
        ship.step(kDt, {}, world());
        CHECK(ship.touched() == 1);  // stays down until it moves
    }

    SECTION("inside a body's influence the ship moves with it; outside, it does not");
    {
        Ship near, far;
        const Vec3 earth{kAuKm, 0, 0}, moved = earth + Vec3{0, 30 * 3600, 0};  // an hour of Earth's orbit
        near.place(earth + Vec3{0, 0, 10 * kEarthR}, {0, 1, 0});
        far.place(earth + Vec3{0, 0, 100 * kEarthR}, {0, 1, 0});
        near.step(0, {}, world(earth));
        far.step(0, {}, world(earth));
        CHECK(near.anchor() == 1 && far.anchor() == 0);
        const Vec3 nearBefore = near.position, farBefore = far.position;
        near.step(0, {}, world(moved));
        far.step(0, {}, world(moved));
        CHECK(length(near.position - nearBefore - (moved - earth)) < 1e-6);
        CHECK(length(far.position - farBefore) < 1e-6);
    }

    SECTION("steering keeps the ship's axes orthonormal and turns the right way");
    {
        Ship ship;
        ship.place({40 * kAuKm, 0, 0}, {1, 0, 0});
        for (int k = 0; k < 10000; ++k) ship.step(kDt, {0, 0.013, -0.007, 0.021}, world());
        CHECK(orthonormal(ship));
        ship.place({40 * kAuKm, 0, 0}, {1, 0, 0});
        ship.step(kDt, {0, 0.1, 0, 0}, world());
        CHECK(ship.forward.z > 0);  // pitch up
        ship.place({40 * kAuKm, 0, 0}, {1, 0, 0});
        ship.step(kDt, {0, 0, 0.1, 0}, world());
        CHECK(ship.forward.y < 0);  // yaw right: from +X with +Z up, right is -Y
    }

    SECTION("the controls: tier and throttle clamp, and autopilot needs a present target and yields to the stick");
    {
        Ship ship;
        ship.setTier(-3);
        CHECK(ship.tier() == 0 && ship.topSpeed() == kDriveTiers[0].topKmPerS);
        ship.setTier(99);
        CHECK(ship.tier() == kDriveTierCount - 1 && ship.topSpeed() == kDriveTiers[kDriveTierCount - 1].topKmPerS);
        ship.setThrottle(2);
        CHECK(ship.throttle() == 1);
        ship.setThrottle(-1);
        CHECK(ship.throttle() == -0.25);
        ship.setThrottle(0.5);
        CHECK(ship.throttle() == 0.5);

        // Held throttle keys stop at the same limits.
        std::vector<FlightBody> bodies = world();
        ship.place({0, 3 * kAuKm, 0}, {0, 1, 0});  // deep space
        FlightControls up;
        up.throttleRate = 1;
        run(ship, bodies, 2, up);
        CHECK(ship.throttle() == 1);
        FlightControls down;
        down.throttleRate = -1;
        run(ship, bodies, 2, down);
        CHECK(ship.throttle() == -0.25);

        // Reverse: thrusters at -25% back the ship up at a quarter of their top speed.
        ship.setTier(0);
        run(ship, bodies, 10);
        CHECK_MSG(ship.speed < 0 && std::fabs(ship.speed + 0.25 * ship.topSpeed()) < 0.01, std::to_string(ship.speed));

        ship.allStop();
        CHECK(ship.throttle() == 0 && !ship.autopilot());

        CHECK(!ship.engageAutopilot(true));  // no target
        ship.setTarget(1);
        CHECK(ship.engageAutopilot(true) && ship.autopilot());
        ship.setTarget(-1);
        CHECK(!ship.autopilot());

        // Any stick input takes control back.
        ship.setTarget(1);
        ship.engageAutopilot(true);
        FlightControls stick;
        stick.yaw = 0.01;
        ship.step(kDt, stick, bodies);
        CHECK(!ship.autopilot());

        // A target with no position at this date cannot be flown to.
        bodies[1].present = false;
        ship.engageAutopilot(true);
        ship.step(kDt, {}, bodies);
        CHECK(!ship.autopilot());
    }

    SECTION("autopilot: from 1.5 au away, arrives and stops at the standoff from Mars");
    {
        std::vector<FlightBody> bodies = world();
        const Vec3 mars{-1.5 * kAuKm, 0.2 * kAuKm, 0};
        bodies.push_back({mars, 3390, 25 * 3390, true});
        Ship ship;
        ship.place(Vec3{kAuKm, 0, 0} + Vec3{0, 0, 30 * kEarthR}, {0, 0, 1});  // facing away
        ship.setTarget(2);
        CHECK(ship.engageAutopilot(true));
        double t = 0;
        while (ship.autopilot() && t < 120) {
            ship.step(kDt, {}, bodies);
            t += kDt;
        }
        CHECK_MSG(ship.arrived() == 2, std::to_string(t) + " s");
        const double dist = length(ship.position - mars);
        CHECK_MSG(std::fabs(dist - Ship::standoff(bodies[2])) < 0.05 * Ship::standoff(bodies[2]), std::to_string(dist));
        CHECK(ship.speed == 0 && ship.touched() < 0);
        CHECK_MSG(t < 60, std::to_string(t) + " s");
    }

    SECTION("the ship is sized to its altitude so it and its camera fit at every scale");
    {
        Ship ship;
        const Vec3 earth{kAuKm, 0, 0};
        ship.place(earth + Vec3{0, 0, kEarthR + 1000}, {0, 1, 0});
        ship.step(0, {}, world());
        CHECK(std::fabs(ship.size() - 10) < 1e-6 && ship.nearest() == 1);
        ship.place(earth + Vec3{0, 0, kEarthR + 0.1}, {0, 1, 0});
        ship.step(0, {}, world());
        CHECK(ship.size() == 0.01);
    }

    return check::finish();
}
