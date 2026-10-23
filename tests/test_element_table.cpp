// Element tables (src/orrery/element_table.hpp): loading, interpolation
// between samples and two-body propagation outside them.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>

#include "check.hpp"
#include "orrery/element_table.hpp"
#include "orrery/time.hpp"

using namespace orrery;

namespace {

// Writes `text` to `path` (in the test's working directory) and returns the path.
std::string writeFile(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    return path;
}

// The message of the std::runtime_error `f` throws, or "" if it throws none.
template <class F>
std::string errorOf(F f) {
    try {
        f();
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return "";
}

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

bool near(double a, double b, double tolerance = 1e-9) { return std::fabs(a - b) < tolerance; }

Elements sample(double jd, double a, double e, double i, double node, double peri, double m, double n) {
    Elements s;
    s.epochJd = jd;
    s.a = a;
    s.e = e;
    s.i = i;
    s.node = node;
    s.peri = peri;
    s.m = m;
    s.n = n;
    return s;
}

}  // namespace

int main() {
    SECTION("load reads every column by name and sorts the samples by epoch");
    const std::string good = writeFile("test_element_table_good.csv",
                                       "# columns in a different order from the struct\n"
                                       "n_deg_per_day,m_deg,peri_deg,node_deg,i_deg,e,a_km,jd_tdb\n"
                                       "2.5,20,30,40,5,0.02,1100,2451555.0\n"
                                       "2.0,10,31,41,6,0.01,1000,2451545.0\n");
    const ElementTable loaded = ElementTable::load(good);
    CHECK_MSG(loaded.samples().size() == 2, std::to_string(loaded.samples().size()));
    CHECK(!loaded.empty());
    CHECK(loaded.firstJd() == 2451545.0 && loaded.lastJd() == 2451555.0);
    const Elements& first = loaded.samples().front();
    CHECK(first.a == 1000 && first.e == 0.01 && first.i == 6 && first.node == 41 && first.peri == 31);
    CHECK(first.m == 10 && first.n == 2.0);

    SECTION("a table with a header and no rows, or no samples at all, throws");
    const std::string noRows = writeFile("test_element_table_no_rows.csv",
                                         "jd_tdb,a_km,e,i_deg,node_deg,peri_deg,m_deg,n_deg_per_day\n");
    std::string err = errorOf([&] { ElementTable::load(noRows); });
    CHECK_MSG(contains(err, noRows) && contains(err, "no rows"), err);
    const ElementTable none;
    CHECK(none.empty());
    err = errorOf([&] { none.at(kJ2000); });
    CHECK_MSG(err == "empty element table", err);

    SECTION("at a sample the table returns that sample, not extrapolated");
    const ElementTable two({sample(kJ2000, 1000, 0.01, 6, 41, 31, 10, 2.0),
                            sample(kJ2000 + 10, 1100, 0.03, 8, 43, 33, 40, 2.0)});
    bool ex = true;
    Elements at = two.at(kJ2000, &ex);
    CHECK(!ex && at.epochJd == kJ2000 && at.a == 1000 && at.m == 10);
    at = two.at(kJ2000 + 10, &ex);
    CHECK(!ex && at.epochJd == kJ2000 + 10 && at.a == 1100 && at.m == 40);

    SECTION("a, e and i are linear between samples");
    at = two.at(kJ2000 + 2.5, &ex);
    CHECK(!ex && at.epochJd == kJ2000 + 2.5);
    CHECK(near(at.a, 1025) && near(at.e, 0.015) && near(at.i, 6.5));
    CHECK(near(at.node, 41.5) && near(at.peri, 31.5));

    SECTION("outside the table the nearest sample is propagated two-body, and flagged");
    at = two.at(kJ2000 - 4, &ex);
    CHECK(ex && at.epochJd == kJ2000 - 4);
    CHECK(at.a == 1000 && at.node == 41 && at.n == 2.0 && near(at.m, 10 - 2.0 * 4));
    at = two.at(kJ2000 + 13, &ex);
    CHECK(ex && at.epochJd == kJ2000 + 13);
    CHECK(at.a == 1100 && at.node == 43 && near(at.m, 40 + 2.0 * 3));
    // The position is the two-body position of that sample.
    CHECK(length(two.position(kJ2000 + 13) - keplerPosition(two.samples().back(), kJ2000 + 13)) < 1e-9);

    SECTION("a single sample is propagated on both sides");
    const ElementTable one({sample(kJ2000, 500, 0, 0, 0, 0, 90, 3.0)});
    at = one.at(kJ2000 + 5, &ex);
    CHECK(ex && near(at.m, 90 + 15));
    at = one.at(kJ2000 - 5, &ex);
    CHECK(ex && near(at.m, 90 - 15));
    at = one.at(kJ2000, &ex);
    CHECK(!ex && at.m == 90);

    SECTION("node and argument of periapsis take the shorter arc across 0/360");
    const ElementTable wrap({sample(kJ2000, 1000, 0, 10, 350, 5, 0, 0),
                             sample(kJ2000 + 2, 1000, 0, 10, 10, 355, 0, 0)});
    at = wrap.at(kJ2000 + 1);
    CHECK_MSG(near(std::fmod(at.node, 360.0), 0), std::to_string(at.node));  // 350 -> 10 via 0, not via 180
    CHECK_MSG(near(at.peri, 0), std::to_string(at.peri));                    // 5 -> 355 via 0
    at = wrap.at(kJ2000 + 0.5);
    CHECK_MSG(near(at.node, 355), std::to_string(at.node));

    SECTION("mean longitude is unwrapped by the whole turns the first sample's motion predicts");
    // Two turns and 5 degrees a day by the first sample's n; the second sample
    // says the angle really advanced two turns and 10 degrees.
    const ElementTable fast({sample(kJ2000, 1000, 0, 0, 0, 0, 0, 725.0),
                             sample(kJ2000 + 1, 1000, 0, 0, 0, 0, 10, 725.0)});
    at = fast.at(kJ2000 + 0.5);
    CHECK_MSG(near(at.n, 730), std::to_string(at.n));
    CHECK_MSG(near(at.m, 365), std::to_string(at.m));

    for (const std::string& path : {good, noRows}) std::remove(path.c_str());
    return check::finish();
}
