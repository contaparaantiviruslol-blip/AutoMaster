"""Transparency / tone-control check on pink noise (needs numpy, scipy)."""
import numpy as np
from scipy.signal import welch
FS = 44100; LAT = 164; D = "/home/claude/test_out/"
ld = lambda n: np.fromfile(D + n, dtype=np.float32).reshape(-1, 2).astype(float)
x = ld("pink_in.f32")
edges = [31.25 * 2 ** k for k in range(10)]
def bands(a):
    f, p = welch(a.mean(axis=1), FS, nperseg=8192)
    return np.array([10 * np.log10(p[(f >= lo) & (f < hi)].sum() + 1e-20) for lo, hi in zip(edges[:-1], edges[1:])])
ref = bands(x[LAT + 20000:])
print("octave centres (Hz):       " + " ".join(f"{int(np.sqrt(a*b)):>6d}" for a, b in zip(edges[:-1], edges[1:])))
for tag in ["neutral", "air6", "crisp100", "low6", "pres4"]:
    y = ld(f"pink_{tag}.f32")
    d = bands(y[LAT + 20000:]) - ref
    print(f"{tag:<10s} gain vs input (dB): " + " ".join(f"{v:+6.2f}" for v in d))
# null test for neutral: align by latency, compare waveforms
y = ld("pink_neutral.f32"); a = x[20000:-LAT - 100]; b = y[20000 + LAT:-100]
n = min(len(a), len(b)); a, b = a[:n], b[:n]
res = a - b
print(f"\nneutral waveform residual {20*np.log10(np.sqrt((res**2).mean())/np.sqrt((a**2).mean())):.1f} dB (expected: not a null, because the crossover/high-pass shift phase; the magnitude above is what matters)")
