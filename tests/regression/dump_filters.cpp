// Dumps the designed filters for many parameter sets, for byte-exact regression comparison.
#include "ctc_core.h"
#include <cstdio>
#include <cstring>
int main(int argc, char** argv) {
    FILE* f = std::fopen(argv[1], "wb");
    const double rates[] = {44100.0, 48000.0, 96000.0, 192000.0};
    int count = 0;
    for (double fs : rates)
    for (int exact = 0; exact < 2; ++exact)
    for (int variant = 0; variant < 8; ++variant) {
        ctc::Params p;
        p.exactSphere = exact != 0;
        switch (variant) {
            case 1: p.speakerSpacingCm = 30; p.listenDistanceCm = 60; break;
            case 2: p.speakerSpacingCm = 200; p.listenDistanceCm = 200; p.regularization = 0.005; break;
            case 3: p.cancelMinHz = 0; p.cancelMaxHz = 5000; p.maxBoostDb = 6; break;
            case 4: p.centerGainDb = 4.5; p.outputGainDb = -3; p.autoLevel = false; break;
            case 5: p.headCircumferenceCm = 58; p.cancelMinHz = 300; break;
            case 6: p.speakerSpacingCm = 400; p.listenDistanceCm = 100; p.maxBoostDb = 18; break;
            case 7: p.regularization = 0.3; p.cancelMaxHz = 12000; p.centerGainDb = -6; break;
            default: break;
        }
        ctc::Design d = ctc::designFilters(p, fs);
        std::fwrite(&d.taps, sizeof(int), 1, f);
        std::fwrite(d.mid.data(), sizeof(double), d.mid.size(), f);
        std::fwrite(d.side.data(), sizeof(double), d.side.size(), f);
        ++count;
    }
    std::fclose(f);
    std::printf("dumped %d designs\n", count);
}
