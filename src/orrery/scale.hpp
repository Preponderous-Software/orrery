// How real positions and sizes become what is drawn.
//
// The real solar system cannot be shown at one scale: at true scale Earth is
// 1/23,000 of its distance from the Sun. Two modes are offered and the app
// always says which one is on screen.
//
// TRUE: every position is the real one (km). Body radii are multiplied by an
// enlargement factor k (the Sun by min(k, kSunEnlargeCap) so it does not
// swallow the inner planets). At k = 1 everything is true to scale. Moons
// keep their true distances, so a large k can hide a moon inside its planet.
//
// COMPRESSED: NOT TO SCALE. Distances from the Sun are compressed
// logarithmically, f(r) = L log10(1 + r / r0), directions kept; radii grow as
// the square root of the true radius; a moon at distance d from its planet
// (true radius R, drawn radius R') is drawn at R' (1 + 2 log10(d / R)).
// Order and direction are preserved, so who is nearer whom stays right.
#pragma once

#include "orrery/vec.hpp"

namespace orrery {

enum class ScaleMode { True, Compressed };

constexpr double kSunEnlargeCap = 20.0;
// COMPRESSED constants: f(r) = kLogScaleKm * log10(1 + r / kLogR0Km).
constexpr double kLogScaleKm = 5.0 * kAuKm;
constexpr double kLogR0Km = 0.3 * kAuKm;
// Drawn radius = kSqrtRadiusCoefficient * sqrt(radius_km) (km): Jupiter ~0.12 au.
constexpr double kSqrtRadiusCoefficient = 0.12 * kAuKm / 264.4;
constexpr double kMoonLogSpread = 2.0;

struct Scale {
    ScaleMode mode = ScaleMode::True;
    double enlarge = 500.0;  // TRUE: body radius factor

    // Drawn radius of a body of true radius r (km). isSun applies the Sun's cap.
    double radius(double radiusKm, bool isSun) const;
    // Drawn position of an object that orbits the Sun.
    Vec3 heliocentric(Vec3 truePos, Vec3 sunTrue, Vec3 sunDrawn) const;
    // Drawn position of an object that orbits a planet (or any parent other than the Sun).
    Vec3 satellite(Vec3 truePos, Vec3 parentTrue, Vec3 parentDrawn, double parentRadiusKm) const;
    // Drawn distance of a ring of true radius ringKm around a planet of true radius planetKm.
    double ringRadius(double ringKm, double planetKm) const;

    // COMPRESSED only: the radial maps on their own (exposed for tests).
    static double compressDistance(double km);
    double satelliteDistance(double km, double parentRadiusKm) const;
};

}  // namespace orrery
