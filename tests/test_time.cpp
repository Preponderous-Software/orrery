// Calendar dates, Julian Dates and the UTC/TDB offset.
#include <cmath>

#include "check.hpp"
#include "orrery/time.hpp"

using namespace orrery;

int main() {
    SECTION("Julian Dates of known instants");
    CHECK(std::fabs(julianDate({2000, 1, 1, 12, 0, 0}) - 2451545.0) < 1e-9);
    CHECK(std::fabs(julianDate({1950, 1, 1, 0, 0, 0}) - 2433282.5) < 1e-9);
    CHECK(std::fabs(julianDate({2026, 10, 18, 0, 0, 0}) - 2461331.5) < 1e-9);
    CHECK(std::fabs(julianDate({1858, 11, 17, 0, 0, 0}) - 2400000.5) < 1e-9);  // MJD 0
    CHECK(std::fabs(julianDate({1970, 1, 1, 0, 0, 0}) - 2440587.5) < 1e-9);    // Unix epoch

    SECTION("calendar round trips, including leap days and centuries");
    const CalendarDate dates[] = {{2024, 2, 29, 23, 59, 30}, {1900, 3, 1, 0, 0, 0}, {2100, 12, 31, 6, 30, 15},
                                  {1850, 1, 1, 0, 0, 0}};
    for (const CalendarDate& d : dates) {
        const CalendarDate back = calendarDate(julianDate(d));
        CHECK(back.year == d.year && back.month == d.month && back.day == d.day && back.hour == d.hour &&
              back.minute == d.minute && std::fabs(back.second - d.second) < 1e-3);
    }

    SECTION("TDB - UTC = TAI - UTC + 32.184 s");
    const double jd2026 = julianDate({2026, 10, 18, 0, 0, 0});
    CHECK(taiMinusUtc(jd2026) == 37.0);
    CHECK(std::fabs((utcToTdb(jd2026) - jd2026) * 86400.0 - 69.184) < 1e-4);
    CHECK(std::fabs(tdbToUtc(utcToTdb(jd2026)) - jd2026) * 86400.0 < 1e-4);
    CHECK(taiMinusUtc(julianDate({2016, 12, 31, 12, 0, 0})) == 36.0);
    CHECK(taiMinusUtc(julianDate({2017, 1, 1, 0, 0, 1})) == 37.0);
    CHECK(taiMinusUtc(julianDate({1960, 1, 1, 0, 0, 0})) == 10.0);

    SECTION("ISO date parsing");
    CalendarDate c;
    CHECK(parseIsoDate("2026-10-18", c) && c.year == 2026 && c.month == 10 && c.day == 18 && c.hour == 0);
    CHECK(parseIsoDate("1986-02-09T12:30", c) && c.hour == 12 && c.minute == 30);
    CHECK(parseIsoDate("2061-07-28 06:00:15Z", c) && c.second == 15);
    CHECK(!parseIsoDate("2026-02-31", c));
    CHECK(!parseIsoDate("2026-13-01", c));
    CHECK(!parseIsoDate("yesterday", c));
    CHECK(!parseIsoDate("2026-10-18T25:00", c));
    CHECK(formatIsoDate({2026, 10, 18, 6, 5, 4.9}) == "2026-10-18 06:05:04");
    return check::finish();
}
