// settings.h - (de)serialisation of ctc::Params into a foobar2000 DSP preset blob.
// Values are stored as fixed-point 32-bit integers so the format is stable.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "ctc_core.h"

namespace settings {

static const int32_t kMagic = 0x31435443;  // "CTC1" little endian

// Current layout (version 7).
struct Blob {
    int32_t magic;      // 'CTC1'
    int32_t version;    // 7
    int32_t spacingMm;  // distance between the speakers, mm
    int32_t distMm;     // listening distance, mm
    int32_t head100;    // head circumference, 0.01 cm (versions <= 5: head radius, 0.01 mm)
    int32_t reg1e6;     // regularisation * 1e6
    int32_t boost100;   // max boost, 0.01 dB
    int32_t freq;       // canceller upper frequency, Hz
    int32_t gain100;    // output gain, 0.01 dB
    int32_t autoLevel;  // 0/1
    int32_t exact;      // 0 = Woodworth + Brown/Duda, 1 = exact rigid sphere
    int32_t minFreq;    // canceller lower frequency, Hz (0 = off)
    int32_t center100;  // centre level, 0.01 dB
    int32_t stereo;     // 0 = binaural input (standard), 1 = ordinary stereo input (added in version 7)
    int32_t virt100;    // virtual speaker half-angle, 0.01 deg (added in version 7)
};

// Older layouts, still readable.
struct BlobV6 {  // versions 5 and 6: no stereo mode (5 stored the head radius, 6 the circumference)
    int32_t magic, version, spacingMm, distMm, head100, reg1e6, boost100, freq, gain100, autoLevel, exact, minFreq, center100;
};
struct BlobV4 {  // version 4: no centre level; head radius
    int32_t magic, version, spacingMm, distMm, head100, reg1e6, boost100, freq, gain100, autoLevel, exact, minFreq;
};
// Versions 1-3 stored the speaker half-angle instead of spacing + distance.
struct BlobV3 {  // version 3
    int32_t magic, version, angle100, head100, reg1e6, boost100, freq, gain100, autoLevel, exact, minFreq;
};
struct BlobV2 {  // version 2: no lower limit
    int32_t magic, version, angle100, head100, reg1e6, boost100, freq, gain100, autoLevel, exact;
};
struct BlobV1 {  // version 1: no model switch, no lower limit
    int32_t magic, version, angle100, head100, reg1e6, boost100, freq, gain100, autoLevel;
};

inline int32_t toFix(double v, double scale) { return static_cast<int32_t>(std::lround(v * scale)); }

inline void toBlob(const ctc::Params& p, Blob& b) {
    b.magic = kMagic;
    b.version = 7;
    b.spacingMm = toFix(p.speakerSpacingCm, 10.0);
    b.distMm = toFix(p.listenDistanceCm, 10.0);
    b.head100 = toFix(p.headCircumferenceCm, 100.0);
    b.reg1e6 = toFix(p.regularization, 1e6);
    b.boost100 = toFix(p.maxBoostDb, 100.0);
    b.freq = toFix(p.cancelMaxHz, 1.0);
    b.gain100 = toFix(p.outputGainDb, 100.0);
    b.autoLevel = p.autoLevel ? 1 : 0;
    b.exact = p.exactSphere ? 1 : 0;
    b.minFreq = toFix(p.cancelMinHz, 1.0);
    b.center100 = toFix(p.centerGainDb, 100.0);
    b.stereo = p.stereoSource ? 1 : 0;
    b.virt100 = toFix(p.virtualHalfAngleDeg, 100.0);
}

// Returns false (and leaves p untouched) if the data is not a valid blob.
// Presets saved by older versions keep the behaviour they were tuned with: every field a version
// did not have gets the value that reproduces its sound (no lower limit, centre 0, binaural input...).
// Versions 1-3: the stored half-angle is converted to a speaker spacing at a listening distance of 1 m.
// Versions <= 5: the stored head radius is converted to a circumference.
inline bool fromBlob(const void* data, std::size_t size, ctc::Params& p) {
    if (!data) return false;
    Blob b;
    std::memset(&b, 0, sizeof(b));
    b.virt100 = 3000;
    bool headIsRadius = true;

    if (size == sizeof(Blob)) {
        std::memcpy(&b, data, sizeof(b));
        if (b.magic != kMagic || b.version != 7) return false;
        headIsRadius = false;
    } else if (size == sizeof(BlobV6)) {
        BlobV6 o;
        std::memcpy(&o, data, sizeof(o));
        if (o.magic != kMagic || (o.version != 5 && o.version != 6)) return false;
        std::memcpy(&b, &o, sizeof(o));
        b.version = 7;
        b.stereo = 0;
        b.virt100 = 3000;
        headIsRadius = o.version == 5;
    } else if (size == sizeof(BlobV4)) {
        BlobV4 o;
        std::memcpy(&o, data, sizeof(o));
        if (o.magic != kMagic || o.version != 4) return false;
        std::memcpy(&b, &o, sizeof(o));
        b.version = 7;
        b.center100 = 0;
        b.stereo = 0;
        b.virt100 = 3000;
    } else if (size == sizeof(BlobV3) || size == sizeof(BlobV2) || size == sizeof(BlobV1)) {
        BlobV3 o;
        std::memset(&o, 0, sizeof(o));
        std::memcpy(&o, data, size);  // missing trailing fields stay zero
        const int expectVersion = size == sizeof(BlobV3) ? 3 : (size == sizeof(BlobV2) ? 2 : 1);
        if (o.magic != kMagic || o.version != expectVersion) return false;
        const double halfAngle = (o.angle100 / 100.0) * 3.14159265358979323846 / 180.0;
        b.magic = kMagic;
        b.version = 7;
        b.distMm = 1000;
        b.spacingMm = static_cast<int32_t>(std::lround(2.0 * 1000.0 * std::tan(halfAngle)));
        b.head100 = o.head100;
        b.reg1e6 = o.reg1e6;
        b.boost100 = o.boost100;
        b.freq = o.freq;
        b.gain100 = o.gain100;
        b.autoLevel = o.autoLevel;
        b.exact = expectVersion >= 2 ? o.exact : 0;
        b.minFreq = expectVersion >= 3 ? o.minFreq : 0;
        b.center100 = 0;
        b.stereo = 0;
        b.virt100 = 3000;
    } else {
        return false;
    }
    p.speakerSpacingCm = b.spacingMm / 10.0;
    p.listenDistanceCm = b.distMm / 10.0;
    if (headIsRadius)
        p.headCircumferenceCm = 2.0 * 3.14159265358979323846 * (b.head100 / 100.0) / 10.0;  // mm -> cm
    else
        p.headCircumferenceCm = b.head100 / 100.0;
    p.regularization = b.reg1e6 / 1e6;
    p.maxBoostDb = b.boost100 / 100.0;
    p.cancelMaxHz = static_cast<double>(b.freq);
    p.cancelMinHz = static_cast<double>(b.minFreq);
    p.centerGainDb = b.center100 / 100.0;
    p.outputGainDb = b.gain100 / 100.0;
    p.autoLevel = b.autoLevel != 0;
    p.exactSphere = b.exact != 0;
    p.stereoSource = b.stereo != 0;
    p.virtualHalfAngleDeg = b.virt100 / 100.0;
    ctc::sanitize(p);
    return true;
}

} // namespace settings
