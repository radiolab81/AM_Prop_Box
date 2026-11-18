// Simulationsuhr: Datum + Uhrzeit (UTC), laeuft mit Faktor weiter, rollt ueber Mitternacht,
// Monats- und Jahreswechsel (Schaltjahre). Das Original hielt im Zeitraffer den Tag fest.
#pragma once
#include "amp/real.hpp"

namespace amp {

class SimClock {
public:
    SimClock() = default;
    SimClock(int year, int month, int day, double hours) { set(year, month, day, hours); }

    void set(int year, int month, int day, double hours);
    void advance_seconds(double s);

    int year() const { return year_; }
    int month() const { return month_; }
    int day() const { return day_; }
    double hours() const { return hours_; }      // 0..24
    int n_day() const;                           // Tag im Jahr, 1..366

    static bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
    static int days_in_month(int y, int m);

private:
    int year_ = 1975, month_ = 10, day_ = 15;
    double hours_ = 12.0;
};

}  // namespace amp
