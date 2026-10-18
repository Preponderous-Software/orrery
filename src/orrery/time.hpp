// Time scales and calendar dates.
//
// The ephemerides run on TDB (Barycentric Dynamical Time). Clocks and the
// on-screen date are UTC. TDB - UTC = (TAI - UTC) + 32.184 s, ignoring TDB's
// periodic terms (under 2 ms). TAI - UTC comes from the IERS leap-second
// table; before 1972 a fixed 10 s is used, which is an approximation
// (real Earth-rotation time differed by up to ~30 s then).
#pragma once

#include <string>

namespace orrery {

constexpr double kJ2000 = 2451545.0;     // JD of 2000-01-01 12:00 TDB
constexpr double kSecondsPerDay = 86400.0;
constexpr double kDaysPerJulianCentury = 36525.0;

struct CalendarDate {
    int year = 2000, month = 1, day = 1, hour = 0, minute = 0;
    double second = 0;
};

// Gregorian calendar (proleptic before 1582) <-> Julian Date.
double julianDate(const CalendarDate& c);
CalendarDate calendarDate(double jd);

// TAI - UTC in seconds at a UTC Julian Date.
double taiMinusUtc(double jdUtc);
double utcToTdb(double jdUtc);
double tdbToUtc(double jdTdb);

// "2026-10-18", "2026-10-18T06:30", "2026-10-18T06:30:15" or with a space;
// a trailing 'Z' is allowed. Returns false on anything else.
bool parseIsoDate(const std::string& text, CalendarDate& out);
std::string formatIsoDate(const CalendarDate& c, bool withSeconds = true);

// The system clock as a UTC Julian Date.
double nowUtc();

}  // namespace orrery
