// ctc_core.h - Crosstalk canceller core (no foobar2000 SDK dependency).
//
// This module implements the "transaural" stage of a speaker virtualizer:
// it takes a binaural stereo signal (e.g. the output of Dolby Headphone) and
// pre-compensates it so that, when played over two loudspeakers, each ear
// receives (approximately) only its own binaural channel.
//
// Theory (symmetric listening position, speakers at +/- phi):
//
//   ears = C * speakers,   C = [ Hi Hc ]
//                              [ Hc Hi ]
//
//   Hi = speaker -> same-side ear transfer function (ipsilateral)
//   Hc = speaker -> opposite ear transfer function (contralateral)
//
// C is diagonalised by the Mid/Side transform, so its (regularised) inverse is
// two independent filters:
//
//   Gmid  = 1 / (Hi + Hc)     applied to M = (L+R)/sqrt(2)
//   Gside = 1 / (Hi - Hc)     applied to S = (L-R)/sqrt(2)
//
// Hi and Hc are taken from a simple analytic head model (Woodworth ITD +
// Brown/Duda head-shadow filter), so no HRTF database is required.
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace ctc {

struct Params {
    double speakerHalfAngleDeg = 15.0;  // half of the angle between the speakers, seen from the listener
    double headRadiusMm        = 87.5;  // average adult head radius
    double regularization      = 0.02;  // Tikhonov beta; larger = safer/weaker cancellation
    double maxBoostDb          = 12.0;  // hard cap on the gain of each canceller filter
    double cancelMinHz         = 150.0; // below this the canceller fades back to plain stereo (0 = off)
    double cancelMaxHz         = 8000.0;// canceller fades out around this frequency (one octave wide)
    double outputGainDb        = 0.0;   // extra trim
    bool   autoLevel           = true;  // normalise average in-band power gain to 0 dB
    bool   exactSphere         = true;  // true: rigid-sphere diffraction (Rayleigh series), false: Woodworth + Brown/Duda
};

// Clamp all parameters to sane ranges.
void sanitize(Params& p);

// Pressure transfer function on a rigid sphere (radius a) for a plane wave, at a point whose
// angle from the source direction is thetaDeg (Rayleigh series, Aaronson & Hartmann 2014, Eq. 2).
// Phase convention: a delay tau appears as exp(-j*w*tau).
std::complex<double> sphereExact(double freqHz, double thetaDeg, double radiusM);

// Speaker->ear transfer functions of the head model at one frequency.
// hi: same-side ear, hc: opposite ear (extra ITD delay and head shadow included).
void headModel(const Params& p, double freqHz, std::complex<double>& hi, std::complex<double>& hc);

struct Design {
    int taps = 0;
    std::vector<double> mid;   // FIR for the Mid channel
    std::vector<double> side;  // FIR for the Side channel
};

// Number of FIR taps used for a given sample rate (multiple of 32).
int chooseTaps(double sampleRate);

// Compute the two FIR filters for a sample rate.
Design designFilters(const Params& p, double sampleRate);

// Real-time engine: Mid/Side split, two FIR convolutions, Mid/Side merge.
class Engine {
public:
    void configure(const Params& p, double sampleRate);
    void reset();
    bool configured() const { return m_taps > 0; }
    const Design& design() const { return m_design; }

    // Process interleaved stereo samples in place. T = float or double.
    template <class T>
    void process(T* interleavedStereo, std::size_t frames);

private:
    static double dot(const double* h, const double* x, int n);

    Design m_design;
    int m_taps = 0;
    int m_pos = 0;
    std::vector<double> m_bufMid;   // 2*taps, duplicated ring buffer
    std::vector<double> m_bufSide;
};

inline double Engine::dot(const double* h, const double* x, int n) {
    double a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    for (int i = 0; i < n; i += 4) {
        a0 += h[i]     * x[i];
        a1 += h[i + 1] * x[i + 1];
        a2 += h[i + 2] * x[i + 2];
        a3 += h[i + 3] * x[i + 3];
    }
    return (a0 + a1) + (a2 + a3);
}

template <class T>
void Engine::process(T* x, std::size_t frames) {
    if (m_taps <= 0) return;
    const double k = 0.70710678118654752440;
    const int n = m_taps;
    const double* hm = m_design.mid.data();
    const double* hs = m_design.side.data();
    for (std::size_t i = 0; i < frames; ++i) {
        const double l = static_cast<double>(x[2 * i]);
        const double r = static_cast<double>(x[2 * i + 1]);
        const double m = (l + r) * k;
        const double s = (l - r) * k;
        m_pos = (m_pos == 0) ? n - 1 : m_pos - 1;
        m_bufMid[m_pos] = m_bufMid[m_pos + n] = m;
        m_bufSide[m_pos] = m_bufSide[m_pos + n] = s;
        const double ym = dot(hm, &m_bufMid[m_pos], n);
        const double ys = dot(hs, &m_bufSide[m_pos], n);
        x[2 * i]     = static_cast<T>((ym + ys) * k);
        x[2 * i + 1] = static_cast<T>((ym - ys) * k);
    }
}

} // namespace ctc
