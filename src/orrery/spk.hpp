// A reader for JPL SPK kernels (NAIF DAF files) holding Chebyshev position
// segments (SPK type 2), which is how the DE planetary ephemerides are
// distributed. It reads the whole file into memory and evaluates positions
// in double precision; there is no dependency on the SPICE toolkit.
//
// Format reference: NAIF "DAF Required Reading" and "SPK Required Reading".
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "orrery/vec.hpp"

namespace orrery {

class SpkFile {
public:
    struct Segment {
        double startEt = 0, endEt = 0;  // TDB seconds past J2000
        int target = 0, center = 0, frame = 0, type = 0;
        // Type 2 directory, from the segment's last four doubles.
        double init = 0, intervalLength = 0;
        int recordSize = 0, recordCount = 0;
        size_t firstWord = 0;  // index into words_ of the first record
    };

    // Throws std::runtime_error on a missing, unreadable or unsupported file.
    explicit SpkFile(const std::string& path);

    const std::vector<Segment>& segments() const { return segments_; }

    // Position (km) and velocity (km/s) of target relative to center, in the
    // segment's frame (ICRF for DE kernels), at `et` TDB seconds past J2000.
    // Chains through the segment tree (e.g. Moon -> Earth-Moon barycentre ->
    // solar-system barycentre <- Earth). Throws if a link is missing or et is
    // outside the coverage.
    void state(int target, int center, double et, Vec3& position, Vec3* velocity = nullptr) const;
    Vec3 position(int target, int center, double et) const {
        Vec3 p;
        state(target, center, et, p);
        return p;
    }

    // The time span every segment covers.
    double coverageStartEt() const { return coverageStart_; }
    double coverageEndEt() const { return coverageEnd_; }

private:
    const Segment* find(int target, double et) const;
    void evaluate(const Segment& s, double et, Vec3& position, Vec3* velocity) const;
    // State of `body` relative to the solar-system barycentre (0).
    void stateFromBarycentre(int body, double et, Vec3& position, Vec3* velocity) const;

    std::vector<double> words_;
    std::vector<Segment> segments_;
    double coverageStart_ = 0, coverageEnd_ = 0;
};

}  // namespace orrery
