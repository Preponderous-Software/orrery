#include "orrery/atmosphere.hpp"

#include <algorithm>
#include <cmath>

namespace orrery {
namespace {

constexpr double kBurnKmPerS = 5.0;     // burn speed at kBurnDensity
constexpr double kBurnDensity = 1.217;  // Earth's air at sea level

}  // namespace

double Atmosphere::topKm() const {
    return present() && surfaceDensity > kAirTopDensity ? scaleHeightKm * std::log(surfaceDensity / kAirTopDensity)
                                                        : 0.0;
}

double Atmosphere::density(double altitudeKm) const {
    if (!present() || altitudeKm >= topKm()) return 0;
    return surfaceDensity * std::exp(-std::max(altitudeKm, 0.0) / scaleHeightKm);
}

Atmosphere atmosphereOf(long naif) {
    // Surface density (kg/m^3), scale height (km), sky colour.
    switch (naif) {
        case 299: return {65.0, 15.9, {0.86f, 0.70f, 0.42f}};    // Venus: yellow under the clouds
        case 399: return {1.217, 8.5, {0.36f, 0.58f, 0.92f}};    // Earth: blue
        case 499: return {0.016, 11.0, {0.78f, 0.58f, 0.40f}};   // Mars: butterscotch
        case 606: return {5.24, 21.0, {0.82f, 0.56f, 0.24f}};    // Titan: orange haze
        default: return {};
    }
}

double burnSpeed(double density) {
    return density > 0 ? kBurnKmPerS * std::pow(kBurnDensity / density, 1.0 / 6.0) : 1e308;
}

}  // namespace orrery
