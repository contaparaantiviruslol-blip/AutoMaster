#pragma once
#include <cmath>

namespace am
{
constexpr double kPi = 3.14159265358979323846;

struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

// Allocation-free filter designs (RBJ cookbook) + the two ITU-R BS.1770 K-weighting stages.
namespace design
{
inline Coeffs make (double b0, double b1, double b2, double a0, double a1, double a2)
{
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

inline Coeffs lowpass (double fs, double f, double q)
{
    const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
    return make ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
}

inline Coeffs highpass (double fs, double f, double q)
{
    const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
    return make ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
}

inline Coeffs peaking (double fs, double f, double q, double dB)
{
    const double A = std::pow (10.0, dB / 40), w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
    return make (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
}

inline Coeffs lowShelf (double fs, double f, double q, double dB)
{
    const double A = std::pow (10.0, dB / 40), w = 2 * kPi * f / fs, c = std::cos (w);
    const double sq = 2 * std::sqrt (A) * (std::sin (w) / (2 * q));
    return make (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                 (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
}

inline Coeffs highShelf (double fs, double f, double q, double dB)
{
    const double A = std::pow (10.0, dB / 40), w = 2 * kPi * f / fs, c = std::cos (w);
    const double sq = 2 * std::sqrt (A) * (std::sin (w) / (2 * q));
    return make (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                 (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
}

// BS.1770 stage 1: high-shelf "head" filter
inline Coeffs kShelf (double fs)
{
    const double G = 3.999843853973347, Q = 0.7071752369554196, fc = 1681.974450955533;
    const double K = std::tan (kPi * fc / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
    const double a0 = 1.0 + K / Q + K * K;
    return { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
             2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
}

// BS.1770 stage 2: RLB high-pass
inline Coeffs kHighpass (double fs)
{
    const double Q = 0.5003270373253953, fc = 38.13547087613982;
    const double K = std::tan (kPi * fc / fs);
    const double a0 = 1.0 + K / Q + K * K;
    return { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
}
} // namespace design

// Two independent channel states, transposed direct form II, double precision.
struct Biquad
{
    Coeffs c;
    double z1[2] { 0, 0 }, z2[2] { 0, 0 };

    void set (const Coeffs& n) { c = n; }
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }

    inline double process (int ch, double x)
    {
        const double y = c.b0 * x + z1[ch];
        z1[ch] = c.b1 * x - c.a1 * y + z2[ch];
        z2[ch] = c.b2 * x - c.a2 * y;
        return y;
    }
};
} // namespace am
