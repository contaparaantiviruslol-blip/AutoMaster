#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace am
{
// True-peak lookahead limiter (stereo-linked), runs at the plugin's normal sample rate AFTER the
// oversampled clipper/exciter stage has been band-limited. Peaks are detected on the signal and on
// 3 interpolated points between every pair of samples (4x, 8-tap windowed sinc), so inter-sample
// peaks are caught. Gain reduction = min-hold over the lookahead window followed by a box filter of
// the same length (the gain has fully arrived by the time the peak leaves the delay line), then an
// instant-attack / exponential-release follower. The gain is smooth, so it is applied at base rate.
class LookaheadLimiter
{
public:
    static constexpr int kTaps = 16, kCentre = 7;     // interpolator: 16-tap Kaiser-windowed sinc per phase
    static constexpr int kDetectDelay = kTaps - kCentre - 1 + 0;   // = 8 samples of look-ahead the interpolator needs

    void prepare (double sampleRate, double lookaheadMs)
    {
        sr = sampleRate;
        L = (int) std::ceil (lookaheadMs * 0.001 * sr);
        if (L < 2 * kTaps) L = 2 * kTaps + 1;
        R = L + kDetectDelay;                         // delay ring: delay = R - 1 = L - 1 + kDetectDelay
        dl.assign ((size_t) R, 0.f); dr.assign ((size_t) R, 0.f);
        need.assign ((size_t) L, 1.f); held.assign ((size_t) L, 1.f);
        wp = gp = 0; boxSum = (double) L; relState = 1.0; minGain = 1.f;

        auto bessel0 = [] (double x) { double sum = 1, term = 1; for (int k = 1; k < 30; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; } return sum; };
        const double beta = 7.0, half = 8.0, pi = 3.14159265358979323846;
        for (int ph = 0; ph < 3; ++ph)                // fractional positions 1/4, 1/2, 3/4
        {
            const double p = 0.25 * (ph + 1); double sum = 0;
            for (int i = 0; i < kTaps; ++i)
            {
                const double t = p - (i - kCentre);
                const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (pi * t) / (pi * t);
                const double r = std::min (1.0, std::abs (t) / half);
                const double win = bessel0 (beta * std::sqrt (1.0 - r * r)) / bessel0 (beta);
                interp[ph][i] = (float) (sinc * win); sum += interp[ph][i];
            }
            for (int i = 0; i < kTaps; ++i) interp[ph][i] = (float) (interp[ph][i] / sum);
        }
        setRelease (120.f); setCeiling (-1.f);
    }

    int getLatency() const { return R - 1; }

    void setCeiling (float dB)    { ceiling = std::pow (10.0f, (dB - 0.1f) / 20.0f); }   // 0.1 dB safety margin
    void setRelease (float ms)    { relCoef = 1.0 - std::exp (-1.0 / (std::max (1.0f, ms) * 0.001 * sr)); }

    float takeMaxGrDb()
    {
        const float g = minGain; minGain = 1.f;
        return g < 1.f ? -20.0f * std::log10 (g) : 0.f;
    }

    void process (float* l, float* r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            dl[(size_t) wp] = l[i]; dr[(size_t) wp] = r[i];          // newest sample = index wp

            // true-peak estimate around the sample that is kDetectDelay samples old
            float pk = 0.f;
            for (int c = 0; c < 2; ++c)
            {
                const std::vector<float>& d = c == 0 ? dl : dr;
                float sm[kTaps];
                for (int k = 0; k < kTaps; ++k) sm[k] = d[(size_t) ((wp - (kTaps - 1) + k + R) % R)];
                pk = std::max (pk, std::abs (sm[kCentre]));
                for (int ph = 0; ph < 3; ++ph)
                {
                    float v = 0.f; for (int k = 0; k < kTaps; ++k) v += sm[k] * interp[ph][k];
                    pk = std::max (pk, std::abs (v));
                }
            }
            need[(size_t) gp] = pk > ceiling ? ceiling / pk : 1.0f;

            float m = 1.0f;
            for (int k = 0; k < L; ++k) m = std::min (m, need[(size_t) k]);

            boxSum += (double) m - (double) held[(size_t) gp];
            held[(size_t) gp] = m;
            gp = (gp + 1) % L;

            const double s = boxSum / L;
            relState = s < relState ? s : relState + relCoef * (s - relState);   // instant attack, smooth release

            wp = (wp + 1) % R;                                                   // now points at the oldest sample
            const float g = (float) relState;
            l[i] = dl[(size_t) wp] * g;
            r[i] = dr[(size_t) wp] * g;
            minGain = std::min (minGain, g);
        }
    }

private:
    double sr = 44100.0, boxSum = 0, relState = 1.0, relCoef = 0.001;
    int L = 9, R = 13, wp = 0, gp = 0;
    float ceiling = 0.89f, minGain = 1.f;
    float interp[3][kTaps] {};
    std::vector<float> dl, dr, need, held;
};

// One band of the 3-band "glue" compressor. The threshold follows the band's own
// slow RMS level, so the same setting behaves sensibly at any input level.
class BandComp
{
public:
    void prepare (double sampleRate, double attackMs, double releaseMs)
    {
        atk  = 1.0 - std::exp (-1.0 / (attackMs  * 0.001 * sampleRate));
        rel  = 1.0 - std::exp (-1.0 / (releaseMs * 0.001 * sampleRate));
        slowFall = 1.0 - std::exp (-1.0 / (2.0  * sampleRate));
        slowRise = 1.0 - std::exp (-1.0 / (0.25 * sampleRate));
        env = 0; power = 1e-6;
    }

    // returns the linear gain to apply to this band
    inline double process (double l, double r, double ratio, double thresholdOffsetDb)
    {
        const double p = 0.5 * (l * l + r * r);
        power += (p > power ? slowRise : slowFall) * (p - power);

        const double lvlDb = 20.0 * std::log10 (std::max (std::abs (l), std::abs (r)) + 1e-9);
        const double thDb  = 10.0 * std::log10 (power + 1e-12) + thresholdOffsetDb;
        const double over  = lvlDb - thDb;
        const double slope = 1.0 - 1.0 / ratio;

        double gr = 0.0;
        if (2.0 * over > -kKnee)
            gr = 2.0 * over < kKnee ? slope * (over + kKnee / 2) * (over + kKnee / 2) / (2 * kKnee) : slope * over;

        env += (gr > env ? atk : rel) * (gr - env);
        return std::pow (10.0, -env / 20.0);
    }

private:
    static constexpr double kKnee = 6.0;
    double atk = 0.01, rel = 0.001, slowFall = 1e-5, slowRise = 1e-4, env = 0, power = 1e-6;
};
} // namespace am
