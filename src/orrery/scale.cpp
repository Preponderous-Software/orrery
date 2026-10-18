#include "orrery/scale.hpp"

#include <algorithm>
#include <cmath>

namespace orrery {

double Scale::compressDistance(double km) { return kLogScaleKm * std::log10(1.0 + km / kLogR0Km); }

double Scale::radius(double radiusKm, bool isSun) const {
    if (mode == ScaleMode::True) return radiusKm * (isSun ? std::min(enlarge, kSunEnlargeCap) : enlarge);
    return kSqrtRadiusCoefficient * std::sqrt(std::max(radiusKm, 0.0));
}

Vec3 Scale::heliocentric(Vec3 truePos, Vec3 sunTrue, Vec3 sunDrawn) const {
    if (mode == ScaleMode::True) return truePos;
    const Vec3 r = truePos - sunTrue;
    const double d = length(r);
    if (d <= 0) return sunDrawn;
    return sunDrawn + r * (compressDistance(d) / d);
}

double Scale::satelliteDistance(double km, double parentRadiusKm) const {
    const double drawnParent = radius(parentRadiusKm, false);
    const double ratio = std::max(km / std::max(parentRadiusKm, 1e-9), 1.0);
    return drawnParent * (1.0 + kMoonLogSpread * std::log10(ratio));
}

Vec3 Scale::satellite(Vec3 truePos, Vec3 parentTrue, Vec3 parentDrawn, double parentRadiusKm) const {
    if (mode == ScaleMode::True) return parentDrawn + (truePos - parentTrue);
    const Vec3 r = truePos - parentTrue;
    const double d = length(r);
    if (d <= 0) return parentDrawn;
    return parentDrawn + r * (satelliteDistance(d, parentRadiusKm) / d);
}

double Scale::ringRadius(double ringKm, double planetKm) const {
    if (mode == ScaleMode::True) return ringKm * enlarge;
    return satelliteDistance(ringKm, planetKm);
}

}  // namespace orrery
