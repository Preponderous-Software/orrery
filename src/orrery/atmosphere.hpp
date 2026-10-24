// The air of the solid bodies that have any to speak of: Venus, Earth, Mars and
// Titan, and the thin air of Pluto and Triton. Each is one exponential layer, density = surface density *
// exp(-altitude / scale height), cut off where it thins to kAirTopDensity.
// The surface densities and scale heights are real (DATA_SOURCES.md); the
// single layer, the cut-off and the sky colours are game choices.
#pragma once

namespace orrery {

constexpr double kAirTopDensity = 1e-6;  // kg/m^3: above this, no air

struct Atmosphere {
    double surfaceDensity = 0;  // kg/m^3; 0 = no air
    double scaleHeightKm = 0;
    float sky[3] = {0, 0, 0};   // the daytime sky from the surface
    double surfaceGravity = 0;  // m/s^2, for gliding; set by the caller (0 = no gliding)
    bool present() const { return surfaceDensity > 0; }
    double topKm() const;                    // where the density falls to kAirTopDensity
    double density(double altitudeKm) const;  // kg/m^3; altitude in real km
};

// The air of a body by NAIF ID; none for every other body. The giant planets
// have no surface, so the ship is lost in them before their air matters.
Atmosphere atmosphereOf(long naif);

// Heating: the speed (real km/s) at which flight held in air of this density
// heats the ship to burning up. A game rule with the shape of the
// Sutton-Graves law (heating ~ sqrt(density) * speed^3): 5 km/s in Earth's
// air at sea level, about 16 km/s at 60 km, 50 km/s at its top.
double burnSpeed(double density);

}  // namespace orrery
