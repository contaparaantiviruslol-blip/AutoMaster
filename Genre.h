#pragma once
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>
#include "Analyzer.h"
#include "../Presets.h"

namespace am
{
// A small, transparent rule-based classifier (no machine learning, no training data).
// It scores each genre preset from tempo, rhythmic regularity, spectral tilt, bass
// weight, stereo width and dynamics. It is a *starting-point guess*: always check by ear.
struct GenreResult
{
    int   preset = 0, runnerUp = 0;
    float score = 0, runnerScore = 0, confidence = 0;
};

namespace detail
{
inline float smooth (float x, float a, float b)               // 0 below a, 1 above b
{
    const float t = std::clamp ((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
inline float gauss (float x, float mu, float sd) { const float z = (x - mu) / sd; return std::exp (-0.5f * z * z); }
inline float inRange (float x, float lo, float hi)
{
    if (x >= lo && x <= hi) return 1.f;
    const float d = x < lo ? (lo - x) / lo : (x - hi) / hi;
    return std::max (0.f, 1.f - d / 0.08f);
}
inline float tempoFit (float bpm, float lo, float hi)         // half/double-time counts a bit less
{
    if (bpm <= 0.f) return 0.3f;
    return std::max ({ inRange (bpm, lo, hi), 0.6f * inRange (bpm * 2, lo, hi), 0.6f * inRange (bpm * 0.5f, lo, hi) });
}
inline float wavg (std::initializer_list<std::pair<float, float>> items)
{
    float s = 0, w = 0; for (auto& it : items) { s += it.first * it.second; w += it.first; }
    return s / w;
}
} // namespace detail

inline GenreResult classify (const Features& f)
{
    using namespace detail;
    float s[kNumPresets] {};

    const float pulseHi = smooth (f.pulse, 0.18f, 0.40f);
    const float pulseLo = 1.f - smooth (f.pulse, 0.08f, 0.22f);
    const float wide    = smooth (f.sideDb, -14.f, -6.f);
    const float dyn     = smooth (f.plr, 11.f, 16.f);

    s[1] = wavg ({ { 2.0f, pulseHi }, { 1.5f, std::max (tempoFit (f.bpm, 120, 150), tempoFit (f.bpm, 170, 180)) },
                   { 1.0f, smooth (f.bassBump, 0.f, 6.f) }, { 0.7f, smooth (f.slope, -4.0f, -2.6f) } });                 // Club / EDM
    s[2] = wavg ({ { 1.5f, tempoFit (f.bpm, 65, 105) }, { 1.5f, smooth (f.bassBump, 2.f, 8.f) },
                   { 1.0f, gauss (f.pulse, 0.28f, 0.15f) }, { 0.7f, 1.f - smooth (f.slope, -3.4f, -2.4f) } });           // Hip-Hop / Trap
    s[3] = wavg ({ { 1.2f, tempoFit (f.bpm, 95, 130) }, { 1.0f, gauss (f.pulse, 0.25f, 0.15f) },
                   { 1.0f, gauss (f.bassBump, 2.f, 3.f) }, { 1.0f, gauss (f.slope, -3.1f, 0.8f) } });                    // Pop
    s[4] = wavg ({ { 1.2f, tempoFit (f.bpm, 100, 190) }, { 1.5f, smooth (f.slope, -3.6f, -2.6f) },
                   { 1.0f, 1.f - smooth (f.bassBump, 1.f, 5.f) }, { 0.8f, 1.f - smooth (f.pulse, 0.25f, 0.45f) } });     // Rock / Metal
    s[5] = wavg ({ { 2.0f, pulseLo }, { 1.2f, 1.f - smooth (f.bassBump, 0.f, 4.f) }, { 1.2f, dyn },
                   { 0.6f, gauss (f.slope, -3.3f, 0.9f) } });                                                            // Acoustic / Jazz
    s[6] = wavg ({ { 2.0f, pulseLo }, { 1.5f, wide }, { 1.0f, 1.f - smooth (f.slope, -4.2f, -3.2f) }, { 1.0f, dyn } });  // Ambient
    s[7] = wavg ({ { 1.5f, 1.f - smooth (f.slope, -5.2f, -4.2f) }, { 1.0f, tempoFit (f.bpm, 60, 95) },
                   { 1.0f, gauss (f.pulse, 0.25f, 0.15f) }, { 0.6f, gauss (f.bassBump, 3.f, 3.f) } });                   // Lo-Fi

    GenreResult r;
    for (int i = 1; i <= 7; ++i)
    {
        if (s[i] > r.score) { r.runnerUp = r.preset; r.runnerScore = r.score; r.preset = i; r.score = s[i]; }
        else if (s[i] > r.runnerScore) { r.runnerUp = i; r.runnerScore = s[i]; }
    }
    r.confidence = std::clamp (0.6f * r.score + 2.0f * (r.score - r.runnerScore), 0.f, 1.f);
    return r;
}

// Typical spectral tilt (dB/octave, 125 Hz..16 kHz) and bass bump (dB) per preset, used
// only for gentle auto-tonal corrections (half strength, clamped).
inline constexpr float kRefSlope[kNumPresets] = { -3.2f, -2.6f, -3.6f, -3.0f, -2.5f, -3.2f, -3.8f, -5.0f, -3.2f, -2.6f };
inline constexpr float kRefBump[kNumPresets]  = {  1.0f,  4.0f,  6.0f,  1.5f,  0.5f, -1.0f,  0.0f,  3.0f,  1.0f,  4.0f };
} // namespace am
