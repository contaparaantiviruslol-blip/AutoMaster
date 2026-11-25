#pragma once

namespace am
{
// Starting points, not laws: tune by ear. Loud presets use a -2 dB ceiling because
// Spotify recommends true peak below -2 dB when a master is louder than -14 LUFS.
struct Preset
{
    const char* name;
    float target;    // LUFS (integrated) the "Match target" button aims for
    float drive;     // input gain into the chain, dB
    float low;       // low shelf @ 100 Hz, dB
    float presence;  // bell @ 3.2 kHz, dB
    float air;       // high shelf @ 9 kHz, dB
    float crisp;     // harmonic exciter, %
    float glue;      // 3-band compression, %
    float width;     // stereo width above ~140 Hz, %
    float clip;      // soft clipper, %
    float ceiling;   // dB
    float release;   // limiter release, ms
};

inline constexpr Preset kPresets[] = {
    //  name                    target drive low  pres air  crisp glue width clip ceil  rel
    { "Auto (analyse first)",   -10.f,  3.f, 0.f, 0.f, 1.5f, 25.f, 35.f, 100.f, 25.f, -2.f, 120.f },
    { "Club / EDM",              -7.5f, 6.f, 1.5f, -0.5f, 2.5f, 45.f, 50.f, 110.f, 45.f, -2.f, 100.f },
    { "Hip-Hop / Trap",          -8.5f, 5.f, 2.f, 0.f, 2.f, 35.f, 40.f, 100.f, 40.f, -2.f, 100.f },
    { "Pop",                     -9.f,  4.5f, 0.5f, 0.5f, 2.5f, 35.f, 35.f, 110.f, 30.f, -2.f, 120.f },
    { "Rock / Metal",            -8.5f, 5.f, 0.5f, 0.5f, 1.5f, 30.f, 45.f, 105.f, 40.f, -2.f, 100.f },
    { "Acoustic / Folk / Jazz", -13.f,  1.5f, 0.f, 0.f, 1.5f, 15.f, 15.f, 100.f, 0.f, -1.f, 200.f },
    { "Ambient / Cinematic",    -14.f,  0.5f, 0.f, -0.5f, 1.f, 10.f, 10.f, 115.f, 0.f, -1.f, 250.f },
    { "Lo-Fi / Chill",          -11.f,  2.f, 1.f, -1.f, 0.f, 10.f, 30.f, 100.f, 15.f, -1.5f, 150.f },
    { "Streaming Safe (-14)",   -14.f,  1.f, 0.f, 0.f, 1.5f, 20.f, 20.f, 100.f, 0.f, -1.f, 180.f },
    { "Loud & Crispy (max)",     -7.f,  8.f, 1.f, 0.f, 3.5f, 60.f, 55.f, 110.f, 60.f, -2.f, 80.f },
};
inline constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));
} // namespace am
