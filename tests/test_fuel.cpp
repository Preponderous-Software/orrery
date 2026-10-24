// Fuel: the drive burns it by tier and throttle, gliding and landed burn none,
// a landed ship refuels, an empty tank leaves no drive and no autopilot, and
// the tow puts the ship back by the body it last landed on with a full tank.
#include <cmath>
#include <string>
#include <vector>

#include "check.hpp"
#include "orrery/flight.hpp"

using namespace orrery;

namespace {

constexpr double kDt = 1.0 / 60;
constexpr double kEarthR = 6371, kMoonR = 1737.4;
const Vec3 kEarth{1.5e8, 0, 0}, kMoon = kEarth + Vec3{384400, 0, 0};

// The Sun, Earth (with air and gravity) and the Moon.
std::vector<FlightBody> world() {
    std::vector<FlightBody> b(3);
    b[0] = {{0, 0, 0}, 695700, 1e308, true, {}, false, {}, 1};
    b[1] = {kEarth, kEarthR, 25 * kEarthR, true, {}, true, atmosphereOf(399), 1};
    b[1].air.surfaceGravity = 9.82;
    b[2] = {kMoon, kMoonR, 12 * kMoonR, true, {}, true, {}, 1};
    return b;
}

void run(Ship& ship, const std::vector<FlightBody>& bodies, double seconds, FlightControls c = {}) {
    for (double t = 0; t < seconds; t += kDt) ship.step(kDt, c, bodies);
}

// Deep space, nose away from everything, at a tier and throttle.
Ship cruising(int tier, double throttle) {
    Ship ship;
    ship.place({0, 40 * 1.5e8, 0}, {0, 1, 0});
    ship.setTier(tier);
    ship.setThrottle(throttle);
    return ship;
}

}  // namespace

int main() {
    const auto bodies = world();

    SECTION("a new ship has a full tank; the rates climb with the tier");
    CHECK(Ship{}.fuel() == 1);
    for (int i = 1; i < kDriveTierCount; ++i) CHECK(kFuelPerSecond[i] > kFuelPerSecond[i - 1]);

    SECTION("the drive burns fuel at its tier's rate times the throttle; zero throttle burns none");
    for (int tier = 0; tier < kDriveTierCount; ++tier) {
        for (double throttle : {1.0, 0.5, -0.25}) {
            Ship ship = cruising(tier, throttle);
            run(ship, bodies, 10);
            const double expect = 1 - std::fabs(throttle) * kFuelPerSecond[tier] * 10;
            CHECK_MSG(std::fabs(ship.fuel() - expect) < 1e-3, std::to_string(tier) + " " + std::to_string(ship.fuel()));
        }
    }
    {
        Ship idle = cruising(4, 0);
        run(idle, bodies, 30);
        CHECK(idle.fuel() == 1);
    }

    SECTION("an empty tank: the throttle drops to zero and the ship coasts to rest; autopilot will not run");
    {
        Ship ship = cruising(4, 1.0);
        ship.setFuel(0.001);  // a fraction of a second at warp 3
        run(ship, bodies, 1);
        CHECK(ship.fuel() == 0 && ship.throttle() == 0);
        run(ship, bodies, 10);
        CHECK_MSG(ship.speed == 0, std::to_string(ship.speed));
        ship.setTarget(2);
        ship.engageAutopilot(true);
        ship.step(kDt, {}, bodies);
        CHECK(!ship.autopilot() && ship.speed == 0);
        ship.setThrottle(1);
        ship.step(kDt, {}, bodies);
        CHECK(ship.throttle() == 0);
    }

    SECTION("gliding burns no fuel, so an empty tank in air still glides down to a landing");
    {
        Ship ship;
        ship.place(kEarth + Vec3{0, kEarthR + 0.5, 0}, {1, 0, 0});
        ship.setFuel(0);
        for (double t = 0; t < 120 && ship.landed() < 0; t += kDt) ship.step(kDt, {}, bodies);
        CHECK(ship.landed() == 1 && ship.touchdown().grade == Touchdown::Soft);
        CHECK(ship.fuel() < 0.05);  // nothing burned on the way (a little refuelled since landing)
    }

    SECTION("a landed ship refuels, from empty to full in five seconds");
    {
        Ship ship;
        ship.place(kMoon + Vec3{0, 4 * kMoonR, 0}, {0, -1, 0});
        ship.setTarget(2);
        ship.engageLanding(true);
        run(ship, bodies, 60);
        CHECK(ship.landed() == 2);
        ship.setFuel(0);
        run(ship, bodies, 2.5);
        CHECK_MSG(std::fabs(ship.fuel() - 0.5) < 0.02, std::to_string(ship.fuel()));
        run(ship, bodies, 3);
        CHECK(ship.fuel() == 1);
    }

    SECTION("the tow: back to the standoff of the body last landed on, at rest, with a full tank");
    {
        Ship ship;
        ship.place(kMoon + Vec3{0, 4 * kMoonR, 0}, {0, -1, 0});
        ship.setTarget(2);
        ship.engageLanding(true);
        run(ship, bodies, 60);
        CHECK(ship.landed() == 2);
        // Fly off toward Earth, run dry, and call the tow.
        ship.setTarget(1);
        ship.engageAutopilot(true);
        run(ship, bodies, 3);
        ship.setFuel(0);
        run(ship, bodies, 5);
        CHECK(ship.speed == 0);
        CHECK(ship.tow(bodies) == 2);
        const double d = length(ship.position - kMoon);
        CHECK_MSG(std::fabs(d - (kMoonR + 4 * kMoonR)) < 1e-6, std::to_string(d));
        CHECK(ship.fuel() == 1 && ship.speed == 0 && ship.target() == 1);
        CHECK(dot(ship.forward, normalize(kMoon - ship.position)) > 0.999);  // facing it
        CHECK(ship.tow(bodies) == 2);  // and it remembers the Moon after a tow

        Ship fresh;  // never landed: the nearest body
        fresh.place(kEarth + Vec3{0, 3 * kEarthR, 0}, {1, 0, 0});
        fresh.step(0, {}, bodies);
        CHECK(fresh.tow(bodies) == 1);
    }

    return check::finish();
}
