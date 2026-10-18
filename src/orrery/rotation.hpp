// A body's orientation from its IAU rotation elements (pole right ascension
// and declination, prime-meridian angle W), as the WGCCRE reports define it:
// the body's +Z is the pole; its +X points at the prime meridian, W degrees
// east along the body's equator from the node where that equator crosses the
// ICRF equator ascending (right ascension = pole RA + 90 degrees); +Y = Z x X,
// so planetocentric east longitude runs counter-clockwise about +Z.
#pragma once

#include "orrery/vec.hpp"

namespace orrery {

struct BodyFrame {
    Vec3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};  // unit axes, ecliptic J2000
};

inline BodyFrame bodyFrame(double poleRaDeg, double poleDecDeg, double wDeg) {
    const Vec3 pole = raDecToIcrf(poleRaDeg, poleDecDeg);
    const Vec3 node = raDecToIcrf(poleRaDeg + 90.0, 0.0);  // on the ICRF equator, so it is perpendicular to the pole
    const Vec3 east = cross(pole, node);
    const double w = wDeg * kDegToRad;
    const Vec3 x = node * std::cos(w) + east * std::sin(w);
    BodyFrame f;
    f.z = icrfToEcliptic(pole);
    f.x = icrfToEcliptic(x);
    f.y = cross(f.z, f.x);
    return f;
}

}  // namespace orrery
