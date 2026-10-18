#include "orrery/kepler.hpp"

#include <cmath>

#include "orrery/time.hpp"

namespace orrery {

double solveKepler(double M, double e) {
    if (e < 1.0) {
        // Reduce M to [-pi, pi]; E has the same sign and E - M is periodic.
        const double twoPi = 2 * kPi;
        const double turns = std::floor((M + kPi) / twoPi);
        const double m = M - turns * twoPi;
        double E = e < 0.8 ? m : (m >= 0 ? kPi : -kPi);
        for (int k = 0; k < 60; ++k) {
            const double f = E - e * std::sin(E) - m;
            const double step = f / (1 - e * std::cos(E));
            E -= step;
            if (std::fabs(step) < 1e-14) break;
        }
        return E + turns * twoPi;
    }
    // Hyperbolic.
    double H = std::asinh(M / e);
    for (int k = 0; k < 100; ++k) {
        const double f = e * std::sinh(H) - H - M;
        const double step = f / (e * std::cosh(H) - 1);
        H -= step;
        if (std::fabs(step) < 1e-14 * std::max(1.0, std::fabs(H))) break;
    }
    return H;
}

namespace {

// Perifocal -> reference plane: Rz(node) Rx(i) Rz(peri).
Vec3 rotateFromPerifocal(const Elements& el, double xp, double yp) {
    const double O = el.node * kDegToRad, w = el.peri * kDegToRad, i = el.i * kDegToRad;
    const double cO = std::cos(O), sO = std::sin(O), cw = std::cos(w), sw = std::sin(w), ci = std::cos(i),
                 si = std::sin(i);
    const double x1 = cw * xp - sw * yp, y1 = sw * xp + cw * yp;
    const double y2 = ci * y1, z2 = si * y1;
    return {cO * x1 - sO * y2, sO * x1 + cO * y2, z2};
}

}  // namespace

Vec3 positionAtEccentricAnomaly(const Elements& el, double E) {
    const double b = el.a * std::sqrt(std::max(0.0, 1 - el.e * el.e));
    return rotateFromPerifocal(el, el.a * (std::cos(E) - el.e), b * std::sin(E));
}

Vec3 positionAtMeanAnomaly(const Elements& el, double M) {
    if (el.e < 1.0) return positionAtEccentricAnomaly(el, solveKepler(M, el.e));
    const double H = solveKepler(M, el.e);
    const double a = el.a;  // negative
    return rotateFromPerifocal(el, a * (std::cosh(H) - el.e), -a * std::sqrt(el.e * el.e - 1) * std::sinh(H));
}

Vec3 keplerPosition(const Elements& el, double jdTdb) {
    const double M = (el.m + el.n * (jdTdb - el.epochJd)) * kDegToRad;
    return positionAtMeanAnomaly(el, M);
}

Elements elementsFromState(Vec3 r, Vec3 v, double gm, double jdTdb) {
    Elements el;
    el.epochJd = jdTdb;
    const double rl = length(r);
    const Vec3 h = cross(r, v);
    const double hl = length(h);
    const Vec3 nodeVec = cross(Vec3{0, 0, 1}, h);
    const Vec3 ev = (cross(v, h) / gm) - r / rl;
    el.e = length(ev);
    const double energy = dot(v, v) / 2 - gm / rl;
    el.a = -gm / (2 * energy);
    el.i = std::acos(std::fmax(-1.0, std::fmin(1.0, h.z / hl))) / kDegToRad;
    const double nl = length(nodeVec);
    el.node = nl > 1e-12 ? std::atan2(nodeVec.y, nodeVec.x) / kDegToRad : 0.0;
    // Argument of periapsis and true anomaly, measured in the orbit plane.
    const Vec3 nodeDir = nl > 1e-12 ? nodeVec / nl : Vec3{1, 0, 0};
    const Vec3 inPlane = cross(normalize(h), nodeDir);
    double nu;
    if (el.e > 1e-10) {
        el.peri = std::atan2(dot(ev, inPlane), dot(ev, nodeDir)) / kDegToRad;
        const Vec3 eDir = ev / el.e;
        nu = std::atan2(dot(cross(eDir, r), normalize(h)), dot(eDir, r));
    } else {
        el.peri = 0;
        nu = std::atan2(dot(r, inPlane), dot(r, nodeDir));
    }
    double M;
    if (el.e < 1) {
        const double E = 2 * std::atan(std::sqrt((1 - el.e) / (1 + el.e)) * std::tan(nu / 2));
        M = E - el.e * std::sin(E);
    } else {
        const double H = 2 * std::atanh(std::sqrt((el.e - 1) / (el.e + 1)) * std::tan(nu / 2));
        M = el.e * std::sinh(H) - H;
    }
    el.m = M / kDegToRad;
    el.n = std::sqrt(gm / std::fabs(el.a * el.a * el.a)) / kDegToRad * kSecondsPerDay;
    return el;
}

}  // namespace orrery
