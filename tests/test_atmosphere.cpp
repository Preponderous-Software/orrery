// Air: the four atmospheres, the burn speed, and the ship in air (drag,
// heating, burning up, and both autopilots keeping cool). In drawn km, as the
// app flies it, with realPerDrawn turning drawn km into real ones.
#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <vector>

#include "check.hpp"
#include "orrery/atmosphere.hpp"
#include "orrery/flight.hpp"

using namespace orrery;

namespace {

constexpr double kDt = 1.0 / 60;
constexpr double kEarthR = 6371, kVenusR = 6051.8;
const Vec3 kAt{1.5e8, 0, 0};

// The Sun (no surface) far off, and one body with air at kAt, drawn `enlarge` times its size.
std::vector<FlightBody> world(long naif, double radiusKm, double enlarge = 1) {
    std::vector<FlightBody> b(2);
    b[0] = {{0, 0, 0}, 695700, 1e308, true, {}, false, {}, 1};
    b[1] = {kAt, radiusKm * enlarge, 25 * radiusKm * enlarge, true, {}, true, {}, 1};
    b[1].air = atmosphereOf(naif);
    b[1].realPerDrawn = 1 / enlarge;
    return b;
}

struct Flight {
    double seconds = 0, hottest = 0;
    bool burned = false;
};

// Steps until the ship lands, burns up or `limit` seconds pass.
Flight fly(Ship& ship, const std::vector<FlightBody>& bodies, double limit) {
    Flight f;
    for (; f.seconds < limit; f.seconds += kDt) {
        ship.step(kDt, {}, bodies);
        f.hottest = std::max(f.hottest, ship.heat());
        if (ship.touchdown().grade == Touchdown::Burned) f.burned = true;
        if (f.burned || ship.landed() >= 0) break;
    }
    return f;
}

// Level flight: `altitudeKm` (real) above the body's +Y side, nose along +X.
Ship level(const std::vector<FlightBody>& bodies, double altitudeKm, int tier, double throttle) {
    const FlightBody& b = bodies[1];
    Ship ship;
    ship.place(b.centre + Vec3{0, b.radius + altitudeKm / b.realPerDrawn, 0}, {1, 0, 0});
    ship.setTier(tier);
    ship.setThrottle(throttle);
    ship.speed = throttle * ship.topSpeed();
    return ship;
}

}  // namespace

int main() {
    SECTION("the four atmospheres, and none elsewhere");
    for (long naif : {299L, 399L, 499L, 606L}) CHECK_MSG(atmosphereOf(naif).present(), std::to_string(naif));
    for (long naif : {10L, 199L, 301L, 599L, 999L}) CHECK_MSG(!atmosphereOf(naif).present(), std::to_string(naif));
    {
        const Atmosphere earth = atmosphereOf(399);
        CHECK(earth.density(0) == 1.217);
        CHECK(std::fabs(earth.density(8.5) - 1.217 / std::exp(1.0)) < 1e-12);  // one scale height up
        CHECK(earth.density(-1) == 1.217);                                       // below the surface: the surface's
        CHECK_MSG(earth.topKm() > 110 && earth.topKm() < 130, std::to_string(earth.topKm()));
        CHECK(std::fabs(earth.density(earth.topKm() - 1e-9) - kAirTopDensity) < 1e-12);
        CHECK(earth.density(earth.topKm()) == 0);
        CHECK(atmosphereOf(299).density(10) > 50 * earth.density(10));  // Venus: thick
        CHECK(atmosphereOf(499).density(10) < earth.density(10) / 50);   // Mars: thin
        CHECK(atmosphereOf(606).topKm() > 2 * earth.topKm());            // Titan: tall
    }

    SECTION("the burn speed: 5 km/s at Earth's sea level, faster where the air is thinner");
    {
        const Atmosphere earth = atmosphereOf(399);
        CHECK(std::fabs(burnSpeed(1.217) - 5.0) < 1e-12);
        CHECK(burnSpeed(earth.density(60)) > 15 && burnSpeed(earth.density(60)) < 18);
        CHECK(burnSpeed(earth.density(100)) > burnSpeed(earth.density(60)));
        CHECK(burnSpeed(0) > 1e300);
    }

    SECTION("on thrusters through Earth's air at 60 km the ship stays cool");
    {
        const auto bodies = world(399, kEarthR);
        Ship ship = level(bodies, 60, 0, 1.0);  // 2 km/s
        const Flight f = fly(ship, bodies, 10);
        CHECK(!f.burned);
        CHECK(ship.air() == 1 && ship.airDensity() > 0);
        CHECK_MSG(f.hottest < 0.05, std::to_string(f.hottest));
        CHECK(ship.speed > 1.9);  // drag there is slight
    }

    SECTION("on cruise at 40 km/s there it burns up, and is put back above the air, cold");
    {
        const auto bodies = world(399, kEarthR);
        Ship ship = level(bodies, 60, 1, 0.2);
        const Flight f = fly(ship, bodies, 5);
        CHECK(f.burned);
        CHECK_MSG(f.seconds < 0.5, std::to_string(f.seconds));
        CHECK(ship.touchdown().body == 1);
        CHECK_MSG(ship.touchdown().impactKmPerS > 30, std::to_string(ship.touchdown().impactKmPerS));
        CHECK(ship.speed == 0 && ship.heat() == 0 && ship.air() < 0);
        CHECK(length(ship.position - kAt) - kEarthR > atmosphereOf(399).topKm());
        ship.setThrottle(0.1);
        ship.step(kDt, {}, bodies);
        ship.step(kDt, {}, bodies);
        CHECK(ship.touchdown().grade == Touchdown::None);  // moving again clears it
    }

    SECTION("heat builds over seconds just over the burn speed, and cools out of the air");
    {
        const auto bodies = world(399, kEarthR);
        Ship ship = level(bodies, 60, 1, 0.125);  // 25 km/s, about 1.5 x the burn speed there
        Flight f = fly(ship, bodies, 0.5);
        CHECK(!f.burned && ship.heat() > 0.3);
        f = fly(ship, bodies, 10);
        CHECK(f.burned && f.seconds > 0.2);

        Ship cooling = level(bodies, 60, 1, 0.125);
        fly(cooling, bodies, 0.5);
        const double hot = cooling.heat();
        cooling.place(kAt + Vec3{0, 2 * kEarthR, 0}, {1, 0, 0});  // out of the air (placing also resets)
        CHECK(cooling.heat() == 0);
        Ship out = level(bodies, 200, 1, 0.125);  // above the air: no heating at all
        fly(out, bodies, 2);
        CHECK(out.heat() == 0 && out.air() < 0 && hot > 0.3);
    }

    SECTION("Venus's thick air holds the ship back near the surface; no air does not");
    {
        const auto venus = world(299, kVenusR);
        Ship ship = level(venus, 1, 0, 1.0);
        fly(ship, venus, 5);
        CHECK_MSG(ship.speed < 1.0, std::to_string(ship.speed));  // under half of the 2 km/s it reaches with no air
        CHECK_MSG(ship.heat() < 0.1, std::to_string(ship.heat()));
        auto bare = venus;
        bare[1].air = {};
        Ship free = level(bare, 1, 0, 1.0);
        fly(free, bare, 5);
        CHECK_MSG(free.speed > 1.9, std::to_string(free.speed));
    }

    SECTION("the landing autopilot comes down through each air without burning up");
    for (const auto& [naif, radius, limit] :
         std::vector<std::tuple<long, double, double>>{{399, kEarthR, 60}, {299, kVenusR, 120}, {499, 3389.5, 60}, {606, 2574.7, 120}}) {
        const auto bodies = world(naif, radius);
        Ship ship;
        ship.place(kAt + Vec3{0, 4 * radius, 0}, {0, -1, 0});
        ship.setTarget(1);
        ship.engageLanding(true);
        const Flight f = fly(ship, bodies, limit);
        CHECK_MSG(!f.burned && ship.landed() == 1, std::to_string(naif));
        CHECK_MSG(ship.touchdown().grade == Touchdown::Soft, std::to_string(naif));
        CHECK_MSG(f.hottest < 0.5, std::to_string(naif) + " " + std::to_string(f.hottest));
        std::printf("    %ld: landed in %.1f s, hottest %.2f\n", naif, f.seconds, f.hottest);
    }

    SECTION("climbing out from a surface on autopilot stays cool too");
    {
        const auto bodies = world(399, kEarthR);
        Ship ship;
        ship.place(kAt + Vec3{0, 4 * kEarthR, 0}, {0, -1, 0});
        ship.setTarget(1);
        ship.engageLanding(true);
        fly(ship, bodies, 60);
        CHECK(ship.landed() == 1);
        std::vector<FlightBody> two = bodies;
        two.push_back({kAt + Vec3{4e5, 0, 0}, 1737.4, 12 * 1737.4, true, {}, true, {}, 1});  // a Moon to fly to
        ship.setTarget(2);
        CHECK(ship.engageAutopilot(true));
        double hottest = 0;
        for (double t = 0; t < 60; t += kDt) {
            ship.step(kDt, {}, two);
            hottest = std::max(hottest, ship.heat());
        }
        CHECK(ship.touchdown().grade != Touchdown::Burned);
        CHECK_MSG(hottest < 0.5, std::to_string(hottest));
        CHECK(ship.air() < 0);  // out of the air by now
    }

    SECTION("the same at any drawn size: an Earth drawn 500 times larger burns at 500 times the speed");
    {
        const auto big = world(399, kEarthR, 500);
        Ship slow = level(big, 60, 2, 1000.0 / kLightKmPerS);  // 1,000 drawn km/s = 2 real km/s
        Flight f = fly(slow, big, 5);
        CHECK(!f.burned && f.hottest < 0.05);
        Ship fast = level(big, 60, 2, 20000.0 / kLightKmPerS);  // 20,000 drawn km/s = 40 real km/s
        f = fly(fast, big, 5);
        CHECK(f.burned);
    }

    return check::finish();
}
