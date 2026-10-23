// The solar system: the catalogue (data/catalog/) plus the DE440s kernel, and
// where everything is at a given TDB instant.
//
// All positions are in km, in the ecliptic and mean equinox of J2000,
// relative to the solar-system barycentre.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "orrery/element_table.hpp"
#include "orrery/rotation.hpp"
#include "orrery/spk.hpp"
#include "orrery/vec.hpp"

namespace orrery {

enum class BodyKind { Star, Planet, Dwarf, Moon, Comet, Spacecraft };
enum class BodySource { Spk, Table, Trajectory };

struct TrajectorySample {
    double jd = 0;
    Vec3 position, velocity;  // heliocentric, km and km/s
};

struct Body {
    std::string name;
    long naif = 0;
    BodyKind kind = BodyKind::Planet;
    long parentNaif = 0;
    int parent = -1;  // index into SolarSystem::bodies(), -1 for the Sun
    BodySource source = BodySource::Spk;
    int spkTarget = 0;
    ElementTable table;                      // Table
    std::vector<TrajectorySample> trajectory;  // Trajectory
    double radiusKm = 0;                     // 0: unknown (comets, spacecraft)
    std::string radiusSource;
    bool hasPole = false;
    double poleRa0 = 0, poleRaRate = 0, poleDec0 = 0, poleDecRate = 0, pmW0 = 0, pmRate = 0;
    double gm = 0, systemGm = 0;  // km^3/s^2, 0 when unknown
    double rotationPeriodHours = 0;  // from pm_rate, or the catalogue's rotation_period_h
    std::string rotationSource;
    float color[3] = {1, 1, 1};      // linear RGB
};

struct SmallBodyGroup {
    std::string id, label;
    int count = 0;
    float color[3] = {1, 1, 1};
};

struct SmallBody {
    int group = 0;
    Elements elements;  // a in au, heliocentric ecliptic J2000
};

struct Ring {
    int planet = -1;
    std::string name;
    double innerKm = 0, outerKm = 0;
    float color[3] = {1, 1, 1};
    float alpha = 1;
};

struct SystemState {
    double jdTdb = 0;
    std::vector<Vec3> positions;     // per body
    std::vector<bool> valid;         // false: a spacecraft outside its trajectory
    std::vector<bool> extrapolated;  // true: outside the body's element table
    std::vector<Vec3> smallBodies;   // per small body
};

class SolarSystem {
public:
    // dataDir holds catalog/ (and kernels/de440s.bsp unless kernelPath is given).
    SolarSystem(const std::string& dataDir, const std::string& kernelPath = "");

    const std::vector<Body>& bodies() const { return bodies_; }
    const std::vector<SmallBodyGroup>& groups() const { return groups_; }
    const std::vector<SmallBody>& smallBodies() const { return smallBodies_; }
    const std::vector<Ring>& rings() const { return rings_; }
    const SpkFile& kernel() const { return *spk_; }
    int find(const std::string& name) const;  // case-insensitive; -1 if absent
    int sun() const { return 0; }

    // The JD (TDB) range the DE440s kernel covers, a little inside its ends.
    double firstJd() const;
    double lastJd() const;

    void evaluate(double jdTdb, SystemState& out, bool withSmallBodies = true) const;

    // Velocity (km/s, SSB-relative) of a body, by central difference of
    // positions one minute apart (DE bodies use the kernel directly).
    Vec3 velocity(int body, double jdTdb) const;

    // Osculating elements of a body around its parent (km, degrees) at jdTdb,
    // for drawing its orbit. False for spacecraft.
    bool osculating(int body, double jdTdb, const SystemState& state, Elements& out) const;

    // The body's north pole (unit vector, ecliptic J2000) at jdTdb, or false.
    bool pole(int body, double jdTdb, Vec3& out) const;
    // The body's orientation (pole and prime meridian, ecliptic J2000) at
    // jdTdb from its IAU elements (secular terms only), or false.
    bool frame(int body, double jdTdb, BodyFrame& out) const;

private:
    Vec3 spkPosition(int target, double jdTdb) const;  // ecliptic, SSB-relative

    std::unique_ptr<SpkFile> spk_;
    std::vector<Body> bodies_;
    std::vector<SmallBodyGroup> groups_;
    std::vector<SmallBody> smallBodies_;
    std::vector<Ring> rings_;
};

const char* kindName(BodyKind kind);
// Whether a body has a solid surface to land on: not the Sun, and not Jupiter,
// Saturn, Uranus or Neptune (NAIF 599, 699, 799, 899), whose IAU radius is
// the 1 bar level in their atmospheres rather than ground.
bool hasSolidSurface(BodyKind kind, long naif);

}  // namespace orrery
