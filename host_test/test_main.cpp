// Host-Test: vergleicht den C++-Kern mit Referenzdaten aus dem Original-Python-Code.
//   amp_test <referenzordner> <scenario.txt> <antenne.msi>
#include "amp/engine.hpp"
#include "amp/simclock.hpp"
#include "amp/storm.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace amp;

static int g_fail = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++g_fail; } } while (0)

static std::vector<std::string> split(const std::string &s, char d) {
    std::vector<std::string> v; std::string t; std::stringstream ss(s);
    while (std::getline(ss, t, d)) v.push_back(t);
    if (!s.empty() && s.back() == d) v.push_back("");
    return v;
}
static std::string slurp(const std::string &p) {
    std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

static const bool IS_DOUBLE = sizeof(real) == 8;

static void test_models(const std::string &dir) {
    std::ifstream f(dir + "/models.csv");
    std::string line;
    double mx[3][5] = {};               // je Modell: fs_g, fs_s, fs_s_pot, t_bw/t_rw, saf
    double mx_real[3][5] = {};          // Spalten fuer genauere Zuordnung
    int n[3] = {};
    std::unique_ptr<PropagationModel> models[3];
    for (int i = 0; i < 3; ++i) models[i] = create_model((ModelId)i);
    while (std::getline(f, line)) {
        auto v = split(line, ',');
        if (v.size() != 15) continue;
        int mi = std::atoi(v[0].c_str());
        double d[15];
        for (int i = 1; i < 15; ++i) d[i] = std::strtod(v[i].c_str(), nullptr);
        SimContext c;
        c.rx_lat = real(d[1]); c.rx_lon = real(d[2]); c.n_day = (int)d[3]; c.time_h = real(d[4]);
        c.sun_alt_rx = sun_alt(c.rx_lat, c.rx_lon, c.n_day, c.time_h);
        models[mi]->prepare(c);
        LinkResult r = models[mi]->compute(Tx{real(d[5]), real(d[6]), real(d[7]), real(d[8])});
        double dev[5] = {std::fabs(r.fs_g - d[9]), std::fabs(r.fs_s - d[10]), std::fabs(r.fs_s_pot - d[11]),
                         std::fmax(std::fabs(r.t_bw - d[12]), std::fabs(r.t_rw - d[13])),
                         std::fabs(r.sky_ant_factor - d[14])};
        for (int k = 0; k < 5; ++k) { if (dev[k] > mx[mi][k]) mx[mi][k] = dev[k]; }
        (void)mx_real;
        ++n[mi];
    }
    const double tol_db = IS_DOUBLE ? 1e-6 : 0.1;
    const double tol_t = IS_DOUBLE ? 1e-9 : 1e-3;
    const double tol_f = IS_DOUBLE ? 1e-9 : 1e-4;
    for (int mi = 0; mi < 3; ++mi) {
        std::printf("Modell %d (%s), %d Faelle: max |dev| fs_g %.2e  fs_s %.2e  fs_s_pot %.2e dB, t %.2e ms, saf %.2e\n",
                    mi, model_name((ModelId)mi), n[mi], mx[mi][0], mx[mi][1], mx[mi][2], mx[mi][3], mx[mi][4]);
        CHECK(n[mi] > 0, "Modellfaelle vorhanden");
        CHECK(mx[mi][0] <= tol_db && mx[mi][1] <= tol_db && mx[mi][2] <= tol_db, "Pegel innerhalb Toleranz");
        CHECK(mx[mi][3] <= tol_t, "Laufzeit innerhalb Toleranz");
        CHECK(mx[mi][4] <= tol_f, "sky_ant_factor innerhalb Toleranz");
    }
}

static void test_antenna(const std::string &dir, const std::string &msi) {
    Antenna a;
    CHECK(a.load_text(slurp(msi), "x.msi") && a.active(), "MSI geladen");
    std::ifstream f(dir + "/antenna.csv");
    std::string line; int n = 0, bad = 0; double mx = 0;
    while (std::getline(f, line)) {
        auto v = split(line, ',');
        if (v.size() != 6) continue;
        a.set_heading(real(std::strtod(v[0].c_str(), nullptr)));
        double ref = std::strtod(v[5].c_str(), nullptr);
        double got = a.attenuation(real(std::strtod(v[1].c_str(), nullptr)), real(std::strtod(v[2].c_str(), nullptr)),
                                   real(std::strtod(v[3].c_str(), nullptr)), real(std::strtod(v[4].c_str(), nullptr)));
        double d = std::fabs(got - ref);
        if (d > 1e-5) ++bad;       // float: Winkel an der Rundungsgrenze x.5 koennen kippen
        if (d > mx) mx = d;
        ++n;
    }
    std::printf("Antenne: %d Faelle, %d abweichend (max %.3f dB)\n", n, bad, mx);
    CHECK(n > 0, "Antennenfaelle vorhanden");
    CHECK(bad <= (IS_DOUBLE ? 0 : n / 100), "Antennendaempfung stimmt");
}

static int run_engine(const std::string &dir, const Scenario &sc, const Antenna &ant_proto) {
    std::ifstream f(dir + "/engine.csv");
    std::string line;
    Engine eng;
    Antenna ant = ant_proto;
    int lines = 0, mismatch_lines = 0, msg_total = 0, msg_bad = 0;
    int cur_key = -1;
    while (std::getline(f, line)) {
        auto v = split(line, '|');
        if (v.size() != 7) continue;
        int mi = std::atoi(v[0].c_str());
        EngineParams p;
        p.gain_mode = v[1] == "physical" ? GainMode::Physical : GainMode::Compat;
        p.gain_window = real(std::atof(v[2].c_str()));
        p.sens = real(std::atof(v[3].c_str()));
        bool use_ant = v[4] == "1";
        int step = std::atoi(v[5].c_str());
        int key = mi * 100000 + (int)p.gain_mode * 10000 + (int)p.gain_window * 100 + (int)p.sens * 10 + (use_ant ? 1 : 0);
        if (key != cur_key || step == 0) { eng.mute_all(); eng.set_model((ModelId)mi); cur_key = key; }
        ant = ant_proto;
        if (use_ant) ant.set_heading(real(37.3)); else ant.reset();
        RunOutput out;
        eng.run(sc, real(50.1), real(8.6), 288, real(step * 0.5), ant, p, true, out);

        auto ref = split(v[6], ';');
        if (v[6].empty()) ref.clear();
        bool line_bad = ref.size() != out.msgs.size();
        size_t n_aud = 0;                       // hoerbare Sender zuerst, in Sortierreihenfolge
        for (const auto &r : out.results) n_aud += r.audible ? 1 : 0;
        std::vector<int> ref_drop, cpp_drop;    // Meldungen "verstummt": Python-Set-Reihenfolge ist beliebig
        for (size_t i = 0; i < ref.size(); ++i) {
            auto kv = split(ref[i], ':');
            int fr = std::atoi(kv[0].c_str());
            double g = std::strtod(kv[1].c_str(), nullptr);
            if (i >= n_aud) { ref_drop.push_back(fr); continue; }
            ++msg_total;
            if (i >= out.msgs.size() || fr != out.msgs[i].freq_hz ||
                std::fabs(g - (double)out.msgs[i].gain) > (IS_DOUBLE ? 2e-6 : 2e-3)) {
                ++msg_bad; line_bad = true;
            }
        }
        for (size_t i = n_aud; i < out.msgs.size(); ++i) {
            cpp_drop.push_back(out.msgs[i].freq_hz);
            if (out.msgs[i].gain != 0) line_bad = true;
        }
        std::sort(ref_drop.begin(), ref_drop.end());
        std::sort(cpp_drop.begin(), cpp_drop.end());
        if (ref_drop != cpp_drop) line_bad = true;
        if (line_bad && mismatch_lines < 3) {
            std::printf("  erste Abweichung: Modell %d %s win %s sens %s ant %s step %s\n   ref: %s\n   cpp:", mi,
                        v[1].c_str(), v[2].c_str(), v[3].c_str(), v[4].c_str(), v[5].c_str(), v[6].c_str());
            for (auto &m : out.msgs) std::printf(" %s;", format_gain_msg(m).c_str());
            std::printf("\n");
        }
        if (line_bad) ++mismatch_lines;
        ++lines;
    }
    std::printf("Engine: %d Laeufe, %d Laeufe abweichend, %d Nachrichten, %d abweichend\n",
                lines, mismatch_lines, msg_total, msg_bad);
    CHECK(lines > 0, "Engine-Referenz vorhanden");
    // double: exakt; float: einzelne Sender direkt an der Schwelle duerfen kippen
    CHECK(mismatch_lines <= (IS_DOUBLE ? 0 : lines / 50), "Engine-Nachrichten stimmen");
    return mismatch_lines;
}

static void test_units() {
    CHECK(SimClock::leap(2000) && !SimClock::leap(1900) && SimClock::leap(1976) && !SimClock::leap(1975), "Schaltjahr");
    SimClock c(1975, 12, 31, 23.5);
    c.advance_seconds(3600);
    CHECK(c.year() == 1976 && c.month() == 1 && c.day() == 1 && std::fabs(c.hours() - 0.5) < 1e-9, "Jahreswechsel");
    CHECK(c.n_day() == 1, "n_day 1. Januar");
    SimClock d(1976, 2, 28, 23.9);
    d.advance_seconds(1200);
    CHECK(d.month() == 2 && d.day() == 29, "Schalttag");
    SimClock e(1975, 10, 15, 12);
    CHECK(e.n_day() == 288, "n_day 15.10.1975 = 288");
    SimClock g(1976, 3, 1, 0); g.advance_seconds(-60);
    CHECK(g.month() == 2 && g.day() == 29 && g.hours() > 23.9, "rueckwaerts ueber Mitternacht");

    CHECK(format_gain(1.0f) == "1.0" && format_gain(0.0f) == "0.0" && format_gain(0.5f) == "0.5", "format_gain glatte Werte");
    CHECK(format_gain(0.123457f) == "0.123457", "format_gain 6 Stellen");
    CHECK(format_gain_msg({1017000, 0.25f}) == "1017000:0.25", "format_gain_msg");

    Scenario s; int bad = 0;
    int n = s.load_text("# x\n50.1,8.6,1017,50,Test, mit Komma\nkaputt\n1,2,3\n\n40,-3,584,100,R.N.E.\n", &bad);
    CHECK(n == 2 && bad == 2 && s.sites[0].name == "Test, mit Komma", "Szenario-Parser");

    Storm st; st.params = profile_params(StormProfile::Strong); st.params.speed_kmh = 111; st.params.heading_deg = 0;
    st.start(50, 10);
    st.move(3600);
    CHECK(std::fabs(st.lat - 51.0) < 1e-3 && std::fabs(st.lon - 10.0) < 1e-3, "Sturm bewegt sich 1 Grad pro Stunde bei 111 km/h");
    CHECK(st.active && std::fabs(st.life_min - 60) < 1e-3, "Sturm Lebensdauer nach 1 h");
    st.move(3600);
    CHECK(!st.active, "Sturm loest sich nach 2 h auf");
    Storm t; t.params = profile_params(StormProfile::Strong); t.start(50, 10);
    Rng r1(42), r2(42); int fired = 0, same = 1;
    for (int i = 0; i < 2000; ++i) {
        Sferic a = t.tick(50, 10, false, r1), b = t.tick(50, 10, false, r2);
        fired += a.fire; if (a.fire != b.fire || a.amp != b.amp) same = 0;
        if (a.fire) CHECK(a.amp > 10 && a.dur_ms >= 5 && a.dur_ms <= 45, "Sferic-Werte im Bereich");
    }
    CHECK(same, "Rng deterministisch");
    // 15 * 0.05 = 0.75 Wahrscheinlichkeit je Tick, am Ort der Zelle volle Amplitude
    CHECK(fired > 1300 && fired < 1700, "Sferics-Rate plausibel (ca. 75 %)");
    Sferic far_s; Rng r3(7); int far_fired = 0;
    for (int i = 0; i < 2000; ++i) far_fired += t.tick(60, 10, false, r3).fire;
    CHECK(far_fired < fired, "weiter entfernt weniger Sferics");
}

int main(int argc, char **argv) {
    if (argc < 4) { std::puts("Aufruf: amp_test <referenzordner> <scenario.txt> <antenne.msi>"); return 2; }
    std::printf("Rechengenauigkeit: %s\n", IS_DOUBLE ? "double" : "float");
    test_units();
    test_models(argv[1]);
    test_antenna(argv[1], argv[3]);
    Scenario sc; int bad = 0;
    int n = sc.load_text(slurp(argv[2]), &bad);
    CHECK(n > 100 && bad == 0, "Szenario geladen");
    Antenna ant; ant.load_text(slurp(argv[3]), "ant");
    run_engine(argv[1], sc, ant);
    std::printf(g_fail ? "\n%d Pruefung(en) FEHLGESCHLAGEN\n" : "\nAlle Pruefungen bestanden\n", g_fail);
    return g_fail ? 1 : 0;
}
