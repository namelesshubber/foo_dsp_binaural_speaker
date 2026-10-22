// Host-side sanity test for the DSP core (no foobar2000 needed).
// Build: g++ -O2 -std=c++17 -I../src test_core.cpp ../src/ctc_core.cpp -o test_core
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

#include "ctc_core.h"
#include "settings.h"

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

// Level seen by the ears for a MONO signal (both channels equal), relative to DSP off.
// DSP off: ears get (Hi+Hc). DSP on: ears get (Hi+Hc)*Gmid.
static void lowFreqReport(const char* title, const ctc::Params& p, double fs) {
    ctc::Design d = ctc::designFilters(p, fs);
    std::printf("\n== %s: mono-bass level at ears vs DSP off ==\n", title);
    for (double f : {30.0, 50.0, 80.0, 100.0, 150.0, 200.0, 300.0, 500.0, 1000.0}) {
        cd hi, hc;
        ctc::headModel(p, f, hi, hc);
        cd g = response(d.mid, f, fs);
        std::printf("  %6.0f Hz  %6.2f dB   (Gmid=%6.2f dB, Gside=%6.2f dB)\n", f, 20 * std::log10(std::abs(g)),
                    20 * std::log10(std::abs(g)), 20 * std::log10(std::abs(response(d.side, f, fs))));
    }
}

static void centerReport() {
    std::printf("\n== centre level: Gmid change vs centre=0 (dB), Gside must not change ==\n");
    ctc::Params base, up = base;
    up.centerGainDb = 6.0;
    ctc::Design d0 = ctc::designFilters(base, 48000.0), d1 = ctc::designFilters(up, 48000.0);
    for (double f : {50.0, 100.0, 150.0, 250.0, 500.0, 1000.0, 3000.0, 6000.0, 8000.0, 12000.0, 18000.0}) {
        std::printf("  %6.0f Hz  mid %+6.2f dB   side %+6.2f dB\n", f,
                    20 * std::log10(std::abs(response(d1.mid, f, 48000.0)) / std::abs(response(d0.mid, f, 48000.0))),
                    20 * std::log10(std::abs(response(d1.side, f, 48000.0)) / std::abs(response(d0.side, f, 48000.0))));
    }
}

static int blobTests() {
    int bad = 0;
    ctc::Params p;
    p.cancelMinHz = 220; p.exactSphere = false; p.speakerSpacingCm = 120; p.listenDistanceCm = 150; p.centerGainDb = 4.5; p.headCircumferenceCm = 57.5;
    settings::Blob b; settings::toBlob(p, b);
    ctc::Params q;
    if (!settings::fromBlob(&b, sizeof(b), q) || q.cancelMinHz != 220 || q.exactSphere || q.speakerSpacingCm != 120 || q.listenDistanceCm != 150 || std::fabs(q.centerGainDb - 4.5) > 1e-9 || std::fabs(q.headCircumferenceCm - 57.5) > 1e-9) { std::printf("blob v3 roundtrip FAILED\n"); ++bad; }
    settings::BlobV4 v4 = {settings::kMagic, 4, 550, 1000, 8750, 20000, 1200, 8000, 0, 1, 1, 150};
    ctc::Params r4;
    if (!settings::fromBlob(&v4, sizeof(v4), r4) || r4.centerGainDb != 0 || r4.cancelMinHz != 150 ||
        std::fabs(r4.speakerSpacingCm - 55.0) > 1e-9) { std::printf("blob v4 read FAILED\n"); ++bad; }
    // Version 5 stored the head RADIUS (8750 = 87.5 mm): 2*pi*8.75 cm = 54.98 cm.
    settings::Blob v5; settings::toBlob(p, v5); v5.version = 5; v5.head100 = 8750;
    ctc::Params r5;
    if (!settings::fromBlob(&v5, sizeof(v5), r5) || std::fabs(r5.headCircumferenceCm - 54.978) > 0.01 ||
        std::fabs(ctc::headRadiusMm(r5) - 87.5) > 0.05) { std::printf("blob v5 read FAILED\n"); ++bad; }
    ctc::Params dflt;
    if (std::fabs(ctc::headRadiusMm(dflt) - 87.54) > 0.05) { std::printf("default radius FAILED (%f)\n", ctc::headRadiusMm(dflt)); ++bad; }
    // Old layouts: half-angle 20 deg -> spacing = 2*tan(20 deg) m at 1 m distance = 72.8 cm.
    settings::BlobV3 v3 = {settings::kMagic, 3, 2000, 8750, 20000, 1200, 8000, 0, 1, 1, 150};
    ctc::Params r3;
    if (!settings::fromBlob(&v3, sizeof(v3), r3) || r3.cancelMinHz != 150 || !r3.exactSphere ||
        std::fabs(r3.speakerSpacingCm - 72.8) > 0.2 || r3.listenDistanceCm != 100) { std::printf("blob v3 read FAILED\n"); ++bad; }
    settings::BlobV2 v2 = {settings::kMagic, 2, 2000, 8750, 20000, 1200, 8000, 0, 1, 1};
    ctc::Params r;
    if (!settings::fromBlob(&v2, sizeof(v2), r) || r.cancelMinHz != 0 || !r.exactSphere) { std::printf("blob v2 read FAILED\n"); ++bad; }
    settings::BlobV1 v1 = {settings::kMagic, 1, 2000, 8750, 20000, 1200, 8000, 0, 1};
    ctc::Params s;
    if (!settings::fromBlob(&v1, sizeof(v1), s) || s.cancelMinHz != 0 || s.exactSphere) { std::printf("blob v1 read FAILED\n"); ++bad; }
    char junk[7] = {0};
    if (settings::fromBlob(junk, sizeof(junk), s)) { std::printf("junk blob accepted FAILED\n"); ++bad; }
    std::printf("\nblob tests: %s\n", bad ? "FAILED" : "ok");
    return bad;
}

static void geometryReport() {
    std::printf("\n== geometry helpers ==\n");
    const double sp[] = {55, 120, 30, 200};
    const double ds[] = {100, 100, 60, 100};
    for (int i = 0; i < 4; ++i) {
        ctc::Params p;
        p.speakerSpacingCm = sp[i];
        p.listenDistanceCm = ds[i];
        std::printf("  spacing %3.0f cm, distance %3.0f cm -> half-angle %5.2f deg, head-to-speaker %5.1f cm\n", sp[i], ds[i],
                    ctc::halfAngleDeg(p), ctc::sourceDistanceM(p) * 100.0);
    }
    // Finite distance must tend to the plane-wave model as the distance grows.
    ctc::Params far;
    far.speakerSpacingCm = 55;
    far.listenDistanceCm = 800;
    ctc::Params near = far;
    near.listenDistanceCm = 40;
    std::printf("  contra/ipsi level at 200 Hz: far(8 m) %.2f dB, near(40 cm) %.2f dB\n",
                [&](const ctc::Params& q) { cd hi, hc; ctc::headModel(q, 200.0, hi, hc); return 20 * std::log10(std::abs(hc / hi)); }(far),
                [&](const ctc::Params& q) { cd hi, hc; ctc::headModel(q, 200.0, hi, hc); return 20 * std::log10(std::abs(hc / hi)); }(near));
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

    {
        ctc::Params off = def; off.cancelMinHz = 0;
        lowFreqReport("lower limit OFF", off, 48000.0);
        lowFreqReport("lower limit 150 Hz (default)", def, 48000.0);
    }
    geometryReport();
    centerReport();
    if (blobTests()) return 1;

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
