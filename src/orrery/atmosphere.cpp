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
    // Surface density (kg/m^3), scale height (km), sky colour. A thin air's
    // sky is a dark one: its colour carries its brightness.
    switch (naif) {
        case 299: return {65.0, 15.9, {0.86f, 0.70f, 0.42f}, 0};    // Venus: yellow under the clouds
        case 399: return {1.217, 8.5, {0.36f, 0.58f, 0.92f}, 0};    // Earth: blue
        case 499: return {0.016, 11.0, {0.78f, 0.58f, 0.40f}, 0};   // Mars: butterscotch
        case 606: return {5.24, 21.0, {0.82f, 0.56f, 0.24f}, 0};    // Titan: orange haze
        case 801: return {1.24e-4, 14.5, {0.07f, 0.08f, 0.11f}, 0};  // Triton: all but black
        case 999: return {9.1e-5, 17.8, {0.08f, 0.10f, 0.15f}, 0};   // Pluto: a faint blue haze
        default: return {};
    }
}

double burnSpeed(double density) {
    return density > 0 ? kBurnKmPerS * std::pow(kBurnDensity / density, 1.0 / 6.0) : 1e308;
}

}  // namespace orrery
