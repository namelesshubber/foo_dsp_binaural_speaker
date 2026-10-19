// ctc_core.cpp - see ctc_core.h for the theory.
#include "ctc_core.h"

#include <algorithm>
#include <cmath>

namespace ctc {

typedef std::complex<double> cd;

static const double kPi = 3.14159265358979323846;
static const double kSpeedOfSound = 343.0;  // m/s

static double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

void sanitize(Params& p) {
    p.speakerHalfAngleDeg = clampd(p.speakerHalfAngleDeg, 3.0, 60.0);
    p.headRadiusMm        = clampd(p.headRadiusMm, 70.0, 100.0);
    p.regularization      = clampd(p.regularization, 0.001, 1.0);
    p.maxBoostDb          = clampd(p.maxBoostDb, 0.0, 20.0);
    p.cancelMaxHz         = clampd(p.cancelMaxHz, 2000.0, 14000.0);
    p.outputGainDb        = clampd(p.outputGainDb, -12.0, 6.0);
}

// Brown & Duda (1998) head-shadow filter for a given angle of incidence
// (degrees, measured from the axis of the ear being considered).
static cd shadow(double omega, double a_m, double incidenceDeg) {
    const double alphaMin = 0.1;
    const double thetaMin = 150.0;
    const double w0 = kSpeedOfSound / a_m;
    const double alpha = 1.0 + alphaMin / 2.0 +
                         (1.0 - alphaMin / 2.0) * std::cos(incidenceDeg / thetaMin * kPi);
    const cd j(0.0, 1.0);
    return (1.0 + j * alpha * omega / (2.0 * w0)) / (1.0 + j * omega / (2.0 * w0));
}

void headModel(const Params& p, double f, cd& hi, cd& hc) {
    const double a = p.headRadiusMm * 1e-3;
    const double phi = p.speakerHalfAngleDeg;
    const double omega = 2.0 * kPi * f;
    const double earAxis = 100.0;  // ears sit at +/-100 degrees on the head axis

    // Right speaker at +phi. Right ear = ipsilateral, left ear = contralateral.
    double thIpsi = std::fabs(phi - earAxis);
    double thContra = std::fabs(phi + earAxis);
    if (thIpsi > 180.0) thIpsi = 360.0 - thIpsi;
    if (thContra > 180.0) thContra = 360.0 - thContra;

    // Woodworth interaural time difference for the extra contralateral path.
    const double phiRad = phi * kPi / 180.0;
    const double itd = (a / kSpeedOfSound) * (phiRad + std::sin(phiRad));

    hi = shadow(omega, a, thIpsi);
    hc = shadow(omega, a, thContra) * std::exp(cd(0.0, -omega * itd));
}

int chooseTaps(double fs) {
    int t = static_cast<int>(std::ceil(fs * 0.0107 / 32.0)) * 32;  // ~10.7 ms
    return std::max(256, std::min(1024, t));
}

// In-place iterative radix-2 FFT. inverse=true applies the 1/N scaling.
static void fft(std::vector<cd>& a, bool inverse) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * kPi / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
        const cd wl(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            cd w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const cd u = a[i + k];
                const cd v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse) {
        for (std::size_t i = 0; i < n; ++i) a[i] /= static_cast<double>(n);
    }
}

// 1 below fc/sqrt2, 0 above fc*sqrt2, raised-cosine in log-frequency in between.
static double fadeWeight(double f, double fc) {
    const double lo = fc / 1.41421356237, hi = fc * 1.41421356237;
    if (f <= lo) return 1.0;
    if (f >= hi) return 0.0;
    const double t = std::log(f / lo) / std::log(hi / lo);
    return 0.5 * (1.0 + std::cos(kPi * t));
}

static cd limitGain(cd g, double cap) {
    const double m = std::abs(g);
    return m > cap ? g * (cap / m) : g;
}

// Build a minimum-phase FIR from a magnitude response sampled on bins 0..nfft/2
// (real cepstrum method). Minimum phase gives a causal, front-loaded impulse
// response with zero added latency, which truncates cleanly to a short FIR.
static std::vector<double> toMinPhaseImpulse(const std::vector<double>& mag, int nfft, int taps) {
    const int h = nfft / 2;
    std::vector<cd> x(static_cast<std::size_t>(nfft));
    for (int k = 0; k <= h; ++k) {
        const double m = std::max(mag[k], 1e-4);  // -80 dB floor (avoids log(0) at DC)
        x[k] = cd(std::log(m), 0.0);
    }
    for (int k = 1; k < h; ++k) x[nfft - k] = x[k];
    fft(x, true);  // real cepstrum

    std::vector<cd> c(static_cast<std::size_t>(nfft), cd(0.0, 0.0));
    c[0] = x[0];
    for (int n = 1; n < h; ++n) c[n] = 2.0 * x[n];
    c[h] = x[h];
    fft(c, false);                       // log spectrum of the minimum-phase filter
    for (auto& v : c) v = std::exp(v);   // complex minimum-phase spectrum
    fft(c, true);                        // impulse response

    std::vector<double> ir(static_cast<std::size_t>(taps));
    const int fadeStart = static_cast<int>(taps * 0.75);
    for (int n = 0; n < taps; ++n) {
        double w = 1.0;
        if (n >= fadeStart) {
            const double t = static_cast<double>(n - fadeStart) / static_cast<double>(taps - fadeStart);
            w = 0.5 * (1.0 + std::cos(kPi * t));
        }
        ir[n] = c[n].real() * w;
    }
    return ir;
}

Design designFilters(const Params& pin, double fs) {
    Params p = pin;
    sanitize(p);

    const int nfft = fs <= 100000.0 ? 8192 : 16384;
    const int half = nfft / 2;
    const int taps = chooseTaps(fs);
    const double cap = std::pow(10.0, p.maxBoostDb / 20.0);

    std::vector<cd> gs(half + 1), gd(half + 1);
    double accum = 0.0, wsum = 0.0;

    for (int k = 0; k <= half; ++k) {
        const double f = k * fs / nfft;
        cd hi, hc;
        headModel(p, f, hi, hc);
        const cd hs = hi + hc;
        const cd hd = hi - hc;
        cd a = std::conj(hs) / (std::norm(hs) + p.regularization);
        cd b = std::conj(hd) / (std::norm(hd) + p.regularization);
        a = limitGain(a, cap);
        b = limitGain(b, cap);
        const double w = fadeWeight(f, p.cancelMaxHz);
        a = w * a + (1.0 - w) * cd(1.0, 0.0);
        b = w * b + (1.0 - w) * cd(1.0, 0.0);
        gs[k] = a;
        gd[k] = b;
        if (f >= 200.0 && f <= 8000.0) {
            const double lw = 1.0 / f;  // log-frequency weighting
            accum += lw * 0.5 * (std::norm(a) + std::norm(b));
            wsum += lw;
        }
    }

    double scale = std::pow(10.0, p.outputGainDb / 20.0);
    if (p.autoLevel && wsum > 0.0 && accum > 1e-12) scale /= std::sqrt(accum / wsum);
    for (int k = 0; k <= half; ++k) {
        gs[k] *= scale;
        gd[k] *= scale;
    }

    Design d;
    d.taps = taps;
    std::vector<double> ms(half + 1), md(half + 1);
    for (int k = 0; k <= half; ++k) {
        ms[k] = std::abs(gs[k]);
        md[k] = std::abs(gd[k]);
    }
    d.mid = toMinPhaseImpulse(ms, nfft, taps);
    d.side = toMinPhaseImpulse(md, nfft, taps);
    return d;
}

void Engine::configure(const Params& p, double sampleRate) {
    m_design = designFilters(p, sampleRate);
    m_taps = m_design.taps;
    m_bufMid.assign(static_cast<std::size_t>(m_taps) * 2, 0.0);
    m_bufSide.assign(static_cast<std::size_t>(m_taps) * 2, 0.0);
    m_pos = 0;
}

void Engine::reset() {
    std::fill(m_bufMid.begin(), m_bufMid.end(), 0.0);
    std::fill(m_bufSide.begin(), m_bufSide.end(), 0.0);
    m_pos = 0;
}

} // namespace ctc
