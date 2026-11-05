// The SPK reader (src/orrery/spk.hpp) on small kernels built here, so every
// value is worked by hand from the Chebyshev polynomials and every error can be
// provoked. No JPL data is needed; test_ephemeris checks the real DE440s.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "check.hpp"
#include "orrery/spk.hpp"

using namespace orrery;

namespace {

constexpr size_t kWordsPerRecord = 128;  // a DAF record is 1024 bytes of 8-byte words
constexpr size_t kSummaryWords = 5;      // ND = 2 doubles + NI = 6 ints, in 2 + 3 words
const std::string kPath = "test_spk.bsp";
const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInfinity = std::numeric_limits<double>::infinity();

// Where the first summary's integers sit: record 2, past NEXT, PREV, NSUM and
// the two doubles. The last two are the segment's first and last word addresses.
constexpr size_t kFirstSummaryInts = (kWordsPerRecord + 3 + 2) * 8;
constexpr size_t kFirstSummaryBegin = kFirstSummaryInts + 16;
constexpr size_t kFirstSummaryEnd = kFirstSummaryInts + 20;

// A summary record's NEXT and NSUM, as word indexes, for record `r` of the file
// (1-based, as NEXT and FWARD count them).
constexpr size_t nextWord(size_t r) { return (r - 1) * kWordsPerRecord; }
constexpr size_t nsumWord(size_t r) { return nextWord(r) + 2; }

// One Chebyshev record's coefficients; the three lists have the same length.
struct Record {
    std::vector<double> x, y, z;
};

// A segment as the kernel holds it: its summary and its data words.
struct Fake {
    int target, center, type;
    double startEt, endEt;
    std::vector<double> words;
};

// A type 2 segment: equal records across [startEt, endEt], then the directory
// (first record's start, interval, record size, record count).
Fake chebyshev(int target, int center, double startEt, double endEt, const std::vector<Record>& records) {
    const double interval = (endEt - startEt) / static_cast<double>(records.size());
    Fake f{target, center, 2, startEt, endEt, {}};
    for (size_t r = 0; r < records.size(); ++r) {
        f.words.push_back(startEt + (static_cast<double>(r) + 0.5) * interval);
        f.words.push_back(interval / 2);
        for (const std::vector<double>* c : {&records[r].x, &records[r].y, &records[r].z}) {
            f.words.insert(f.words.end(), c->begin(), c->end());
        }
    }
    f.words.push_back(startEt);
    f.words.push_back(interval);
    f.words.push_back(static_cast<double>(2 + 3 * records[0].x.size()));
    f.words.push_back(static_cast<double>(records.size()));
    return f;
}

// A segment that stands still at (x, 0, 0): one record of degree zero.
Fake still(int target, int center, double startEt, double endEt, double x) {
    return chebyshev(target, center, startEt, endEt, {Record{{x}, {0.0}, {0.0}}});
}

void putInt(std::vector<char>& bytes, size_t offset, int32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof value);
}

void putWord(std::vector<char>& bytes, size_t word, double value) {
    std::memcpy(bytes.data() + word * 8, &value, sizeof value);
}

// A little-endian DAF/SPK: the file record, summary records holding at most
// `perSummaryRecord` summaries each, then the segments' data.
std::vector<char> image(const std::vector<Fake>& segments, size_t perSummaryRecord = 25) {
    const size_t summaryRecords = (segments.size() + perSummaryRecord - 1) / perSummaryRecord;
    size_t next = (1 + summaryRecords) * kWordsPerRecord;
    size_t total = next;
    for (const Fake& f : segments) total += f.words.size();

    std::vector<char> bytes(total * 8, 0);
    std::memcpy(bytes.data(), "DAF/SPK ", 8);
    putInt(bytes, 8, 2);                            // ND
    putInt(bytes, 12, 6);                           // NI
    putInt(bytes, 76, summaryRecords > 0 ? 2 : 0);  // FWARD: the first summary record
    std::memcpy(bytes.data() + 88, "LTL-IEEE", 8);

    for (size_t i = 0; i < segments.size(); ++i) {
        const Fake& f = segments[i];
        const size_t record = i / perSummaryRecord;  // 0 is the file's record 2
        const size_t base = (record + 1) * kWordsPerRecord;
        if (i % perSummaryRecord == 0) {
            const bool last = record + 1 == summaryRecords;
            putWord(bytes, base, last ? 0.0 : static_cast<double>(record + 3));  // NEXT
            putWord(bytes, base + 2, static_cast<double>(std::min(segments.size() - i, perSummaryRecord)));  // NSUM
        }
        const size_t at = base + 3 + (i % perSummaryRecord) * kSummaryWords;
        putWord(bytes, at, f.startEt);
        putWord(bytes, at + 1, f.endEt);
        const size_t ints = (at + 2) * 8;
        putInt(bytes, ints, f.target);
        putInt(bytes, ints + 4, f.center);
        putInt(bytes, ints + 8, 1);  // the frame: J2000
        putInt(bytes, ints + 12, f.type);
        putInt(bytes, ints + 16, static_cast<int32_t>(next + 1));              // first word, 1-based
        putInt(bytes, ints + 20, static_cast<int32_t>(next + f.words.size()));  // last word, 1-based
        for (double w : f.words) putWord(bytes, next++, w);
    }
    return bytes;
}

SpkFile load(const std::vector<char>& bytes) {
    {
        std::ofstream out(kPath, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    return SpkFile(kPath);
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

// The message of loading `bytes`, for a file the constructor must refuse.
std::string loadError(const std::vector<char>& bytes) {
    return errorOf([&] { load(bytes); });
}

bool same(Vec3 a, Vec3 b, double tolerance = 1e-12) { return length(a - b) < tolerance; }

}  // namespace

int main() {
    Vec3 p, v;

    // T0..T3 at s: 1, s, 2s^2 - 1, 4s^3 - 3s; their derivatives: 0, 1, 4s, 12s^2 - 3.
    SECTION("a record's position and velocity, worked from T0..T3");
    const SpkFile cubic = load(image({chebyshev(3, 0, 0, 200, {Record{{10, 2, 3, 4}, {-1, 0, 0.5, 0}, {0, 0, 0, 2}}})}));
    cubic.state(3, 0, 150, p, &v);  // s = 0.5
    CHECK(same(p, {5.5, -1.25, -2}));
    CHECK(same(v, {0.08, 0.01, 0}));  // divided by the record's radius, 100 s
    cubic.state(3, 0, 200, p, &v);  // s = 1, the end of the record
    CHECK(same(p, {19, -0.5, 2}));
    CHECK(same(v, {0.5, 0.02, 0.18}));
    cubic.state(3, 0, 0, p, &v);  // s = -1, the start
    CHECK(same(p, {7, -0.5, -2}));
    CHECK(same(v, {0.26, -0.02, 0.18}));

    SECTION("state without a velocity, and position, give the same place");
    cubic.state(3, 0, 150, p);
    CHECK(same(p, {5.5, -1.25, -2}));
    CHECK(same(cubic.position(3, 0, 150), {5.5, -1.25, -2}));

    SECTION("the segment table holds what the summary and directory say");
    CHECK(cubic.segments().size() == 1);
    const SpkFile::Segment& seg = cubic.segments()[0];
    CHECK(seg.target == 3 && seg.center == 0 && seg.frame == 1 && seg.type == 2);
    CHECK(seg.startEt == 0 && seg.endEt == 200 && seg.init == 0 && seg.intervalLength == 200);
    CHECK(seg.recordSize == 14 && seg.recordCount == 1);  // 2 + 3 * 4 coefficients

    SECTION("a degree-zero record has no velocity");
    const SpkFile constant = load(image({still(3, 0, 0, 200, 7)}));
    constant.state(3, 0, 33, p, &v);
    CHECK(same(p, {7, 0, 0}) && same(v, {0, 0, 0}));

    SECTION("64 coefficients per component is the limit");
    std::vector<double> t63(64, 0.0), zero64(64, 0.0);
    t63[63] = 1;
    const SpkFile deep = load(image({chebyshev(3, 0, 0, 200, {Record{t63, zero64, zero64}})}));
    deep.state(3, 0, 200, p, &v);  // T63(1) = 1, T63'(1) = 63^2
    CHECK(same(p, {1, 0, 0}, 1e-9));
    CHECK(same(v, {39.69, 0, 0}, 1e-9));
    const std::vector<double> zero65(65, 0.0);
    const SpkFile tooDeep = load(image({chebyshev(3, 0, 0, 200, {Record{zero65, zero65, zero65}})}));
    std::string err = errorOf([&] { tooDeep.position(3, 0, 100); });
    CHECK_MSG(contains(err, "degree too high"), err);

    SECTION("the record is chosen by the instant, and the last one holds the end");
    const SpkFile two = load(image({chebyshev(3, 0, 0, 200, {Record{{1}, {0}, {0}}, Record{{2}, {0}, {0}}})}));
    CHECK(same(two.position(3, 0, 50), {1, 0, 0}));
    CHECK(same(two.position(3, 0, 99.5), {1, 0, 0}));
    CHECK(same(two.position(3, 0, 100), {2, 0, 0}));
    CHECK(same(two.position(3, 0, 200), {2, 0, 0}));

    SECTION("a body with two segments uses the one covering the instant, the first at a shared end");
    const SpkFile spans = load(image({still(3, 0, 0, 100, 1), still(3, 0, 100, 200, 2)}));
    CHECK(same(spans.position(3, 0, 50), {1, 0, 0}));
    CHECK(same(spans.position(3, 0, 150), {2, 0, 0}));
    CHECK(same(spans.position(3, 0, 100), {1, 0, 0}));

    SECTION("states chain through the segment tree in either direction");
    const SpkFile tree = load(image({
        chebyshev(3, 0, 0, 200, {Record{{1000, 50}, {0, 0}, {0, 0}}}),  // the Earth-Moon barycentre, 0.5 km/s
        still(399, 3, 0, 200, -1),                                      // Earth
        chebyshev(301, 3, 0, 200, {Record{{80, 10}, {0, 0}, {0, 0}}}),  // the Moon, 0.1 km/s
    }));
    CHECK(same(tree.position(399, 0, 100), {999, 0, 0}));
    CHECK(same(tree.position(301, 399, 100), {81, 0, 0}));
    CHECK(same(tree.position(399, 301, 100), {-81, 0, 0}));
    CHECK(same(tree.position(301, 0, 150), {1110, 0, 0}));  // 1025 + 85 at s = 0.5
    tree.state(301, 0, 100, p, &v);
    CHECK(same(p, {1080, 0, 0}) && same(v, {0.6, 0, 0}));
    tree.state(301, 399, 100, p, &v);  // Earth's own record stands still on the barycentre
    CHECK(same(p, {81, 0, 0}) && same(v, {0.1, 0, 0}));
    tree.state(3, 301, 100, p, &v);
    CHECK(same(p, {-80, 0, 0}) && same(v, {-0.1, 0, 0}));
    CHECK(same(tree.position(399, 399, 100), {0, 0, 0}));
    CHECK(same(tree.position(0, 0, 100), {0, 0, 0}));

    SECTION("nine links are followed; a tenth, or a loop, is an error");
    std::vector<Fake> chain;
    for (int body = 1; body <= 10; ++body) chain.push_back(still(body, body - 1, 0, 200, 1));
    const SpkFile links = load(image(chain));
    CHECK(same(links.position(9, 0, 100), {9, 0, 0}));
    err = errorOf([&] { links.position(10, 0, 100); });
    CHECK_MSG(contains(err, "chain too deep"), err);
    const SpkFile loop = load(image({still(10, 11, 0, 200, 1), still(11, 10, 0, 200, 1)}));
    err = errorOf([&] { loop.position(10, 0, 100); });
    CHECK_MSG(contains(err, "chain too deep"), err);

    SECTION("a missing body or an instant outside the segments is an error naming the body");
    err = errorOf([&] { cubic.position(5, 0, 100); });
    CHECK_MSG(contains(err, "no SPK segment for body 5 at ET"), err);
    err = errorOf([&] { cubic.position(3, 5, 100); });
    CHECK_MSG(contains(err, "no SPK segment for body 5 at ET"), err);
    err = errorOf([&] { cubic.position(3, 0, -1); });
    CHECK_MSG(contains(err, "no SPK segment for body 3 at ET"), err);
    err = errorOf([&] { cubic.position(3, 0, 200.5); });
    CHECK_MSG(contains(err, "no SPK segment for body 3 at ET"), err);
    err = errorOf([&] { tree.position(301, 0, 201); });
    CHECK_MSG(contains(err, "no SPK segment for body 301 at ET"), err);

    SECTION("coverage is the span every type 2 segment shares; other types are skipped");
    const Fake typeThree{7, 0, 3, 60, 70, {0, 0, 0, 0}};
    const SpkFile mixed = load(image({still(3, 0, 0, 200, 1), typeThree, still(5, 0, 50, 300, 1)}));
    CHECK(mixed.segments().size() == 2);
    CHECK(mixed.coverageStartEt() == 50 && mixed.coverageEndEt() == 200);
    err = errorOf([&] { mixed.position(7, 0, 65); });
    CHECK_MSG(contains(err, "no SPK segment for body 7"), err);

    SECTION("summary records are followed along their linked list");
    const std::vector<Fake> three = {still(3, 0, 0, 200, 1), still(5, 0, 0, 200, 2), still(6, 0, 0, 200, 3)};
    for (size_t perRecord : {size_t(1), size_t(2), size_t(25)}) {
        const SpkFile linked = load(image(three, perRecord));
        CHECK_MSG(linked.segments().size() == 3, std::to_string(perRecord));
        CHECK(same(linked.position(6, 0, 100), {3, 0, 0}));
    }

    SECTION("a file that is not a little-endian SPK is refused, naming the file");
    const std::vector<char> good = image({still(3, 0, 0, 200, 1)});
    err = errorOf([] { SpkFile("test_spk_does_not_exist.bsp"); });
    CHECK_MSG(contains(err, "test_spk_does_not_exist.bsp: cannot open"), err);
    CHECK_MSG(contains(err, "tools/fetch_data.py all"), err);
    err = loadError(std::vector<char>(1023, 0));
    CHECK_MSG(contains(err, kPath) && contains(err, "not a DAF file"), err);
    std::vector<char> bad = good;
    bad.push_back(0);  // not a whole number of words
    err = loadError(bad);
    CHECK_MSG(contains(err, kPath) && contains(err, "not a DAF file"), err);
    bad = good;
    std::memcpy(bad.data(), "DAF/PCK ", 8);
    err = loadError(bad);
    CHECK_MSG(contains(err, "not an SPK file"), err);
    bad = good;
    std::memcpy(bad.data() + 88, "BIG-IEEE", 8);
    err = loadError(bad);
    CHECK_MSG(contains(err, "only little-endian"), err);

    SECTION("a summary format other than SPK's is refused");
    bad = good;
    putInt(bad, 8, 3);  // ND
    err = loadError(bad);
    CHECK_MSG(contains(err, "unexpected summary format"), err);
    bad = good;
    putInt(bad, 12, 5);  // NI
    err = loadError(bad);
    CHECK_MSG(contains(err, "unexpected summary format"), err);

    SECTION("addresses outside the file are refused");
    bad = good;
    putInt(bad, 76, 1000);  // FWARD
    err = loadError(bad);
    CHECK_MSG(contains(err, "summary record out of range"), err);
    bad = good;
    putInt(bad, kFirstSummaryEnd, 1000000);
    err = loadError(bad);
    CHECK_MSG(contains(err, "segment addresses out of range"), err);
    bad = good;
    putInt(bad, kFirstSummaryBegin, 1000000);  // begins after it ends
    err = loadError(bad);
    CHECK_MSG(contains(err, "segment addresses out of range"), err);
    bad = good;
    putInt(bad, kFirstSummaryBegin, 0);  // addresses are 1-based
    err = loadError(bad);
    CHECK_MSG(contains(err, "segment addresses out of range"), err);

    SECTION("a type 2 directory that does not fit its segment is refused, naming the target");
    const size_t recordCountWord = good.size() / 8 - 1, recordSizeWord = recordCountWord - 1;
    bad = good;
    putWord(bad, recordSizeWord, 4);  // under the smallest record, 2 + 3 words
    err = loadError(bad);
    CHECK_MSG(contains(err, "malformed type 2 segment for target 3"), err);
    bad = good;
    putWord(bad, recordSizeWord, 6);  // not 2 + 3n words
    err = loadError(bad);
    CHECK_MSG(contains(err, "malformed type 2 segment for target 3"), err);
    bad = good;
    putWord(bad, recordCountWord, 0);
    err = loadError(bad);
    CHECK_MSG(contains(err, "malformed type 2 segment for target 3"), err);
    bad = good;
    putWord(bad, recordCountWord, 1000);  // more records than the segment holds
    err = loadError(bad);
    CHECK_MSG(contains(err, "malformed type 2 segment for target 3"), err);

    SECTION("a directory with no usable interval, size or count is refused");
    const size_t intervalWord = recordSizeWord - 1, initWord = intervalWord - 1;
    for (double interval : {0.0, -200.0, kNaN, kInfinity}) {
        bad = good;
        putWord(bad, intervalWord, interval);
        err = loadError(bad);
        CHECK_MSG(contains(err, "malformed type 2 segment for target 3"), err);
    }
    bad = good;
    putWord(bad, initWord, kNaN);
    err = loadError(bad);
    CHECK_MSG(contains(err, "malformed type 2 segment for target 3"), err);
    for (size_t word : {recordSizeWord, recordCountWord}) {
        for (double value : {kNaN, 1e300, -1e300}) {
            bad = good;
            putWord(bad, word, value);
            err = loadError(bad);
            CHECK_MSG(contains(err, "malformed type 2 segment for target 3"), err);
        }
    }

    SECTION("an interval too small to index by still picks a record");
    bad = good;
    putWord(bad, intervalWord, 1e-300);
    const SpkFile tiny = load(bad);
    CHECK(same(tiny.position(3, 0, 200), {1, 0, 0}));
    CHECK(same(tiny.position(3, 0, 0), {1, 0, 0}));

    SECTION("a type 2 segment too short for its directory is refused");
    bad = good;
    putInt(bad, kFirstSummaryBegin, 1);  // the first three words of the file
    putInt(bad, kFirstSummaryEnd, 3);
    err = loadError(bad);
    CHECK_MSG(contains(err, "malformed type 2 segment for target 3"), err);
    bad = good;
    putInt(bad, kFirstSummaryBegin, static_cast<int32_t>(good.size() / 8));  // one word, the last
    err = loadError(bad);
    CHECK_MSG(contains(err, "malformed type 2 segment for target 3"), err);
    const Fake shortOther{7, 0, 3, 60, 70, {0}};  // only type 2 has a directory to read
    CHECK(load(image({still(3, 0, 0, 200, 1), shortOther})).segments().size() == 1);

    SECTION("a summary count that does not fit its record is refused");
    const std::vector<Fake> full(25, still(3, 0, 0, 200, 1));
    CHECK(load(image(full)).segments().size() == 25);  // 3 + 25 * 5 words, the whole record
    for (double nsum : {26.0, 1e6, 1e300, -1.0, kNaN}) {
        bad = good;
        putWord(bad, nsumWord(2), nsum);
        err = loadError(bad);
        CHECK_MSG(contains(err, kPath) && contains(err, "too many summaries"), err);
    }

    SECTION("a NEXT outside the file is refused");
    for (double next : {1e6, 1e300, -1.0, kNaN}) {
        bad = good;
        putWord(bad, nextWord(2), next);
        err = loadError(bad);
        CHECK_MSG(contains(err, kPath) && contains(err, "summary record out of range"), err);
    }

    SECTION("a loop in the summary records is refused");
    bad = good;
    putWord(bad, nextWord(2), 2);  // the only record, pointing at itself
    err = loadError(bad);
    CHECK_MSG(contains(err, kPath) && contains(err, "summary records form a loop"), err);
    bad = image(three, 1);  // records 2, 3 and 4, in that order
    putWord(bad, nextWord(4), 3);
    err = loadError(bad);
    CHECK_MSG(contains(err, kPath) && contains(err, "summary records form a loop"), err);

    SECTION("a kernel with no usable segments is refused");
    err = loadError(image({}));
    CHECK_MSG(contains(err, "no type 2 segments"), err);
    err = loadError(image({typeThree}));
    CHECK_MSG(contains(err, "no type 2 segments"), err);

    std::remove(kPath.c_str());
    return check::finish();
}
