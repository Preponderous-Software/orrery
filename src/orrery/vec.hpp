// Double-precision 3-vectors. Positions are kept in doubles everywhere and
// only become floats relative to the camera, at draw time.
#pragma once

#include <cmath>

namespace orrery {

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(double s, Vec3 a) { return a * s; }
inline Vec3 operator/(Vec3 a, double s) { return {a.x / s, a.y / s, a.z / s}; }
inline Vec3& operator+=(Vec3& a, Vec3 b) { return a = a + b; }
inline Vec3& operator-=(Vec3& a, Vec3 b) { return a = a - b; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    const double l = length(a);
    return l > 0 ? a / l : Vec3{0, 0, 1};
}

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kAuKm = 149597870.7;  // IAU 2012 Resolution B2

// The ecliptic and mean equinox of J2000 from ICRF: a rotation about +X by
// the IAU 1976 obliquity, 84381.448 arcseconds, as Horizons uses for its
// "ecliptic of J2000" frame.
constexpr double kObliquityJ2000Rad = 84381.448 / 3600.0 * kDegToRad;

inline Vec3 icrfToEcliptic(Vec3 v) {
    const double c = std::cos(kObliquityJ2000Rad), s = std::sin(kObliquityJ2000Rad);
    return {v.x, c * v.y + s * v.z, -s * v.y + c * v.z};
}

// Unit vector (ICRF) of a right ascension and declination in degrees.
inline Vec3 raDecToIcrf(double raDeg, double decDeg) {
    const double ra = raDeg * kDegToRad, dec = decDeg * kDegToRad;
    return {std::cos(dec) * std::cos(ra), std::cos(dec) * std::sin(ra), std::sin(dec)};
}

}  // namespace orrery
