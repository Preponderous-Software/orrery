#include "orrery/time.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>

namespace orrery {

double julianDate(const CalendarDate& c) {
    // Fliegel & Van Flandern style integer algorithm for the day number.
    const int a = (14 - c.month) / 12;
    const int y = c.year + 4800 - a;
    const int m = c.month + 12 * a - 3;
    const long jdn = c.day + (153 * m + 2) / 5 + 365L * y + y / 4 - y / 100 + y / 400 - 32045;
    return static_cast<double>(jdn) - 0.5 + (c.hour + (c.minute + c.second / 60.0) / 60.0) / 24.0;
}

CalendarDate calendarDate(double jd) {
    const double shifted = jd + 0.5;
    long z = static_cast<long>(std::floor(shifted));
    double frac = shifted - static_cast<double>(z);
    // Round to the millisecond so 23:59:59.9999 does not print as :60.
    double secondsOfDay = std::round(frac * kSecondsPerDay * 1000.0) / 1000.0;
    if (secondsOfDay >= kSecondsPerDay) {
        secondsOfDay -= kSecondsPerDay;
        ++z;
    }
    const long a = z + 32044;
    const long b = (4 * a + 3) / 146097;
    const long c = a - 146097 * b / 4;
    const long d = (4 * c + 3) / 1461;
    const long e = c - 1461 * d / 4;
    const long m = (5 * e + 2) / 153;
    CalendarDate out;
    out.day = static_cast<int>(e - (153 * m + 2) / 5 + 1);
    out.month = static_cast<int>(m + 3 - 12 * (m / 10));
    out.year = static_cast<int>(100 * b + d - 4800 + m / 10);
    out.hour = static_cast<int>(secondsOfDay / 3600.0);
    out.minute = static_cast<int>((secondsOfDay - out.hour * 3600.0) / 60.0);
    out.second = secondsOfDay - out.hour * 3600.0 - out.minute * 60.0;
    return out;
}

namespace {
// UTC dates (JD at 0h) from which TAI - UTC took each value (IERS Bulletin C).
struct Leap {
    double jd;
    double seconds;
};
const Leap kLeaps[] = {
    {2441317.5, 10}, {2441499.5, 11}, {2441683.5, 12}, {2442048.5, 13}, {2442413.5, 14}, {2442778.5, 15},
    {2443144.5, 16}, {2443509.5, 17}, {2443874.5, 18}, {2444239.5, 19}, {2444786.5, 20}, {2445151.5, 21},
    {2445516.5, 22}, {2446247.5, 23}, {2447161.5, 24}, {2447892.5, 25}, {2448257.5, 26}, {2448804.5, 27},
    {2449169.5, 28}, {2449534.5, 29}, {2450083.5, 30}, {2450630.5, 31}, {2451179.5, 32}, {2453736.5, 33},
    {2454832.5, 34}, {2456109.5, 35}, {2457204.5, 36}, {2457754.5, 37},
};
}  // namespace

double taiMinusUtc(double jdUtc) {
    double value = 10.0;
    for (const Leap& l : kLeaps) {
        if (jdUtc >= l.jd) value = l.seconds;
    }
    return value;
}

double utcToTdb(double jdUtc) { return jdUtc + (taiMinusUtc(jdUtc) + 32.184) / kSecondsPerDay; }

double tdbToUtc(double jdTdb) {
    double utc = jdTdb - (37.0 + 32.184) / kSecondsPerDay;
    utc = jdTdb - (taiMinusUtc(utc) + 32.184) / kSecondsPerDay;
    return utc;
}

bool parseIsoDate(const std::string& text, CalendarDate& out) {
    CalendarDate c;
    c.hour = c.minute = 0;
    c.second = 0;
    char sep = 0;
    int consumed = 0;
    const char* s = text.c_str();
    if (std::sscanf(s, "%d-%d-%d%n", &c.year, &c.month, &c.day, &consumed) != 3) return false;
    s += consumed;
    if (*s == 'T' || *s == ' ') {
        sep = *s++;
        int n = 0;
        if (std::sscanf(s, "%d:%d%n", &c.hour, &c.minute, &n) != 2) return false;
        s += n;
        if (*s == ':') {
            ++s;
            if (std::sscanf(s, "%lf%n", &c.second, &n) != 1) return false;
            s += n;
        }
    }
    (void)sep;
    if (*s == 'Z') ++s;
    if (*s != '\0') return false;
    if (c.month < 1 || c.month > 12 || c.day < 1 || c.day > 31 || c.hour < 0 || c.hour > 23 || c.minute < 0 ||
        c.minute > 59 || c.second < 0 || c.second >= 61)
        return false;
    // Reject 2026-02-31 and the like by a round trip.
    const CalendarDate back = calendarDate(julianDate({c.year, c.month, c.day, 12, 0, 0}));
    if (back.year != c.year || back.month != c.month || back.day != c.day) return false;
    out = c;
    return true;
}

std::string formatIsoDate(const CalendarDate& c, bool withSeconds) {
    char buf[64];
    if (withSeconds) {
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d", c.year, c.month, c.day, c.hour, c.minute,
                      static_cast<int>(std::floor(c.second)));
    } else {
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d", c.year, c.month, c.day, c.hour, c.minute);
    }
    return buf;
}

double nowUtc() {
    // system_clock counts Unix time, which skips leap seconds like UTC does.
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const double unixSeconds = std::chrono::duration<double>(now).count();
    return 2440587.5 + unixSeconds / kSecondsPerDay;
}

}  // namespace orrery
