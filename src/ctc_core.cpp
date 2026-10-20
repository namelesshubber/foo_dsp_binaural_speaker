// ctc_core.cpp - see ctc_core.h for the theory.
#include "ctc_core.h"

#include <algorithm>
#include <cmath>

namespace ctc {

typedef std::complex<double> cd;

static const double kPi = 3.14159265358979323846;
static const double kSpeedOfSound = 343.0;  // m/s

static double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

double halfAngleDeg(const Params& p) {
    const double deg = std::atan((p.speakerSpacingCm * 0.5) / p.listenDistanceCm) * 180.0 / kPi;
    return clampd(deg, 3.0, 60.0);
}

double sourceDistanceM(const Params& p) {
    const double h = p.speakerSpacingCm * 0.5;
    return std::sqrt(h * h + p.listenDistanceCm * p.listenDistanceCm) * 0.01;
}

void sanitize(Params& p) {
    p.speakerSpacingCm    = clampd(p.speakerSpacingCm, 10.0, 500.0);
    p.listenDistanceCm    = clampd(p.listenDistanceCm, 25.0, 800.0);
    p.headRadiusMm        = clampd(p.headRadiusMm, 70.0, 100.0);
    p.regularization      = clampd(p.regularization, 0.001, 1.0);
    p.maxBoostDb          = clampd(p.maxBoostDb, 0.0, 20.0);
    p.cancelMinHz         = clampd(p.cancelMinHz, 0.0, 500.0);
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


// Spherical Hankel functions of the second kind h_n(x) = j_n(x) - i*y_n(x), n = 0..count-1,
// by the upward recurrence (stable for the Hankel function because the y_n part grows with n).
static void hankelSeq(double x, int count, std::vector<cd>& h) {
    h.assign(static_cast<std::size_t>(count) + 2, cd(0.0, 0.0));
    const double s = std::sin(x), co = std::cos(x);
    h[0] = cd(s / x, co / x);                                   // j0 - i*y0, y0 = -cos x / x
    h[1] = cd(s / (x * x) - co / x, co / (x * x) + s / x);      // j1 - i*y1, y1 = -cos x/x^2 - sin x/x
    for (int n = 1; n < count; ++n) h[n + 1] = (2.0 * n + 1.0) / x * h[n] - h[n - 1];
}

static int seriesTerms(double x) {
    int N = static_cast<int>(x + 4.0 * std::cbrt(x) + 10.0);
    return N > 150 ? 150 : N;
}

// Rayleigh series for the pressure on a rigid sphere hit by a plane wave:
//   p(theta)/p0 = 1/(ka)^2 * sum_n i^(n+1) (2n+1) P_n(cos theta) / h_n'(ka),  h_n = spherical Hankel (2nd kind)
cd sphereExact(double f, double thetaDeg, double radius) {
    const double fEval = std::max(f, 20.0);  // avoid x -> 0 at DC
    const double x = 2.0 * kPi * fEval * radius / kSpeedOfSound;
    const double ct = std::cos(thetaDeg * kPi / 180.0);
    const int N = seriesTerms(x);

    std::vector<cd> h;
    hankelSeq(x, N + 1, h);

    cd sum(0.0, 0.0);
    double pPrev = 1.0, pCur = ct;  // P0, P1
    cd ipow(0.0, 1.0);              // i^(n+1) for n = 0
    for (int n = 0; n <= N; ++n) {
        const double pn = (n == 0) ? 1.0 : (n == 1 ? ct : pCur);
        const cd hp = (n == 0) ? -h[1] : h[n - 1] - (n + 1.0) / x * h[n];
        sum += ipow * (2.0 * n + 1.0) * pn / hp;
        if (n >= 1) {  // advance Legendre: P_{n+1}
            const double pNext = ((2.0 * n + 1.0) * ct * pCur - n * pPrev) / (n + 1.0);
            pPrev = pCur;
            pCur = pNext;
        }
        ipow *= cd(0.0, 1.0);
    }
    return sum / (x * x);
}

// Point source at distance r (>= radius):
//   H = (kr/(ka)^2) e^{j k r} * sum_n (2n+1) P_n(cos theta) h_n(kr) / h_n'(ka)
cd spherePoint(double f, double thetaDeg, double radius, double dist) {
    const double fEval = std::max(f, 20.0);
    const double k = 2.0 * kPi * fEval / kSpeedOfSound;
    const double x = k * radius;
    const double xr = k * std::max(dist, radius * 1.05);
    const double ct = std::cos(thetaDeg * kPi / 180.0);
    const int N = seriesTerms(x);

    std::vector<cd> h, hr;
    hankelSeq(x, N + 1, h);
    hankelSeq(xr, N + 1, hr);

    cd sum(0.0, 0.0);
    double pPrev = 1.0, pCur = ct;
    for (int n = 0; n <= N; ++n) {
        const double pn = (n == 0) ? 1.0 : (n == 1 ? ct : pCur);
        const cd hp = (n == 0) ? -h[1] : h[n - 1] - (n + 1.0) / x * h[n];
        sum += (2.0 * n + 1.0) * pn * hr[n] / hp;
        if (n >= 1) {
            const double pNext = ((2.0 * n + 1.0) * ct * pCur - n * pPrev) / (n + 1.0);
            pPrev = pCur;
            pCur = pNext;
        }
    }
    return sum * (xr / (x * x)) * std::exp(cd(0.0, xr));
}

void headModel(const Params& p, double f, cd& hi, cd& hc) {
    const double a = p.headRadiusMm * 1e-3;
    const double phi = halfAngleDeg(p);
    const double omega = 2.0 * kPi * f;
    const double earAxis = 100.0;  // ears sit at +/-100 degrees on the head axis

    if (p.exactSphere) {
        // Right speaker at +phi: ipsilateral ear is earAxis-phi away from the source direction,
        // contralateral ear is earAxis+phi away.
        const double r = sourceDistanceM(p);
        hi = spherePoint(f, earAxis - phi, a, r);
        hc = spherePoint(f, earAxis + phi, a, r);
        return;
    }

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

// 0 below fc/sqrt2, 1 above fc*sqrt2, raised-cosine in log-frequency in between.
// fc <= 0 disables the limit (always 1).
static double fadeWeightLow(double f, double fc) {
    if (fc <= 0.0) return 1.0;
    const double lo = fc / 1.41421356237, hi = fc * 1.41421356237;
    if (f <= lo) return 0.0;
    if (f >= hi) return 1.0;
    const double t = std::log(f / lo) / std::log(hi / lo);
    return 0.5 * (1.0 - std::cos(kPi * t));
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
        // The final filters are minimum-phase and built from the magnitude only, so the band
        // limits are blended on magnitudes (blending complex values with different phases would
        // create notches in the transition regions).
        const double w = fadeWeight(f, p.cancelMaxHz);
        const double am = w * std::abs(a) + (1.0 - w);
        const double bm = w * std::abs(b) + (1.0 - w);
        gs[k] = cd(am, 0.0);
        gd[k] = cd(bm, 0.0);
        if (f >= 200.0 && f <= 8000.0) {
            const double lw = 1.0 / f;  // log-frequency weighting
            accum += lw * 0.5 * (am * am + bm * bm);
            wsum += lw;
        }
    }

    double scale = std::pow(10.0, p.outputGainDb / 20.0);
    if (p.autoLevel && wsum > 0.0 && accum > 1e-12) scale /= std::sqrt(accum / wsum);

    // Lower limit: below it the filters fade back to unity (plain stereo, same bass as with the
    // DSP off). Applied after the level normalisation so it does not change the auto level.
    for (int k = 0; k <= half; ++k) {
        const double f = k * fs / nfft;
        const double wl = fadeWeightLow(f, p.cancelMinHz);
        gs[k] = wl * (gs[k] * scale) + (1.0 - wl) * cd(1.0, 0.0);
        gd[k] = wl * (gd[k] * scale) + (1.0 - wl) * cd(1.0, 0.0);
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
