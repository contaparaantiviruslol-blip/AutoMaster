#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Presets.h"

namespace ids
{
inline constexpr const char* preset   = "preset";
inline constexpr const char* target   = "target";
inline constexpr const char* drive    = "drive";
inline constexpr const char* low      = "low";
inline constexpr const char* presence = "presence";
inline constexpr const char* air      = "air";
inline constexpr const char* crisp    = "crisp";
inline constexpr const char* glue     = "glue";
inline constexpr const char* width    = "width";
inline constexpr const char* clip     = "clip";
inline constexpr const char* ceiling  = "ceiling";
inline constexpr const char* release  = "release";
}

namespace am
{
inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    StringArray names;
    for (auto& pr : kPresets) names.add (pr.name);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::preset, 1 }, "Preset", names, 0));

    auto add = [&p] (const char* id, const char* name, float lo, float hi, float def, const char* unit)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                                                            NormalisableRange<float> (lo, hi, 0.01f), def,
                                                            AudioParameterFloatAttributes().withLabel (unit)));
    };

    const auto& d = kPresets[0];
    add (ids::target,   "Target loudness", -16.f, -5.f,  d.target,   "LUFS");
    add (ids::drive,    "Drive",           -12.f, 24.f,  d.drive,    "dB");
    add (ids::low,      "Low",             -6.f,  6.f,   d.low,      "dB");
    add (ids::presence, "Presence",        -4.f,  4.f,   d.presence, "dB");
    add (ids::air,      "Air",              0.f,  6.f,   d.air,      "dB");
    add (ids::crisp,    "Crisp",            0.f,  100.f, d.crisp,    "%");
    add (ids::glue,     "Glue",             0.f,  100.f, d.glue,     "%");
    add (ids::width,    "Width",            50.f, 150.f, d.width,    "%");
    add (ids::clip,     "Clipper",          0.f,  100.f, d.clip,     "%");
    add (ids::ceiling,  "Ceiling",         -6.f, -0.1f,  d.ceiling,  "dB");
    add (ids::release,  "Release",          20.f, 500.f, d.release,  "ms");

    return { p.begin(), p.end() };
}
} // namespace am
