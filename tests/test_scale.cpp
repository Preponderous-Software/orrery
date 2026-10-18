// Scale modes: TRUE keeps real positions and multiplies radii by exactly the
// factor shown on screen; COMPRESSED keeps directions and order.
#include <cmath>

#include "check.hpp"
#include "orrery/scale.hpp"

using namespace orrery;

int main() {
    const Vec3 sunTrue{1000, -2000, 30}, sunDrawn = sunTrue;
    Scale s;

    SECTION("TRUE: positions are untouched; radii scale by the stated factor");
    s.mode = ScaleMode::True;
    s.enlarge = 1.0;
    const Vec3 earth{-2.6e7, 1.449e8, 4.4e3};
    CHECK(length(s.heliocentric(earth, sunTrue, sunDrawn) - earth) == 0);
    CHECK(s.radius(6371.0, false) == 6371.0 && s.radius(695700.0, true) == 695700.0);  // k = 1 is true scale
    s.enlarge = 500;
    CHECK(s.radius(6371.0, false) == 6371.0 * 500 && s.radius(695700.0, true) == 695700.0 * kSunEnlargeCap);
    const Vec3 moon = earth + Vec3{384400, 0, 0};
    CHECK(length(s.satellite(moon, earth, earth, 6371) - moon) < 1e-6);  // true distance even when enlarged
    CHECK(s.ringRadius(136775, 58232) == 136775.0 * 500);

    SECTION("unit conversions");
    CHECK(std::fabs(kAuKm - 149597870.7) < 1e-9);
    CHECK(std::fabs(kObliquityJ2000Rad / kDegToRad - 23.4392911) < 1e-7);
    const Vec3 northIcrf{0, 0, 1};
    const Vec3 inEcliptic = icrfToEcliptic(northIcrf);  // the celestial pole is tilted toward -Y by the obliquity
    CHECK(std::fabs(std::acos(inEcliptic.z) / kDegToRad - 23.4392911) < 1e-7 && inEcliptic.y > 0);

    SECTION("COMPRESSED: f(r) = 5 au * log10(1 + r / 0.3 au), direction kept, order kept");
    s.mode = ScaleMode::Compressed;
    CHECK(Scale::compressDistance(0) == 0);
    CHECK(std::fabs(Scale::compressDistance(0.3 * kAuKm) / kAuKm - 5.0 * std::log10(2.0)) < 1e-12);
    double previous = -1;
    for (double au : {0.39, 0.72, 1.0, 1.52, 5.2, 9.5, 19.2, 30.1, 39.5, 96.0}) {
        const double d = Scale::compressDistance(au * kAuKm);
        CHECK(d > previous);
        previous = d;
    }
    // The point of the mode: Neptune/Mercury is 77 at true scale and under 6 drawn.
    CHECK(Scale::compressDistance(30.1 * kAuKm) / Scale::compressDistance(0.39 * kAuKm) < 6.0);
    const Vec3 drawn = s.heliocentric(earth, sunTrue, sunDrawn);
    const Vec3 a = normalize(earth - sunTrue), b = normalize(drawn - sunDrawn);
    CHECK(length(a - b) < 1e-12);
    CHECK(std::fabs(length(drawn - sunDrawn) - Scale::compressDistance(length(earth - sunTrue))) < 1e-3);

    SECTION("COMPRESSED: radii grow as sqrt; moons start at the drawn surface and keep their order");
    CHECK(std::fabs(s.radius(4.0, false) / s.radius(1.0, false) - 2.0) < 1e-12);
    CHECK(std::fabs(s.satelliteDistance(69911.0, 69911.0) - s.radius(69911.0, false)) < 1e-6);
    CHECK(s.satelliteDistance(421700, 69911) < s.satelliteDistance(1882700, 69911));  // Io inside Callisto
    CHECK(s.ringRadius(136775, 58232) < s.satelliteDistance(185539, 58232));          // A ring inside Mimas
    return check::finish();
}
