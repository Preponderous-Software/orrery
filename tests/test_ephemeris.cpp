// Accuracy against JPL Horizons. Every reference vector in tests/data/vectors
// is fetched from Horizons at build time (tools/fetch_data.py, then
// tools/build_catalog.py; nothing is committed) and is compared with what
// the orrery computes from DE440s, the element tables, the trajectories and
// the SBDB elements. Tolerances are per source and printed with each result.
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

#include "check.hpp"
#include "orrery/csv.hpp"
#include "orrery/system.hpp"
#include "orrery/time.hpp"

#ifndef ORRERY_SOURCE_DIR
#define ORRERY_SOURCE_DIR "."
#endif

using namespace orrery;

namespace {

const std::string kRoot = ORRERY_SOURCE_DIR;

// Compares `compute(jd)` with every row of a reference file.
void compare(const char* name, double toleranceKm, const std::function<Vec3(double)>& compute) {
    const CsvTable t = readCsv(kRoot + "/tests/data/vectors/" + name + ".csv");
    double worst = 0;
    for (const auto& row : t.rows) {
        const double jd = t.number(row, "jd_tdb");
        const Vec3 expected{t.number(row, "x_km"), t.number(row, "y_km"), t.number(row, "z_km")};
        worst = std::fmax(worst, length(compute(jd) - expected));
    }
    std::printf("  %-15s worst %12.3f km over %zu epochs (tolerance %g km)\n", name, worst, t.rows.size(), toleranceKm);
    CHECK_MSG(worst <= toleranceKm, std::string(name) + ": " + std::to_string(worst) + " km");
}

}  // namespace

int main() {
    const SolarSystem sys(kRoot + "/data");
    const SpkFile& spk = sys.kernel();
    auto de = [&](int target, int center) {
        return [&spk, target, center](double jd) {
            return icrfToEcliptic(spk.position(target, center, (jd - kJ2000) * kSecondsPerDay));
        };
    };
    auto body = [&](const char* n) {
        const int i = sys.find(n);
        if (i < 0) std::printf("  no body %s\n", n);
        return i;
    };
    auto relative = [&](int child, int parent) {
        return [&sys, child, parent](double jd) {
            SystemState s;
            sys.evaluate(jd, s, false);
            return s.positions[static_cast<size_t>(child)] - s.positions[static_cast<size_t>(parent)];
        };
    };
    auto absolute = [&](int b) {
        return [&sys, b](double jd) {
            SystemState s;
            sys.evaluate(jd, s, false);
            return s.positions[static_cast<size_t>(b)];
        };
    };

    SECTION("DE440s through our SPK reader vs Horizons (DE441), SSB-relative, 1950-2100");
    // Horizons serves DE441 for these; DE440 and DE441 agree to well under a
    // kilometre for the planets over this span, so 2 km leaves room only for
    // that difference.
    compare("sun", 2.0, de(10, 0));
    compare("mercury", 2.0, de(199, 0));
    compare("venus", 2.0, de(299, 0));
    compare("earth", 2.0, de(399, 0));
    compare("mars_bary", 2.0, de(4, 0));
    compare("jupiter_bary", 2.0, de(5, 0));
    compare("saturn_bary", 2.0, de(6, 0));
    compare("uranus_bary", 5.0, de(7, 0));
    compare("neptune_bary", 5.0, de(8, 0));
    compare("pluto_bary", 50.0, de(9, 0));
    compare("moon_geo", 1.0, de(301, 399));

    SECTION("planet centres: DE barycentre minus the catalogued moons' pull");
    // Small moons left out of the catalogue (Jupiter's inner moons, Nix,
    // Hydra...) and the element-table interpolation set the floor here.
    compare("jupiter_center", 25.0, absolute(body("Jupiter")));
    compare("pluto_center", 25.0, absolute(body("Pluto")));

    SECTION("moons from interpolated Horizons element tables (30-day samples)");
    compare("phobos", 15.0, relative(body("Phobos"), body("Mars")));
    compare("io", 1000.0, relative(body("Io"), body("Jupiter")));
    compare("titan", 300.0, relative(body("Titan"), body("Saturn")));
    compare("hyperion", 5000.0, relative(body("Hyperion"), body("Saturn")));
    compare("triton", 30.0, relative(body("Triton"), body("Neptune")));
    compare("charon", 15.0, relative(body("Charon"), body("Pluto")));

    SECTION("dwarf planets and a comet from interpolated heliocentric tables");
    compare("ceres", 5000.0, relative(body("Ceres"), sys.sun()));
    compare("eris", 5000.0, relative(body("Eris"), sys.sun()));
    compare("halley", 20000.0, relative(body("Halley"), sys.sun()));

    SECTION("spacecraft: Hermite interpolation of 10-day Horizons vectors");
    compare("voyager1", 50.0, relative(body("Voyager 1"), sys.sun()));

    SECTION("two-body propagation of SBDB elements (Vesta) vs Horizons' perturbed orbit");
    {
        // The SBDB elements are fetched at build time, so Vesta is found by a
        // window around its orbit (a 2.3614 au, e 0.0902, i 7.14 deg) wide
        // enough for any later SBDB solution, not by exact values. No other
        // numbered main-belt asteroid with H < 13 falls inside it.
        const SmallBody* vesta = nullptr;
        int matches = 0;
        for (const SmallBody& sb : sys.smallBodies()) {
            if (sys.groups()[static_cast<size_t>(sb.group)].id == "main_belt" &&
                std::fabs(sb.elements.a - 2.3614) < 0.002 && std::fabs(sb.elements.e - 0.0902) < 0.002 &&
                std::fabs(sb.elements.i - 7.14) < 0.05) {
                vesta = &sb;
                ++matches;
            }
        }
        CHECK(vesta != nullptr);
        CHECK(matches == 1);
        if (vesta) {
            const CsvTable t = readCsv(kRoot + "/tests/data/vectors/vesta.csv");
            for (size_t k = 0; k < t.rows.size(); ++k) {
                const double jd = t.number(t.rows[k], "jd_tdb");
                const Vec3 expected{t.number(t.rows[k], "x_km"), t.number(t.rows[k], "y_km"),
                                    t.number(t.rows[k], "z_km")};
                const double err = length(keplerPosition(vesta->elements, jd) * kAuKm - expected);
                // At the epoch only the elements' rounding; a year off, Jupiter's pull; ten years off, more.
                const double dt = std::fabs(jd - vesta->elements.epochJd);
                const double tolerance = dt < 1 ? 1000.0 : dt < 400 ? 1.0e5 : 3.0e6;
                std::printf("  vesta at JD %.2f (%+.0f d from the SBDB epoch): %.1f km (tolerance %g km)\n", jd,
                            jd - vesta->elements.epochJd, err, tolerance);
                CHECK(err <= tolerance);
            }
        }
    }

    SECTION("catalogue queries: find, the kernel's range, osculating orbits, poles and frames");
    {
        // find: case-insensitive, then any name containing the key.
        CHECK(sys.find("JUPITER") == sys.find("Jupiter") && sys.find("Jupiter") > 0);
        CHECK(sys.find("halley") >= 0 && sys.bodies()[static_cast<size_t>(sys.find("halley"))].name == "1P/Halley");
        CHECK(sys.find("voyager") == sys.find("Voyager 1"));
        CHECK(sys.find("Vulcan") == -1);

        // The usable range sits a day inside DE440s's coverage (1849-12-26 to
        // 2150-01-22), and the whole system evaluates at both ends.
        CHECK(sys.firstJd() > julianDate({1849, 12, 26, 0, 0, 0}) && sys.firstJd() < julianDate({1849, 12, 28, 0, 0, 0}));
        CHECK(sys.lastJd() > julianDate({2150, 1, 20, 0, 0, 0}) && sys.lastJd() < julianDate({2150, 1, 22, 0, 0, 0}));
        for (double jd : {sys.firstJd(), sys.lastJd()}) {
            SystemState s;
            sys.evaluate(jd, s, false);
            CHECK(std::isfinite(length(s.positions[static_cast<size_t>(body("Earth"))])));
        }

        const double jd = julianDate({2026, 10, 18, 0, 0, 0});
        SystemState state;
        sys.evaluate(jd, state, false);
        Elements el;
        CHECK(!sys.osculating(sys.sun(), jd, state, el));
        CHECK(!sys.osculating(body("Voyager 1"), jd, state, el));
        // Earth around the Sun, from its DE state: the Moon moves Earth's
        // centre about 12 m/s, so its elements wobble about the EMB's.
        CHECK(sys.osculating(body("Earth"), jd, state, el));
        std::printf("  Earth osculating: a %.6f au, e %.5f, i %.4f deg\n", el.a / kAuKm, el.e, el.i);
        CHECK(std::fabs(el.a / kAuKm - 1.0) < 0.01 && std::fabs(el.e - 0.0167) < 0.003 && el.i < 0.05);
        // The Moon around Earth (osculating, so the Sun's pull moves a and e).
        CHECK(sys.osculating(body("Moon"), jd, state, el));
        std::printf("  Moon osculating: a %.0f km, e %.4f, i %.3f deg\n", el.a, el.e, el.i);
        CHECK(el.a > 370000 && el.a < 400000 && el.e < 0.1 && std::fabs(el.i - 5.15) < 0.4);
        // A table body's elements are its table's.
        const int io = body("Io");
        CHECK(sys.osculating(io, jd, state, el));
        const Elements table = sys.bodies()[static_cast<size_t>(io)].table.at(jd);
        CHECK(el.a == table.a && el.e == table.e && el.i == table.i && el.node == table.node && el.m == table.m);
        CHECK(el.a > 415000 && el.a < 430000);

        // Poles: none for spacecraft, comets or the dwarf planets with
        // literature radii; a unit vector for every body that has one.
        Vec3 p;
        CHECK(!sys.pole(body("Voyager 1"), jd, p) && !sys.pole(body("Halley"), jd, p) && !sys.pole(body("Eris"), jd, p));
        for (size_t i = 0; i < sys.bodies().size(); ++i) {
            if (sys.pole(static_cast<int>(i), jd, p)) CHECK_MSG(std::fabs(length(p) - 1) < 1e-12, sys.bodies()[i].name);
        }
        CHECK(sys.pole(body("Earth"), kJ2000, p));
        CHECK(std::fabs(std::acos(p.z) / kDegToRad - 23.44) < 0.01);  // the J2000 obliquity
        // The angle the HUD prints as "axial tilt": the spin pole against the
        // current orbit's normal. Venus's and Uranus's IAU north poles are on
        // the ecliptic's north side but they spin backwards (W decreases), so
        // their spin poles are the IAU poles reversed and their obliquities
        // are the usual 177.36 and 97.77 deg rather than 180 deg minus those.
        auto tilt = [&](const char* name) {
            double degrees = -1;
            CHECK_MSG(sys.axialTilt(body(name), jd, state, degrees), name);
            return degrees;
        };
        const double earthTilt = tilt("Earth"), venusTilt = tilt("Venus"), uranusTilt = tilt("Uranus");
        std::printf("  axial tilt: Earth %.3f deg, Venus %.3f deg, Uranus %.3f deg\n", earthTilt, venusTilt, uranusTilt);
        CHECK(std::fabs(earthTilt - 23.44) < 0.05);
        CHECK(sys.bodies()[static_cast<size_t>(body("Venus"))].pmRate < 0);
        CHECK(sys.bodies()[static_cast<size_t>(body("Uranus"))].pmRate < 0);
        CHECK(std::fabs(venusTilt - 177.36) < 0.5);
        CHECK(std::fabs(uranusTilt - 97.77) < 0.5);
        // None without a pole (Eris) or an orbit (Voyager 1).
        double none = 0;
        CHECK(!sys.axialTilt(body("Eris"), jd, state, none) && !sys.axialTilt(body("Voyager 1"), jd, state, none));

        // Frames: +Z is the pole; the prime meridian comes back after one
        // sidereal day and is on the far side after half of one.
        BodyFrame f, later, half;
        CHECK(!sys.frame(body("Voyager 1"), jd, f));
        const int earth = body("Earth");
        CHECK(sys.frame(earth, jd, f) && sys.pole(earth, jd, p));
        CHECK(length(f.z - p) < 1e-12);
        const double sidereal = 360.0 / sys.bodies()[static_cast<size_t>(earth)].pmRate;
        CHECK(sys.frame(earth, jd + sidereal, later) && sys.frame(earth, jd + sidereal / 2, half));
        CHECK(dot(f.x, later.x) > 1 - 1e-9);
        CHECK(dot(f.x, half.x) < -1 + 1e-6);
    }
    return check::finish();
}
