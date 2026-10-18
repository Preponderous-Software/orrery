// Two-body (Keplerian) orbits.
#pragma once

#include "orrery/vec.hpp"

namespace orrery {

// Osculating elements in some reference plane (the orrery uses the ecliptic
// of J2000). Angles in degrees; a in any length unit (km in the catalogue,
// au for small bodies). Hyperbolic orbits have e > 1 and a < 0.
struct Elements {
    double a = 1, e = 0, i = 0, node = 0, peri = 0;
    double m = 0;          // mean anomaly at epoch, degrees
    double n = 0;          // mean motion, degrees per day
    double epochJd = 0;    // TDB
};

// Solves Kepler's equation M = E - e sin E (e < 1) for E, radians, or the
// hyperbolic M = e sinh H - H (e > 1) for H. Newton's method from a safe
// start; converges to 1e-14 rad.
double solveKepler(double meanAnomalyRad, double e);

// Position at the elements' epoch plus `days`, relative to the focus, in the
// elements' unit and plane.
Vec3 keplerPosition(const Elements& el, double jdTdb);
// The same with M given directly (radians): for drawing a whole orbit.
Vec3 positionAtMeanAnomaly(const Elements& el, double meanAnomalyRad);
// Position on the orbit at eccentric anomaly E (radians, e < 1).
Vec3 positionAtEccentricAnomaly(const Elements& el, double eccentricAnomalyRad);

// Osculating elements from a state vector (km, km/s) and GM (km^3/s^2), at
// epoch jdTdb. n is derived from GM.
Elements elementsFromState(Vec3 r, Vec3 v, double gm, double jdTdb);

}  // namespace orrery
