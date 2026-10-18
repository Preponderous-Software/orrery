// Kepler's equation, two-body propagation and element/state conversions.
#include <cmath>
#include <cstdio>

#include "check.hpp"
#include "orrery/element_table.hpp"
#include "orrery/kepler.hpp"
#include "orrery/time.hpp"

using namespace orrery;

int main() {
    SECTION("Kepler's equation is solved to 1e-12 for e in [0, 0.99] and hyperbolic e");
    double worst = 0;
    for (double e : {0.0, 0.01, 0.2, 0.5, 0.8, 0.95, 0.99}) {
        for (double M = -7.0; M <= 7.0; M += 0.05) {
            const double E = solveKepler(M, e);
            worst = std::fmax(worst, std::fabs(E - e * std::sin(E) - M));
        }
    }
    CHECK_MSG(worst < 1e-12, std::to_string(worst));
    for (double e : {1.2, 2.0, 5.0}) {
        for (double M = -20.0; M <= 20.0; M += 0.5) {
            const double H = solveKepler(M, e);
            CHECK(std::fabs(e * std::sinh(H) - H - M) < 1e-9 * std::fmax(1.0, std::fabs(M)));
        }
    }

    SECTION("a circular orbit moves at the mean motion and keeps its radius");
    Elements c;
    c.a = 1000;
    c.n = 36;  // ten days per orbit
    c.epochJd = kJ2000;
    const Vec3 p0 = keplerPosition(c, kJ2000), p1 = keplerPosition(c, kJ2000 + 2.5);
    CHECK(std::fabs(length(p0) - 1000) < 1e-9 && std::fabs(length(p1) - 1000) < 1e-9);
    CHECK(std::fabs(p0.x - 1000) < 1e-9 && std::fabs(p1.y - 1000) < 1e-9);  // a quarter turn later

    SECTION("perihelion and aphelion distances of an eccentric orbit");
    Elements el;
    el.a = 2.0;
    el.e = 0.5;
    el.i = 30;
    el.node = 40;
    el.peri = 50;
    el.n = 1;
    CHECK(std::fabs(length(positionAtMeanAnomaly(el, 0)) - 1.0) < 1e-12);
    CHECK(std::fabs(length(positionAtMeanAnomaly(el, kPi)) - 3.0) < 1e-12);
    // The orbit normal makes the inclination with +Z.
    const Vec3 a = positionAtMeanAnomaly(el, 0.3), b = positionAtMeanAnomaly(el, 1.3);
    CHECK(std::fabs(std::acos(normalize(cross(a, b)).z) / kDegToRad - 30.0) < 1e-9);

    SECTION("state -> elements -> state round trip (Earth-like, km and km/s)");
    const double gm = 132712440041.279419;
    const Vec3 r{-2.6e7, 1.449e8, 4.4e3}, v{-29.8, -5.4, 0.0012};
    const Elements fromState = elementsFromState(r, v, gm, kJ2000);
    const Vec3 back = keplerPosition(fromState, kJ2000);
    CHECK_MSG(length(back - r) < 1e-3, std::to_string(length(back - r)) + " km");
    CHECK(std::fabs(fromState.a / kAuKm - 1.0) < 0.02 && fromState.e < 0.05);
    // A quarter orbit later the propagated position still has the energy's semi-major axis.
    const Vec3 later = keplerPosition(fromState, kJ2000 + 91.3);
    CHECK(length(later) > fromState.a * (1 - fromState.e) - 1 && length(later) < fromState.a * (1 + fromState.e) + 1);

    SECTION("element tables: exact at samples, unwrapped mean longitude between them");
    // A synthetic table sampled from one fixed orbit must reproduce it exactly
    // between samples, even with many revolutions per sample interval.
    Elements truth;
    truth.a = 9378;
    truth.e = 0.015;
    truth.i = 27;
    truth.node = 85;
    truth.peri = 42;
    truth.m = 235;
    truth.n = 1128.0;  // Phobos-like: 94 turns per 30 days
    truth.epochJd = 2444239.5;
    std::vector<Elements> samples;
    for (int k = 0; k < 4; ++k) {
        Elements s = truth;
        s.epochJd = truth.epochJd + 30.0 * k;
        s.m = std::fmod(truth.m + truth.n * 30.0 * k, 360.0);
        samples.push_back(s);
    }
    const ElementTable table(samples);
    double worstKm = 0;
    for (double t = truth.epochJd; t <= truth.epochJd + 90.0; t += 0.37) {
        worstKm = std::fmax(worstKm, length(table.position(t) - keplerPosition(truth, t)));
    }
    CHECK_MSG(worstKm < 1e-3, std::to_string(worstKm) + " km");
    bool ex = false;
    table.position(truth.epochJd + 200.0, &ex);
    CHECK(ex);
    table.position(truth.epochJd + 45.0, &ex);
    CHECK(!ex);
    return check::finish();
}
