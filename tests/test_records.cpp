// Mission records written by a released build: each fixture under
// tests/fixtures was written by the game itself (tests/fixtures/README.md).
// Every one must load with the records it holds, write back byte for byte, and
// keep working as records: a slower time never replaces one, a faster one does.
// The fixtures are never edited; a new records format adds a v<N> fixture.
#include <fstream>
#include <iterator>
#include <map>
#include <string>

#include "check.hpp"
#include "orrery/missions.hpp"

#ifndef ORRERY_FIXTURES
#define ORRERY_FIXTURES "tests/fixtures"
#endif

using namespace orrery;

namespace {

std::string readAll(const std::string& name) {
    std::ifstream in(std::string(ORRERY_FIXTURES) + "/" + name, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
    static_assert(kRecordsFormatVersion == 1, "a new records format needs its own tests/fixtures/v<N>-*.txt here");

    SECTION("v1-moon-run.txt (0.6.0: the Moon run on the landing autopilot)");
    {
        const std::string text = readAll("v1-moon-run.txt");
        CHECK(!text.empty());
        auto r = parseRecords(text);
        CHECK(r.size() == 1);
        CHECK(r.count("moon-run") == 1 && r["moon-run"] == 25.057);
        CHECK(formatRecords(r) == text);
        CHECK(!recordTime(r, "moon-run", 30.0) && r["moon-run"] == 25.057);
        CHECK(recordTime(r, "moon-run", 24.5) && r["moon-run"] == 24.5);
    }

    SECTION("v1-two-missions.txt (0.6.0: the hot entry flown on top of the Moon run's records)");
    {
        const std::string text = readAll("v1-two-missions.txt");
        CHECK(!text.empty());
        auto r = parseRecords(text);
        CHECK(r.size() == 2);
        CHECK(r.count("moon-run") == 1 && r["moon-run"] == 25.057);
        CHECK(r.count("hot-entry") == 1 && r["hot-entry"] == 32.539);
        CHECK(formatRecords(r) == text);
        CHECK(recordTime(r, "galilean", 600.0) && r.size() == 3);
    }

    return check::finish();
}
