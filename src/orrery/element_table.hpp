// Tables of osculating elements sampled at regular epochs (from Horizons),
// and interpolation between them.
//
// Between two samples, a, e and i are interpolated linearly, the node and the
// argument of periapsis along the shorter arc, and the mean longitude
// L = node + peri + M is unwrapped with the number of whole revolutions the
// first sample's mean motion predicts, then interpolated linearly. So the
// position is exact (to the table's precision) at every sample, continuous
// between them, and it follows the real mean motion rather than one sample's
// osculating value, which for close moons (Phobos) is off by enough to drift
// tens of degrees in a month. Outside the table the nearest sample is
// propagated two-body and `extrapolated` is set.
#pragma once

#include <string>
#include <vector>

#include "orrery/kepler.hpp"

namespace orrery {

class ElementTable {
public:
    ElementTable() = default;
    explicit ElementTable(std::vector<Elements> samples);

    // Reads a catalogue CSV (jd_tdb,a_km,e,i_deg,node_deg,peri_deg,m_deg,n_deg_per_day).
    static ElementTable load(const std::string& path);

    const std::vector<Elements>& samples() const { return samples_; }
    bool empty() const { return samples_.empty(); }
    double firstJd() const { return samples_.front().epochJd; }
    double lastJd() const { return samples_.back().epochJd; }

    // Interpolated elements at jdTdb, with epoch = jdTdb.
    Elements at(double jdTdb, bool* extrapolated = nullptr) const;
    Vec3 position(double jdTdb, bool* extrapolated = nullptr) const { return keplerPosition(at(jdTdb, extrapolated), jdTdb); }

private:
    std::vector<Elements> samples_;
};

}  // namespace orrery
