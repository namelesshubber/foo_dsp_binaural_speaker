// Host-side sanity test for the DSP core (no foobar2000 needed).
// Build: g++ -O2 -std=c++17 -I../src test_core.cpp ../src/ctc_core.cpp -o test_core
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

#include "ctc_core.h"

typedef std::complex<double> cd;

static cd response(const std::vector<double>& h, double f, double fs) {
    cd acc(0, 0);
    const double w = 2.0 * 3.14159265358979323846 * f / fs;
    for (size_t n = 0; n < h.size(); ++n) acc += h[n] * std::exp(cd(0, -w * (double)n));
    return acc;
}

// Channel separation at the ears when the binaural signal is (1, 0).
static void report(const char* title, const ctc::Params& p, double fs) {
    ctc::Design d = ctc::designFilters(p, fs);
    std::printf("\n== %s (fs=%.0f, taps=%d) ==\n", title, fs, d.taps);
    std::printf("  freq    earL(dB)  earR/earL = separation(dB)\n");
    const double freqs[] = {100, 200, 400, 800, 1500, 3000, 5000, 7000, 10000};
    for (double f : freqs) {
        cd hi, hc;
        ctc::headModel(p, f, hi, hc);
        cd gs = response(d.mid, f, fs), gd = response(d.side, f, fs);
        cd eL = ((hi + hc) * gs + (hi - hc) * gd) * 0.5;
        cd eR = ((hi + hc) * gs - (hi - hc) * gd) * 0.5;
        std::printf("  %6.0f  %8.2f  %8.2f\n", f, 20 * std::log10(std::abs(eL) + 1e-12),
                    20 * std::log10(std::abs(eR) / (std::abs(eL) + 1e-12) + 1e-12));
    }
}

int main() {
    ctc::Params def;                 // exact sphere model (default)
    report("default, exact sphere", def, 48000.0);
    ctc::Params classic = def;
    classic.exactSphere = false;
    report("default, classic Woodworth+Brown/Duda", classic, 48000.0);

    ctc::Params strong = def;
    strong.regularization = 0.001;
    strong.maxBoostDb = 20;
    strong.autoLevel = false;
    report("strong (beta=0.001, no cap)", strong, 48000.0);
    report("default @44.1k", def, 44100.0);
    report("default @192k", def, 192000.0);

    // Stability + speed test.
    ctc::Engine e;
    e.configure(def, 48000.0);
    const size_t frames = 48000 * 10;
    std::vector<float> buf(frames * 2);
    unsigned s = 1;
    for (auto& v : buf) { s = s * 1664525u + 1013904223u; v = ((s >> 8) & 0xFFFF) / 32768.0f - 1.0f; v *= 0.5f; }
    auto t0 = std::chrono::steady_clock::now();
    e.process(buf.data(), frames);
    auto t1 = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();
    double peak = 0; bool bad = false;
    for (float v : buf) { if (!std::isfinite(v)) bad = true; peak = std::max(peak, (double)std::fabs(v)); }
    std::printf("\nProcessed 10 s of 48k noise in %.3f s (%.1fx realtime), peak=%.3f, nonfinite=%d\n",
                sec, 10.0 / sec, peak, bad ? 1 : 0);
    return bad ? 1 : 0;
}
