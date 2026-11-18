#include "amp/simclock.hpp"
#include <cmath>

namespace amp {

int SimClock::days_in_month(int y, int m) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 30;
    return (m == 2 && leap(y)) ? 29 : d[m - 1];
}

void SimClock::set(int y, int m, int d, double h) {
    m = m < 1 ? 1 : (m > 12 ? 12 : m);
    d = d < 1 ? 1 : (d > days_in_month(y, m) ? days_in_month(y, m) : d);
    year_ = y; month_ = m; day_ = d;
    hours_ = std::fmod(h, 24.0); if (hours_ < 0) hours_ += 24.0;
}

void SimClock::advance_seconds(double s) {
    hours_ += s / 3600.0;
    while (hours_ >= 24.0) {
        hours_ -= 24.0;
        if (++day_ > days_in_month(year_, month_)) {
            day_ = 1;
            if (++month_ > 12) { month_ = 1; ++year_; }
        }
    }
    while (hours_ < 0.0) {
        hours_ += 24.0;
        if (--day_ < 1) {
            if (--month_ < 1) { month_ = 12; --year_; }
            day_ = days_in_month(year_, month_);
        }
    }
}

int SimClock::n_day() const {
    int n = day_;
    for (int m = 1; m < month_; ++m) n += days_in_month(year_, m);
    return n;
}

}  // namespace amp
