#include "orrery/element_table.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "orrery/csv.hpp"

namespace orrery {
namespace {

double wrap180(double deg) {
    double d = std::fmod(deg + 180.0, 360.0);
    if (d < 0) d += 360.0;
    return d - 180.0;
}

}  // namespace

ElementTable::ElementTable(std::vector<Elements> samples) : samples_(std::move(samples)) {
    std::sort(samples_.begin(), samples_.end(),
              [](const Elements& a, const Elements& b) { return a.epochJd < b.epochJd; });
}

ElementTable ElementTable::load(const std::string& path) {
    const CsvTable t = readCsv(path);
    std::vector<Elements> samples;
    for (const auto& row : t.rows) {
        Elements e;
        e.epochJd = t.number(row, "jd_tdb");
        e.a = t.number(row, "a_km");
        e.e = t.number(row, "e");
        e.i = t.number(row, "i_deg");
        e.node = t.number(row, "node_deg");
        e.peri = t.number(row, "peri_deg");
        e.m = t.number(row, "m_deg");
        e.n = t.number(row, "n_deg_per_day");
        samples.push_back(e);
    }
    if (samples.empty()) throw std::runtime_error(path + ": no rows");
    return ElementTable(std::move(samples));
}

Elements ElementTable::at(double jd, bool* extrapolated) const {
    if (samples_.empty()) throw std::runtime_error("empty element table");
    if (extrapolated) *extrapolated = jd < firstJd() || jd > lastJd();
    if (jd <= firstJd() || jd >= lastJd() || samples_.size() == 1) {
        Elements e = jd <= firstJd() ? samples_.front() : samples_.back();
        e.m += e.n * (jd - e.epochJd);
        e.epochJd = jd;
        return e;
    }
    const auto it = std::upper_bound(samples_.begin(), samples_.end(), jd,
                                     [](double t, const Elements& s) { return t < s.epochJd; });
    const Elements& a = *(it - 1);
    const Elements& b = *it;
    const double span = b.epochJd - a.epochJd;
    const double f = (jd - a.epochJd) / span;

    const double la = a.node + a.peri + a.m;
    double lb = b.node + b.peri + b.m;
    // Whole revolutions between the samples, from a's mean motion; the rest
    // is the (small) difference between the predicted and the real angle.
    const double predicted = la + a.n * span;
    lb += 360.0 * std::round((predicted - lb) / 360.0);

    Elements e;
    e.epochJd = jd;
    e.a = a.a + f * (b.a - a.a);
    e.e = a.e + f * (b.e - a.e);
    e.i = a.i + f * (b.i - a.i);
    e.node = a.node + f * wrap180(b.node - a.node);
    e.peri = a.peri + f * wrap180(b.peri - a.peri);
    const double l = la + f * (lb - la);
    e.m = l - e.node - e.peri;
    e.n = (lb - la) / span;
    return e;
}

}  // namespace orrery
