// Headless test: synthesises three very different "tracks", runs the plugin's analysis
// on each, applies the suggested settings, renders the result and writes raw float32
// files that Tests/check.py then measures independently (LUFS, true peak, spectrum).
#include <fstream>
#include <iostream>
#include <random>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/PluginProcessor.h"

namespace
{
constexpr double kFs = 44100.0;
constexpr int kBlock = 512;

struct Track { std::vector<float> l, r; };

Track synth (int style, double seconds)
{
    const size_t N = (size_t) (seconds * kFs);
    Track t; t.l.assign (N, 0.f); t.r.assign (N, 0.f);
    std::mt19937 rng (1234); std::uniform_real_distribution<float> U (-1.f, 1.f);
    const double bpm = style == 0 ? 128.0 : style == 2 ? 85.0 : 72.0, beat = 60.0 / bpm;
    const double tp = 2.0 * 3.14159265358979323846;
    float prevN = 0.f, lp = 0.f;

    // irregular plucks for the "acoustic" style
    std::vector<double> pluckT; { double x = 0.3; while (x < seconds) { pluckT.push_back (x); x += 0.35 + 0.9 * (U (rng) * 0.5f + 0.5f); } }

    for (size_t n = 0; n < N; ++n)
    {
        const double ts = (double) n / kFs, tb = std::fmod (ts, beat);
        double L = 0, R = 0;
        const float noise = U (rng);

        if (style == 0)   // club: four-on-the-floor kick, off-beat hats, ducked bass, pad
        {
            const double kick = std::sin (tp * (45.0 * tb + 4.0 * (1.0 - std::exp (-30.0 * tb)))) * std::exp (-9.0 * tb) * 0.8
                              + noise * std::exp (-tb * 400.0) * 0.5;   // beater click
            const double th = std::fmod (ts + beat / 2, beat);
            const double hat = (noise - prevN) * std::exp (-th * 70.0) * 0.12;
            const double bass = (std::sin (tp * 55.0 * ts) + 0.4 * std::sin (tp * 110.0 * ts)) * 0.2 * (1.0 - std::exp (-tb * 25.0));
            lp += 0.35f * ((float) (std::sin (tp * 220.0 * ts) + std::sin (tp * 261.6 * ts) + std::sin (tp * 329.6 * ts)) * 0.05f - lp);
            L = kick + hat + bass + lp * 1.0 + 0.03 * std::sin (tp * 880.0 * ts * 1.003);
            R = kick + hat * 0.8 + bass + lp * 1.0 + 0.03 * std::sin (tp * 880.0 * ts * 0.997);
        }
        else if (style == 2)   // hip-hop: 808 + snare + 16th hats
        {
            const double t2 = std::fmod (ts, beat * 2.0), ts2 = std::fmod (ts + beat, beat * 2.0);
            const double k808 = std::sin (tp * 42.0 * t2) * std::exp (-2.5 * t2) * 0.75;
            const double snare = noise * std::exp (-18.0 * ts2) * 0.35 * (ts2 < beat ? 1.0 : 0.0);
            const double th = std::fmod (ts, beat / 4);
            const double hat = (noise - prevN) * std::exp (-th * 120.0) * 0.1;
            const double keys = 0.05 * std::sin (tp * 196.0 * ts) + 0.04 * std::sin (tp * 233.1 * ts);
            L = k808 + snare + hat + keys; R = k808 + snare * 0.9 + hat * 1.1 + keys;
        }
        else   // acoustic: irregular plucked harmonics, no steady pulse
        {
            for (size_t p = 0; p < pluckT.size(); ++p)
            {
                const double d = ts - pluckT[p]; if (d < 0 || d > 3.0) continue;
                const double f0 = 110.0 * std::pow (2.0, (double) ((p * 5) % 12) / 12.0 + (p % 3));
                double v = 0; for (int h = 1; h <= 6; ++h) v += std::sin (tp * f0 * h * ts) / h * std::exp (-d * (2.0 + h * 0.8));
                const double pan = 0.3 + 0.4 * (double) (p % 3) / 2.0;
                L += 0.25 * v * (1.0 - pan); R += 0.25 * v * pan;
            }
        }
        prevN = noise;
        t.l[n] = (float) L; t.r[n] = (float) R;
    }
    float pk = 0; for (size_t n = 0; n < N; ++n) pk = std::max ({ pk, std::abs (t.l[n]), std::abs (t.r[n]) });
    const float g = 0.7f / pk;                            // unmastered mix: peaks around -3 dBFS
    for (size_t n = 0; n < N; ++n) { t.l[n] *= g; t.r[n] *= g; }
    return t;
}

void run (AutoMasterProcessor& p, const Track& in, Track* out, size_t from = 0, size_t to = 0)
{
    if (to == 0) to = in.l.size();
    if (out) { out->l.assign (to - from, 0.f); out->r.assign (to - from, 0.f); }
    juce::AudioBuffer<float> buf (2, kBlock); juce::MidiBuffer mb;
    for (size_t pos = from; pos < to; pos += (size_t) kBlock)
    {
        const int len = (int) std::min ((size_t) kBlock, to - pos);
        buf.setSize (2, len, false, false, true);
        std::copy (in.l.begin() + (long) pos, in.l.begin() + (long) pos + len, buf.getWritePointer (0));
        std::copy (in.r.begin() + (long) pos, in.r.begin() + (long) pos + len, buf.getWritePointer (1));
        p.processBlock (buf, mb);
        if (out) for (int i = 0; i < len; ++i) { out->l[pos - from + (size_t) i] = buf.getSample (0, i); out->r[pos - from + (size_t) i] = buf.getSample (1, i); }
    }
}

void save (const Track& t, const std::string& path)
{
    std::ofstream f (path, std::ios::binary);
    for (size_t i = 0; i < t.l.size(); ++i) { f.write ((const char*) &t.l[i], 4); f.write ((const char*) &t.r[i], 4); }
}
}

void setP (AutoMasterProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

// Pink noise through neutral / Air / Crisp settings: checks transparency and what the tone controls do.
void toneTests()
{
    std::mt19937 rng (7); std::uniform_real_distribution<float> U (-1.f, 1.f);
    Track in; const size_t N = (size_t) (20 * kFs); in.l.resize (N); in.r.resize (N);
    float b0 = 0, b1 = 0, b2 = 0;
    for (size_t i = 0; i < N; ++i)
    {
        const float w = U (rng);
        b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
        const float v = (b0 + b1 + b2 + w * 0.1848f) * 0.04f;
        in.l[i] = v; in.r[i] = v * 0.9f + 0.1f * U (rng) * 0.1f;
    }
    save (in, "/home/claude/test_out/pink_in.f32");

    struct Case { const char* tag; float air, crisp, low, pres, width; };
    const Case cases[] = { { "neutral", 0, 0, 0, 0, 100 }, { "air6", 6, 0, 0, 0, 100 }, { "crisp100", 0, 100, 0, 0, 100 },
                           { "low6", 0, 0, 6, 0, 100 }, { "pres4", 0, 0, 0, 4, 100 } };
    for (auto& c : cases)
    {
        AutoMasterProcessor p;
        p.setPlayConfigDetails (2, 2, kFs, kBlock);
        p.prepareToPlay (kFs, kBlock);
        setP (p, ids::drive, 0.f); setP (p, ids::low, c.low); setP (p, ids::presence, c.pres); setP (p, ids::air, c.air);
        setP (p, ids::crisp, c.crisp); setP (p, ids::glue, 0.f); setP (p, ids::width, c.width); setP (p, ids::clip, 0.f);
        setP (p, ids::ceiling, -0.1f);
        p.prepareToPlay (kFs, kBlock);
        Track o; run (p, in, &o);
        save (o, std::string ("/home/claude/test_out/pink_") + c.tag + ".f32");
        std::cout << "tone case " << c.tag << ": reported latency " << p.getLatencySamples() << " samples\n";
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    toneTests();
    const char* names[] = { "club", "hiphop", "acoustic" };
    const int styles[] = { 0, 2, 1 };
    system ("mkdir -p /home/claude/test_out");

    for (int s = 0; s < 3; ++s)
    {
        std::cout << "\n===== " << names[s] << " =====\n";
        const Track in = synth (styles[s], 60.0);
        AutoMasterProcessor p;
        p.setPlayConfigDetails (2, 2, kFs, kBlock);
        p.prepareToPlay (kFs, kBlock);

        // 1) analyse the first 40 s
        p.startAnalysis();
        run (p, in, nullptr, 0, (size_t) (40 * kFs));
        p.stopAnalysis();
        run (p, in, nullptr, 0, (size_t) kBlock);            // lets the audio thread acknowledge the stop
        am::Features f;
        if (! p.takeAnalysisResult (f)) { std::cout << "NO ANALYSIS RESULT\n"; continue; }
        std::cout << p.applyAnalysis (f) << "\n";
        { std::ofstream fx (std::string ("/home/claude/test_out/") + names[s] + "_flux.f32", std::ios::binary);
          for (float v : p.getAnalyzer().fluxHistory()) fx.write ((const char*) &v, 4); }
        std::cout << "  features: bpm " << f.bpm << " pulse " << f.pulse << " slope " << f.slope << " bump " << f.bassBump
                  << " side " << f.sideDb << " plr " << f.plr << " lufs " << f.lufs << "\n";
        const auto g = am::classify (f);
        std::cout << "  scores: best " << g.score << " second " << g.runnerScore << " conf " << g.confidence << "\n";

        // 2) render with the suggested settings, then 3) match target and render again
        p.prepareToPlay (kFs, kBlock);
        Track o1; run (p, in, &o1);
        std::cout << "  plugin meter, pass 1: integrated " << p.mIntegrated.load() << " LUFS, peak " << p.mPeakDb.load() << " dB\n";
        std::cout << "  " << p.matchTarget() << "\n";
        p.prepareToPlay (kFs, kBlock);
        Track o2; run (p, in, &o2);
        std::cout << "  plugin meter, pass 2: integrated " << p.mIntegrated.load() << " LUFS\n";

        save (in, std::string ("/home/claude/test_out/") + names[s] + "_in.f32");
        save (o1, std::string ("/home/claude/test_out/") + names[s] + "_out1.f32");
        save (o2, std::string ("/home/claude/test_out/") + names[s] + "_out2.f32");

        if (s == 0)   // maximum-loudness preset on the club track
        {
            p.loadPreset (9);
            p.prepareToPlay (kFs, kBlock);
            Track o3; run (p, in, &o3);
            save (o3, "/home/claude/test_out/club_loud_out.f32");
            std::cout << "  'Loud & Crispy' meter: integrated " << p.mIntegrated.load() << " LUFS\n";
        }
    }
    return 0;
}
