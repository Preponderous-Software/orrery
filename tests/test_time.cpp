// Calendar dates, Julian Dates and the UTC/TDB offset.
#include <cmath>
#include <string>

#include "check.hpp"
#include "orrery/time.hpp"

using namespace orrery;

namespace {
// Every step of TAI - UTC since it started at 10 s on 1972-01-01 (IERS
// Bulletin C), each at 00:00 UTC on the first of the month.
struct LeapStep {
    int year, month;
    double seconds;  // TAI - UTC from the step on; one less before it
};
const LeapStep kLeapSteps[] = {
    {1972, 7, 11}, {1973, 1, 12}, {1974, 1, 13}, {1975, 1, 14}, {1976, 1, 15}, {1977, 1, 16}, {1978, 1, 17},
    {1979, 1, 18}, {1980, 1, 19}, {1981, 7, 20}, {1982, 7, 21}, {1983, 7, 22}, {1985, 7, 23}, {1988, 1, 24},
    {1990, 1, 25}, {1991, 1, 26}, {1992, 7, 27}, {1993, 7, 28}, {1994, 7, 29}, {1996, 1, 30}, {1997, 7, 31},
    {1999, 1, 32}, {2006, 1, 33}, {2009, 1, 34}, {2012, 7, 35}, {2015, 7, 36}, {2017, 1, 37},
};
}  // namespace

int main() {
    SECTION("Julian Dates of known instants");
    CHECK(std::fabs(julianDate({2000, 1, 1, 12, 0, 0}) - 2451545.0) < 1e-9);
    CHECK(std::fabs(julianDate({1950, 1, 1, 0, 0, 0}) - 2433282.5) < 1e-9);
    CHECK(std::fabs(julianDate({2026, 10, 18, 0, 0, 0}) - 2461331.5) < 1e-9);
    CHECK(std::fabs(julianDate({1858, 11, 17, 0, 0, 0}) - 2400000.5) < 1e-9);  // MJD 0
    CHECK(std::fabs(julianDate({1970, 1, 1, 0, 0, 0}) - 2440587.5) < 1e-9);    // Unix epoch
    CHECK(std::fabs(julianDate({1582, 10, 15, 0, 0, 0}) - 2299160.5) < 1e-9);  // the first Gregorian day
    CHECK(std::fabs(julianDate({1582, 10, 14, 0, 0, 0}) - 2299159.5) < 1e-9);  // proleptic before it
    CHECK(std::fabs(julianDate({2000, 1, 1, 18, 0, 0}) - 2451545.25) < 1e-9);  // the hours are a fraction of a day

    SECTION("calendar round trips, including leap days and centuries");
    const CalendarDate dates[] = {{2024, 2, 29, 23, 59, 30}, {1900, 3, 1, 0, 0, 0}, {2100, 12, 31, 6, 30, 15},
                                  {1850, 1, 1, 0, 0, 0}};
    for (const CalendarDate& d : dates) {
        const CalendarDate back = calendarDate(julianDate(d));
        CHECK(back.year == d.year && back.month == d.month && back.day == d.day && back.hour == d.hour &&
              back.minute == d.minute && std::fabs(back.second - d.second) < 1e-3);
    }

    SECTION("calendar dates round to the millisecond, rolling over into the next day");
    const CalendarDate newYear = calendarDate(julianDate({2026, 12, 31, 23, 59, 59.9999}));
    CHECK(newYear.year == 2027 && newYear.month == 1 && newYear.day == 1 && newYear.hour == 0 && newYear.minute == 0 &&
          newYear.second == 0);

    SECTION("TDB - UTC = TAI - UTC + 32.184 s");
    const double jd2026 = julianDate({2026, 10, 18, 0, 0, 0});
    CHECK(taiMinusUtc(jd2026) == 37.0);
    CHECK(std::fabs((utcToTdb(jd2026) - jd2026) * 86400.0 - 69.184) < 1e-4);
    CHECK(std::fabs(tdbToUtc(utcToTdb(jd2026)) - jd2026) * 86400.0 < 1e-4);
    CHECK(taiMinusUtc(julianDate({2016, 12, 31, 12, 0, 0})) == 36.0);
    CHECK(taiMinusUtc(julianDate({2017, 1, 1, 0, 0, 1})) == 37.0);
    CHECK(taiMinusUtc(julianDate({1960, 1, 1, 0, 0, 0})) == 10.0);

    SECTION("every leap-second step falls on its date, with its value");
    CHECK(taiMinusUtc(julianDate({1972, 1, 1, 0, 0, 0})) == 10.0);
    for (const LeapStep& l : kLeapSteps) {
        const double stepJd = julianDate({l.year, l.month, 1, 0, 0, 0});
        const std::string when = formatIsoDate({l.year, l.month, 1, 0, 0, 0});
        CHECK_MSG(taiMinusUtc(stepJd) == l.seconds, when);
        CHECK_MSG(taiMinusUtc(stepJd - 1.0 / kSecondsPerDay) == l.seconds - 1.0, when);
    }

    SECTION("UTC to TDB and back holds on both sides of every step");
    for (const LeapStep& l : kLeapSteps) {
        const double stepJd = julianDate({l.year, l.month, 1, 0, 0, 0});
        const std::string when = formatIsoDate({l.year, l.month, 1, 0, 0, 0});
        for (double offset : {-1.0, 0.0, 1.0, 5.0, 20.0}) {
            const double utc = stepJd + offset / kSecondsPerDay;
            CHECK_MSG(std::fabs(tdbToUtc(utcToTdb(utc)) - utc) * kSecondsPerDay < 1e-3,
                      when + " + " + std::to_string(offset) + " s");
        }
    }
    const double utc1960 = julianDate({1960, 1, 1, 0, 0, 0});
    CHECK(std::fabs(tdbToUtc(utcToTdb(utc1960)) - utc1960) * kSecondsPerDay < 1e-3);

    SECTION("ISO date parsing");
    CalendarDate c;
    CHECK(parseIsoDate("2026-10-18", c) && c.year == 2026 && c.month == 10 && c.day == 18 && c.hour == 0);
    CHECK(parseIsoDate("1986-02-09T12:30", c) && c.hour == 12 && c.minute == 30);
    CHECK(parseIsoDate("2061-07-28 06:00:15Z", c) && c.second == 15);
    CHECK(!parseIsoDate("2026-02-31", c));
    CHECK(!parseIsoDate("2026-13-01", c));
    CHECK(!parseIsoDate("yesterday", c));
    CHECK(!parseIsoDate("2026-10-18T25:00", c));
    CHECK(parseIsoDate("2026-10-18T06:30:15.5", c) && c.second == 15.5);
    CHECK(parseIsoDate("2026-10-18 06:30", c) && c.hour == 6 && c.minute == 30 && c.second == 0);
    CHECK(parseIsoDate("2026-10-18Z", c) && c.hour == 0 && c.minute == 0);
    CHECK(parseIsoDate("2016-12-31T23:59:60", c) && c.second == 60);  // a leap second is a real time
    CHECK(parseIsoDate("2000-02-29", c) && !parseIsoDate("1900-02-29", c) && !parseIsoDate("2023-02-29", c));
    for (const char* bad : {"", "2026-10", "2026-00-10", "2026-10-00", "2026-10-18T", "2026-10-18T06", "2026-10-18T06:60",
                            "2026-10-18T06:30:", "2026-10-18T06:30:61", "2026-10-18t06:30", "2026-10-18Zjunk",
                            "2026-10-18 later"}) {
        CHECK_MSG(!parseIsoDate(bad, c), bad);
    }
    CalendarDate kept{1999, 12, 31, 23, 59, 58};
    CHECK(!parseIsoDate("2026-02-31", kept) && kept.year == 1999 && kept.second == 58);  // a failure changes nothing

    SECTION("ISO date parsing: seconds that are not a finite number");
    for (const char* bad : {"2026-10-18T06:30:nan", "2026-10-18T06:30:-nan", "2026-10-18T06:30:NaN",
                            "2026-10-18T06:30:inf"}) {
        CHECK_MSG(!parseIsoDate(bad, c), bad);
    }

    SECTION("ISO date formatting");
    CHECK(formatIsoDate({2026, 10, 18, 6, 5, 4.9}) == "2026-10-18 06:05:04");
    CHECK(formatIsoDate({2026, 10, 18, 6, 5, 4.9}, false) == "2026-10-18 06:05");
    CHECK(formatIsoDate({1850, 1, 2, 0, 0, 59.999}) == "1850-01-02 00:00:59");  // seconds floor, never round to :60

    SECTION("the system clock is a UTC Julian Date");
    CHECK(nowUtc() > julianDate({2026, 1, 1, 0, 0, 0}));
    return check::finish();
}
