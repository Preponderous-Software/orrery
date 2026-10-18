// Body orientation from IAU rotation elements (src/orrery/rotation.hpp).
#include <cmath>

#include "check.hpp"
#include "orrery/rotation.hpp"

using namespace orrery;

namespace {
bool near(Vec3 a, Vec3 b, double tol) { return length(a - b) < tol; }
}  // namespace

int main() {
    SECTION("the frame is right-handed and orthonormal, with +Z on the pole");
    const BodyFrame mars = bodyFrame(317.269, 54.432, 176.049);
    CHECK(std::fabs(length(mars.x) - 1) < 1e-12 && std::fabs(length(mars.y) - 1) < 1e-12);
    CHECK(std::fabs(dot(mars.x, mars.z)) < 1e-12 && std::fabs(dot(mars.y, mars.z)) < 1e-12);
    CHECK(near(cross(mars.x, mars.y), mars.z, 1e-12));
    CHECK(near(mars.z, icrfToEcliptic(raDecToIcrf(317.269, 54.432)), 1e-12));

    SECTION("W = 0 puts the prime meridian on the ascending node (pole RA + 90 degrees)");
    const BodyFrame node = bodyFrame(40.0, 60.0, 0.0);
    CHECK(near(node.x, icrfToEcliptic(raDecToIcrf(130.0, 0.0)), 1e-12));

    SECTION("Earth: the pole is the ICRF pole (23.44 degrees from ecliptic north) and W tracks sidereal time");
    // pck00011: pole RA 0, Dec 90 at J2000; W = 190.147 + 360.9856235 d. At
    // J2000 the prime meridian sits at right ascension 90 + 190.147 = 280.147
    // degrees, which is the Greenwich sidereal angle then (280.46, less the
    // 0.31-degree offset between this secular model and GMST).
    const BodyFrame earth = bodyFrame(0.0, 90.0, 190.147);
    CHECK(std::fabs(std::acos(earth.z.z) / kDegToRad - 23.4393) < 1e-3);
    CHECK(near(earth.x, icrfToEcliptic(raDecToIcrf(280.147, 0.0)), 1e-9));
    // One sidereal day later (360 / 360.9856235 days) it is back where it was.
    const double sidereal = 360.0 / 360.9856235;
    const BodyFrame later = bodyFrame(0.0, 90.0, 190.147 + 360.9856235 * sidereal);
    CHECK(near(later.x, earth.x, 1e-9));
    // A quarter turn moves +X to where +Y was: east is counter-clockwise about the pole.
    const BodyFrame quarter = bodyFrame(0.0, 90.0, 190.147 + 90.0);
    CHECK(near(quarter.x, earth.y, 1e-9));
    return check::finish();
}
