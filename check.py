"""Independent check of the DSP test renders (needs: pip install numpy scipy pyloudnorm)."""
import sys
import numpy as np
import pyloudnorm as pyln
from scipy.signal import resample_poly, welch

FS = 44100
D = "/home/claude/test_out/"


def load(name):
    return np.fromfile(D + name, dtype=np.float32).reshape(-1, 2).astype(np.float64)


def true_peak_db(x):
    up = resample_poly(x, 4, 1, axis=0)
    return 20 * np.log10(np.max(np.abs(up)) + 1e-12)


def octave_db(x):
    mono = x.mean(axis=1)
    f, p = welch(mono, FS, nperseg=8192)
    edges = [31.25 * 2 ** k for k in range(10)]
    return [10 * np.log10(p[(f >= a) & (f < b)].sum() + 1e-20) for a, b in zip(edges[:-1], edges[1:])]


def report(label, x, ref=None, skip=0):
    x = x[skip:]
    lufs = pyln.Meter(FS).integrated_loudness(x)
    sp = 20 * np.log10(np.max(np.abs(x)) + 1e-12)
    tp = true_peak_db(x)
    clipped = int(np.sum(np.abs(x) >= 0.9999))
    crest = sp - lufs
    print(f"  {label:<26} LUFS {lufs:6.2f} | sample peak {sp:6.2f} dBFS | true peak {tp:6.2f} dBTP | PLR {crest:5.1f} | clipped samples {clipped}")
    return lufs


for name in ["club", "hiphop", "acoustic"]:
    print(f"\n{name}")
    try:
        x = load(f"{name}_in.f32")
    except FileNotFoundError:
        print("  (missing)")
        continue
    report("input", x)
    for tag, lab in [("out1", "suggested settings"), ("out2", "after Match target")]:
        y = load(f"{name}_{tag}.f32")
        report(lab, y, skip=4000)
    if name == "club":
        y = load("club_loud_out.f32")
        report("Loud & Crispy preset", y, skip=4000)
        # spectral comparison input vs output (per-octave, relative to total)
        a, b = octave_db(x), octave_db(load("club_out2.f32")[4000:])
        print("  octave band change (output - input, dB, 31 Hz .. 16 kHz, after level-normalising):")
        off = np.mean(np.array(b) - np.array(a))
        print("   ", " ".join(f"{(bb - aa - off):+5.1f}" for aa, bb in zip(a, b)))
