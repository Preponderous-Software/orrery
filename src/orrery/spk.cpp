#include "orrery/spk.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace orrery {
namespace {

constexpr size_t kRecordBytes = 1024;

[[noreturn]] void bad(const std::string& path, const std::string& what) {
    throw std::runtime_error(path + ": " + what);
}

}  // namespace

SpkFile::SpkFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) bad(path, "cannot open (run tools/fetch_data.py kernels)");
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < kRecordBytes || bytes.size() % 8 != 0) bad(path, "not a DAF file");
    if (std::memcmp(bytes.data(), "DAF/SPK ", 8) != 0) bad(path, "not an SPK file (no DAF/SPK id word)");
    if (std::memcmp(bytes.data() + 88, "LTL-IEEE", 8) != 0) {
        bad(path, "only little-endian (LTL-IEEE) SPK files are supported");
    }
    int32_t nd = 0, ni = 0, fward = 0;
    std::memcpy(&nd, bytes.data() + 8, 4);
    std::memcpy(&ni, bytes.data() + 12, 4);
    std::memcpy(&fward, bytes.data() + 76, 4);
    if (nd != 2 || ni != 6) bad(path, "unexpected summary format (ND=2, NI=6 expected for SPK)");

    words_.resize(bytes.size() / 8);
    std::memcpy(words_.data(), bytes.data(), bytes.size());

    // Summary records: a linked list of 1024-byte records, each holding
    // NEXT, PREV, NSUM and then NSUM summaries of ND doubles + NI ints.
    const size_t summaryWords = static_cast<size_t>(nd) + static_cast<size_t>((ni + 1) / 2);
    int record = fward;
    bool first = true;
    while (record > 0) {
        const size_t base = static_cast<size_t>(record - 1) * (kRecordBytes / 8);
        if (base + 3 > words_.size()) bad(path, "summary record out of range");
        const int next = static_cast<int>(words_[base]);
        const int count = static_cast<int>(words_[base + 2]);
        for (int i = 0; i < count; ++i) {
            const size_t at = base + 3 + static_cast<size_t>(i) * summaryWords;
            Segment s;
            s.startEt = words_[at];
            s.endEt = words_[at + 1];
            int32_t ints[6];
            std::memcpy(ints, &words_[at + 2], sizeof ints);
            s.target = ints[0];
            s.center = ints[1];
            s.frame = ints[2];
            s.type = ints[3];
            const size_t begin = static_cast<size_t>(ints[4]) - 1, end = static_cast<size_t>(ints[5]) - 1;
            if (end >= words_.size() || begin > end) bad(path, "segment addresses out of range");
            if (s.type != 2) continue;  // only Chebyshev position segments are used
            s.init = words_[end - 3];
            s.intervalLength = words_[end - 2];
            s.recordSize = static_cast<int>(words_[end - 1]);
            s.recordCount = static_cast<int>(words_[end]);
            s.firstWord = begin;
            if (s.recordSize < 5 || (s.recordSize - 2) % 3 != 0 || s.recordCount < 1 ||
                begin + static_cast<size_t>(s.recordSize) * static_cast<size_t>(s.recordCount) > end - 3) {
                bad(path, "malformed type 2 segment for target " + std::to_string(s.target));
            }
            if (first) {
                coverageStart_ = s.startEt;
                coverageEnd_ = s.endEt;
                first = false;
            } else {
                coverageStart_ = std::max(coverageStart_, s.startEt);
                coverageEnd_ = std::min(coverageEnd_, s.endEt);
            }
            segments_.push_back(s);
        }
        record = next;
    }
    if (segments_.empty()) bad(path, "no type 2 segments");
}

const SpkFile::Segment* SpkFile::find(int target, double et) const {
    for (const Segment& s : segments_) {
        if (s.target == target && et >= s.startEt && et <= s.endEt) return &s;
    }
    return nullptr;
}

void SpkFile::evaluate(const Segment& s, double et, Vec3& position, Vec3* velocity) const {
    int index = static_cast<int>(std::floor((et - s.init) / s.intervalLength));
    if (index < 0) index = 0;
    if (index >= s.recordCount) index = s.recordCount - 1;
    const double* rec = &words_[s.firstWord + static_cast<size_t>(index) * static_cast<size_t>(s.recordSize)];
    const double mid = rec[0], radius = rec[1];
    const int n = (s.recordSize - 2) / 3;  // coefficients per component
    const double x = (et - mid) / radius;
    // Chebyshev T_k(x) and their derivatives by the recurrences.
    double t[64], dt[64];
    if (n > 64) throw std::runtime_error("SPK record degree too high");
    t[0] = 1;
    dt[0] = 0;
    if (n > 1) {
        t[1] = x;
        dt[1] = 1;
    }
    for (int k = 2; k < n; ++k) {
        t[k] = 2 * x * t[k - 1] - t[k - 2];
        dt[k] = 2 * t[k - 1] + 2 * x * dt[k - 1] - dt[k - 2];
    }
    double p[3] = {0, 0, 0}, v[3] = {0, 0, 0};
    for (int c = 0; c < 3; ++c) {
        const double* coef = rec + 2 + c * n;
        for (int k = 0; k < n; ++k) {
            p[c] += coef[k] * t[k];
            v[c] += coef[k] * dt[k];
        }
        v[c] /= radius;
    }
    position = {p[0], p[1], p[2]};
    if (velocity) *velocity = {v[0], v[1], v[2]};
}

void SpkFile::stateFromBarycentre(int body, double et, Vec3& position, Vec3* velocity) const {
    position = {};
    if (velocity) *velocity = {};
    int current = body;
    for (int depth = 0; current != 0; ++depth) {
        if (depth > 8) throw std::runtime_error("SPK segment chain too deep");
        const Segment* s = find(current, et);
        if (!s) {
            throw std::runtime_error("no SPK segment for body " + std::to_string(current) + " at ET " +
                                     std::to_string(et));
        }
        Vec3 p, v;
        evaluate(*s, et, p, velocity ? &v : nullptr);
        position += p;
        if (velocity) *velocity += v;
        current = s->center;
    }
}

void SpkFile::state(int target, int center, double et, Vec3& position, Vec3* velocity) const {
    Vec3 pt, pc, vt, vc;
    stateFromBarycentre(target, et, pt, velocity ? &vt : nullptr);
    stateFromBarycentre(center, et, pc, velocity ? &vc : nullptr);
    position = pt - pc;
    if (velocity) *velocity = vt - vc;
}

}  // namespace orrery
