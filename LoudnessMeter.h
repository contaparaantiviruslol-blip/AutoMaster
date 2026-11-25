#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include "Biquad.h"

namespace am
{
// Stereo K-weighted loudness meter. Owned and driven by ONE thread (the audio thread);
// results are published through atomics so the GUI can read them safely.
class LoudnessMeter
{
public:
    static constexpr int kBins = 800;            // 0.1 LU bins from -70 to +10 LUFS

    LoudnessMeter()
    {
        for (int i = 0; i < kBins; ++i)
            binPower[(size_t) i] = lufsToPower (binLufs (i));
    }

    void prepare (double sampleRate)
    {
        stage1.set (design::kShelf (sampleRate));
        stage2.set (design::kHighpass (sampleRate));
        subLen = (int) std::lround (0.1 * sampleRate);
        reset();
    }

    void reset()
    {
        stage1.reset(); stage2.reset();
        acc = 0; cnt = 0; ringPos = 0; numSub = 0;
        ring.fill (0.0);
        hist.fill (0);
        momentaryLufs.store (-100.f); shortTermLufs.store (-100.f); integratedLufs.store (-100.f);
    }

    void process (const float* l, const float* r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const double a = stage2.process (0, stage1.process (0, l[i]));
            const double b = stage2.process (1, stage1.process (1, r[i]));
            acc += a * a + b * b;
            if (++cnt >= subLen) { endSubBlock (acc / cnt); acc = 0; cnt = 0; }
        }
    }

    float momentary()  const { return momentaryLufs.load(); }
    float shortTerm()  const { return shortTermLufs.load(); }
    float integrated() const { return integratedLufs.load(); }

    static double powerToLufs (double p) { return p > 1e-12 ? -0.691 + 10.0 * std::log10 (p) : -100.0; }
    static double lufsToPower (double l)  { return std::pow (10.0, (l + 0.691) / 10.0); }

private:
    static double binLufs (int i) { return -70.0 + (i + 0.5) / 10.0; }

    double meanOfLast (int k) const
    {
        double s = 0;
        for (int i = 1; i <= k; ++i) s += ring[(size_t) ((ringPos - i + 30 * 4) % 30)];
        return s / k;
    }

    void endSubBlock (double p)
    {
        ring[(size_t) ringPos] = p;
        ringPos = (ringPos + 1) % 30;
        ++numSub;

        if (numSub >= 4)
        {
            const double m = meanOfLast (4);                        // 400 ms block, 75 % overlap
            const double L = powerToLufs (m);
            momentaryLufs.store ((float) L);
            if (L >= -70.0)
                ++hist[(size_t) std::min (kBins - 1, std::max (0, (int) std::floor ((L + 70.0) * 10.0)))];
        }
        if (numSub >= 30)
            shortTermLufs.store ((float) powerToLufs (meanOfLast (30)));

        integratedLufs.store ((float) computeIntegrated());
    }

    double computeIntegrated() const
    {
        double sum = 0; double n = 0;
        for (int i = 0; i < kBins; ++i) { sum += hist[(size_t) i] * binPower[(size_t) i]; n += hist[(size_t) i]; }
        if (n < 1) return -100.0;

        const double relGate = powerToLufs (sum / n) - 10.0;       // relative gate, -10 LU
        double sum2 = 0, n2 = 0;
        for (int i = 0; i < kBins; ++i)
            if (binLufs (i) >= relGate) { sum2 += hist[(size_t) i] * binPower[(size_t) i]; n2 += hist[(size_t) i]; }
        return n2 < 1 ? -100.0 : powerToLufs (sum2 / n2);
    }

    Biquad stage1, stage2;
    double acc = 0; int cnt = 0, subLen = 4410, ringPos = 0, numSub = 0;
    std::array<double, 30> ring {};
    std::array<unsigned, kBins> hist {};
    std::array<double, kBins> binPower {};
    std::atomic<float> momentaryLufs { -100.f }, shortTermLufs { -100.f }, integratedLufs { -100.f };
};
} // namespace am
