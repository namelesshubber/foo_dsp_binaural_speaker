// settings.h - (de)serialisation of ctc::Params into a foobar2000 DSP preset blob.
// Values are stored as fixed-point 32-bit integers so the format is stable.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "ctc_core.h"

namespace settings {

struct Blob {
    int32_t magic;      // 'CTC1'
    int32_t version;    // 1
    int32_t angle100;   // speaker half angle, 0.01 deg
    int32_t head100;    // head radius, 0.01 mm
    int32_t reg1e6;     // regularisation * 1e6
    int32_t boost100;   // max boost, 0.01 dB
    int32_t freq;       // canceller upper frequency, Hz
    int32_t gain100;    // output gain, 0.01 dB
    int32_t autoLevel;  // 0/1
};

static const int32_t kMagic = 0x31435443;  // "CTC1" little endian

inline int32_t toFix(double v, double scale) { return static_cast<int32_t>(std::lround(v * scale)); }

inline void toBlob(const ctc::Params& p, Blob& b) {
    b.magic = kMagic;
    b.version = 1;
    b.angle100 = toFix(p.speakerHalfAngleDeg, 100.0);
    b.head100 = toFix(p.headRadiusMm, 100.0);
    b.reg1e6 = toFix(p.regularization, 1e6);
    b.boost100 = toFix(p.maxBoostDb, 100.0);
    b.freq = toFix(p.cancelMaxHz, 1.0);
    b.gain100 = toFix(p.outputGainDb, 100.0);
    b.autoLevel = p.autoLevel ? 1 : 0;
}

// Returns false (and leaves p untouched) if the data is not a valid blob.
inline bool fromBlob(const void* data, std::size_t size, ctc::Params& p) {
    if (!data || size != sizeof(Blob)) return false;
    Blob b;
    std::memcpy(&b, data, sizeof(b));
    if (b.magic != kMagic || b.version != 1) return false;
    p.speakerHalfAngleDeg = b.angle100 / 100.0;
    p.headRadiusMm = b.head100 / 100.0;
    p.regularization = b.reg1e6 / 1e6;
    p.maxBoostDb = b.boost100 / 100.0;
    p.cancelMaxHz = static_cast<double>(b.freq);
    p.outputGainDb = b.gain100 / 100.0;
    p.autoLevel = b.autoLevel != 0;
    ctc::sanitize(p);
    return true;
}

} // namespace settings
