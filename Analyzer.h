#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <juce_dsp/juce_dsp.h>
#include "LoudnessMeter.h"

namespace am
{
struct Features
{
    bool  valid = false;
    float seconds = 0;                // analysed audio
    float lufs = -70.f, peakDb = -100.f, plr = 0.f;
    float bpm = 0.f, pulse = 0.f;     // tempo estimate, rhythmic regularity 0..1
    float octDb[9] {};                // per-octave energy (dB), 31 Hz .. 16 kHz
    float slope = 0.f;                // dB per octave, 125 Hz .. 16 kHz (more negative = darker)
    float bassBump = 0.f;             // dB: (31-125 Hz) minus (125-500 Hz), per-octave means
    float sideDb = -60.f;             // side/mid energy ratio, dB (higher = wider)
};

// Fed with the plugin INPUT while "Analyse" is running. Audio-thread side: reset()/process().
// Message-thread side: finalise(), only after the audio thread has stopped feeding it.
class Analyzer
{
public:
    static constexpr int specSize = 4096, specHop = 2048, fluxSize = 1024, fluxHop = 512;
    static constexpr int nOct = 9, maxFluxFrames = 30000;

    Analyzer() : specFFT (12), fluxFFT (10)
    {
        specWin.resize (specSize); fluxWin.resize (fluxSize);
        for (int i = 0; i < specSize; ++i) specWin[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * (float) kPi * (float) i / (float) specSize);
        for (int i = 0; i < fluxSize; ++i) fluxWin[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * (float) kPi * (float) i / (float) fluxSize);
        specBuf.assign (2 * specSize, 0.f); fluxBuf.assign (2 * fluxSize, 0.f);
        ring.assign (specSize, 0.f);
        prevMag.assign (fluxSize / 2 + 1, 0.f);
        fluxHist.reserve (maxFluxFrames);
    }

    void prepare (double sampleRate) { fs = sampleRate; loud.prepare (fs); reset(); }

    void reset()
    {
        loud.reset();
        std::fill (ring.begin(), ring.end(), 0.f);
        std::fill (prevMag.begin(), prevMag.end(), 0.f);
        fluxHist.clear();
        std::fill (std::begin (octE), std::end (octE), 0.0);
        specFrames = 0; havePrev = false; wp = 0; cntFlux = cntSpec = 0;
        peak = 0; sumM = sumS = 0; totalSamples = 0;
    }

    const std::vector<float>& fluxHistory() const { return fluxHist; }
    float seconds() const { return fs > 0 ? (float) ((double) totalSamples / fs) : 0.f; }

    void process (const float* l, const float* r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float a = l[i], b = r[i];
            peak = std::max ({ peak, std::abs (a), std::abs (b) });
            const double m = 0.5 * (a + b), s = 0.5 * (a - b);
            sumM += m * m; sumS += s * s;
            ring[(size_t) wp] = (float) m;
            wp = (wp + 1) & (specSize - 1);
            if (++cntFlux == fluxHop) { cntFlux = 0; fluxFrame(); }
            if (++cntSpec == specHop) { cntSpec = 0; specFrame(); }
        }
        loud.process (l, r, n);
        totalSamples += (uint64_t) n;
    }

    Features finalise() const
    {
        Features f;
        f.seconds = seconds();
        f.lufs    = loud.integrated();
        f.peakDb  = 20.0f * std::log10 (std::max (peak, 1e-6f));
        f.plr     = f.peakDb - f.lufs;
        f.sideDb  = (float) (10.0 * std::log10 ((sumS + 1e-12) / (sumM + 1e-12)));

        if (specFrames < 20 || f.lufs < -60.f || f.seconds < 8.f)   // too little (or silent) audio
            return f;

        for (int b = 0; b < nOct; ++b)
            f.octDb[b] = (float) (10.0 * std::log10 (octE[b] / specFrames + 1e-12));

        // spectral slope, 125 Hz .. 16 kHz (octave bands 2..8), least squares
        {
            double sx = 0, sy = 0, sxx = 0, sxy = 0; const int n = nOct - 2;
            for (int b = 2; b < nOct; ++b) { sx += b; sy += f.octDb[b]; sxx += (double) b * b; sxy += b * f.octDb[b]; }
            f.slope = (float) ((n * sxy - sx * sy) / (n * sxx - sx * sx));
        }
        f.bassBump = 0.5f * (f.octDb[0] + f.octDb[1]) - 0.5f * (f.octDb[2] + f.octDb[3]);

        estimateTempo (f);
        f.valid = true;
        return f;
    }

private:
    void fluxFrame()
    {
        for (int i = 0; i < fluxSize; ++i)
            fluxBuf[(size_t) i] = ring[(size_t) ((wp - fluxSize + i) & (specSize - 1))] * fluxWin[(size_t) i];
        std::fill (fluxBuf.begin() + fluxSize, fluxBuf.end(), 0.f);
        fluxFFT.performFrequencyOnlyForwardTransform (fluxBuf.data(), true);

        const int kLo = std::max (1, (int) (40.0 * fluxSize / fs)), kHi = std::min (fluxSize / 2, (int) (5000.0 * fluxSize / fs));
        float flux = 0.f;
        for (int k = kLo; k <= kHi; ++k)
        {
            const float mag = std::log1p (100.0f * fluxBuf[(size_t) k] / (fluxSize * 0.25f));
            flux += std::max (0.f, mag - prevMag[(size_t) k]);
            prevMag[(size_t) k] = mag;
        }
        if (havePrev && (int) fluxHist.size() < maxFluxFrames) fluxHist.push_back (flux);
        havePrev = true;
    }

    void specFrame()
    {
        double e = 0;
        for (int i = 0; i < specSize; ++i)
        {
            const float x = ring[(size_t) ((wp - specSize + i) & (specSize - 1))];
            e += (double) x * x;
            specBuf[(size_t) i] = x * specWin[(size_t) i];
        }
        if (e / specSize < 1e-6) return;                       // below about -60 dBFS: ignore silence

        std::fill (specBuf.begin() + specSize, specBuf.end(), 0.f);
        specFFT.performFrequencyOnlyForwardTransform (specBuf.data(), true);

        for (int k = 1; k <= specSize / 2; ++k)
        {
            const double f = k * fs / specSize;
            const int b = (int) std::floor (std::log2 (f / 31.25));
            if (b < 0 || b >= nOct) continue;
            const double mag = specBuf[(size_t) k] / (specSize * 0.25);
            octE[b] += mag * mag;
        }
        ++specFrames;
    }

    void estimateTempo (Features& f) const
    {
        const int N = (int) fluxHist.size();
        const double fps = fs / fluxHop;
        const int lagMin = std::max (2, (int) std::floor (fps * 60.0 / 200.0)), lagMax = (int) std::ceil (fps * 60.0 / 60.0);
        if (N < lagMax * 4) return;

        std::vector<double> x ((size_t) N);
        double mean = 0; for (float v : fluxHist) mean += v; mean /= N;
        for (int i = 0; i < N; ++i) x[(size_t) i] = fluxHist[(size_t) i] - mean;

        auto ac = [&] (int lag) { double s = 0; for (int i = 0; i + lag < N; ++i) s += x[(size_t) i] * x[(size_t) (i + lag)]; return s / (N - lag); };
        const double r0 = ac (0);
        if (r0 <= 1e-12) return;

        std::vector<double> r ((size_t) lagMax + 2, 0.0);
        for (int lag = lagMin - 1; lag <= lagMax + 1; ++lag) r[(size_t) lag] = ac (lag);

        int best = lagMin; double bestScore = -1e30;
        for (int lag = lagMin; lag <= lagMax; ++lag)
        {
            const double bpm = 60.0 * fps / lag;
            const double z = std::log2 (bpm / 120.0) / 0.8;             // mild prior around 120 BPM
            const double score = r[(size_t) lag] * std::exp (-0.5 * z * z);
            if (score > bestScore) { bestScore = score; best = lag; }
        }
        double lagF = best;                                              // parabolic refinement
        const double y0 = r[(size_t) best - 1], y1 = r[(size_t) best], y2 = r[(size_t) best + 1], den = y0 - 2 * y1 + y2;
        if (std::abs (den) > 1e-12) lagF += 0.5 * (y0 - y2) / den;

        f.bpm   = (float) (60.0 * fps / lagF);
        f.pulse = (float) std::clamp (r[(size_t) best] / r0, 0.0, 1.0);
    }

    juce::dsp::FFT specFFT, fluxFFT;
    LoudnessMeter loud;
    std::vector<float> specWin, fluxWin, specBuf, fluxBuf, ring, prevMag, fluxHist;
    double octE[nOct] {}, sumM = 0, sumS = 0, fs = 44100.0;
    float peak = 0;
    int specFrames = 0, wp = 0, cntFlux = 0, cntSpec = 0;
    bool havePrev = false;
    uint64_t totalSamples = 0;
};
} // namespace am
