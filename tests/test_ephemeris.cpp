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
    return check::finish();
}
