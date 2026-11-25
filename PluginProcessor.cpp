#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace am;

namespace
{
constexpr int    kChunk = 2048;            // max samples handled per oversampling call
constexpr double kQ = 0.70710678118654752; // Butterworth
constexpr double kXover1 = 140.0, kXover2 = 3000.0;
constexpr double kExciterTargetU = 1.4;   // saturator input level relative to its knee, at the band's RMS
}

AutoMasterProcessor::AutoMasterProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout()),
      os (2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true)
{
    pTarget = apvts.getRawParameterValue (ids::target);
    pDrive  = apvts.getRawParameterValue (ids::drive);
    pLow    = apvts.getRawParameterValue (ids::low);
    pPres   = apvts.getRawParameterValue (ids::presence);
    pAir    = apvts.getRawParameterValue (ids::air);
    pCrisp  = apvts.getRawParameterValue (ids::crisp);
    pGlue   = apvts.getRawParameterValue (ids::glue);
    pWidth  = apvts.getRawParameterValue (ids::width);
    pClip   = apvts.getRawParameterValue (ids::clip);
    pCeil   = apvts.getRawParameterValue (ids::ceiling);
    pRel    = apvts.getRawParameterValue (ids::release);
}

bool AutoMasterProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AutoMasterProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;

    hpf.set (design::highpass (sr, 22.0, kQ));
    xLpA1.set (design::lowpass (sr, kXover1, kQ));  xLpA2.set (design::lowpass (sr, kXover1, kQ));
    xHpA1.set (design::highpass (sr, kXover1, kQ)); xHpA2.set (design::highpass (sr, kXover1, kQ));
    xLpB1.set (design::lowpass (sr, kXover2, kQ));  xLpB2.set (design::lowpass (sr, kXover2, kQ));
    xHpB1.set (design::highpass (sr, kXover2, kQ)); xHpB2.set (design::highpass (sr, kXover2, kQ));
    apLp1.set (design::lowpass (sr, kXover2, kQ));  apLp2.set (design::lowpass (sr, kXover2, kQ));
    apHp1.set (design::highpass (sr, kXover2, kQ)); apHp2.set (design::highpass (sr, kXover2, kQ));
    exciterHp.set (design::highpass (sr * 2.0, 4000.0, kQ));
    excCoef = 1.0 - std::exp (-1.0 / (0.1 * sr * 2.0));
    excMs[0] = excMs[1] = 1e-6; excCross[0] = excCross[1] = 0.0;

    for (auto* b : { &hpf, &eqLow, &eqPres, &eqAir, &xLpA1, &xLpA2, &xHpA1, &xHpA2, &xLpB1, &xLpB2, &xHpB1, &xHpB2,
                     &apLp1, &apLp2, &apHp1, &apHp2, &exciterHp })
        b->reset();
    lastLow = lastPres = lastAir = 1e9f;

    compLow.prepare (sr, 30.0, 200.0);
    compMid.prepare (sr, 15.0, 150.0);
    compHigh.prepare (sr, 5.0, 100.0);

    driveSm.reset (sr, 0.05);
    driveSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pDrive->load()));

    os.initProcessing ((size_t) kChunk);
    os.reset();
    limiter.prepare (sr, 1.5);
    limiter2.prepare (sr, 0.75);

    outMeter.prepare (sr);
    analyzer.prepare (sr);

    setLatencySamples ((int) std::ceil (os.getLatencyInSamples()) + limiter.getLatency() + limiter2.getLatency());
}

AutoMasterProcessor::Snapshot AutoMasterProcessor::readParams() const
{
    return { pDrive->load(), pLow->load(), pPres->load(), pAir->load(), pCrisp->load() / 100.f, pGlue->load() / 100.f,
             pWidth->load() / 100.f, pClip->load() / 100.f, pCeil->load(), pRel->load() };
}

void AutoMasterProcessor::updateFilters (const Snapshot& s)
{
    if (s.lowDb  != lastLow)  { eqLow.set  (design::lowShelf  (sr, 100.0,  0.7, s.lowDb));  lastLow  = s.lowDb; }
    if (s.presDb != lastPres) { eqPres.set (design::peaking   (sr, 3200.0, 0.7, s.presDb)); lastPres = s.presDb; }
    if (s.airDb  != lastAir)  { eqAir.set  (design::highShelf (sr, 9000.0, 0.6, s.airDb));  lastAir  = s.airDb; }
}

void AutoMasterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n == 0) return;
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    // analysis taps the plugin INPUT, before any processing
    if (cmdStart.exchange (false)) { analyzer.reset(); analysing.store (true); }
    if (cmdStop.exchange (false))  { analysing.store (false); analysisReady.store (true); }
    if (analysing.load())          { analyzer.process (L, R, n); mAnalysisSeconds.store (analyzer.seconds()); }
    if (resetMetersFlag.exchange (false)) { outMeter.reset(); mPeakDb.store (-100.f); mGrMax.store (0.f); }

    const Snapshot s = readParams();
    updateFilters (s);
    driveSm.setTargetValue (juce::Decibels::decibelsToGain (s.driveDb));
    limiter.setCeiling (s.ceilingDb);
    limiter.setRelease (s.releaseMs);
    limiter2.setCeiling (s.ceilingDb);
    limiter2.setRelease (40.f);

    for (int start = 0; start < n; start += kChunk)
        processChunk (L + start, R + start, std::min (kChunk, n - start), s);

    // meters
    outMeter.process (L, R, n);
    mMomentary.store (outMeter.momentary());
    mShort.store (outMeter.shortTerm());
    mIntegrated.store (outMeter.integrated());

    float pk = 0.f;
    for (int i = 0; i < n; ++i) pk = std::max ({ pk, std::abs (L[i]), std::abs (R[i]) });
    const float fall = 20.f * (float) n / (float) sr;
    const float pkDb = 20.f * std::log10 (pk + 1e-9f), curPk = mPeakDb.load();
    mPeakDb.store (pkDb > curPk ? pkDb : curPk - fall);
    const float gr = limiter.takeMaxGrDb() + limiter2.takeMaxGrDb(), curGr = mGrDb.load();
    mGrDb.store (gr > curGr ? gr : std::max (0.f, curGr - fall));
    if (gr > mGrMax.load()) mGrMax.store (gr);
}

void AutoMasterProcessor::processChunk (float* l, float* r, int len, const Snapshot& s)
{
    // ===== base rate: tone EQ -> 3-band glue compression -> bass-mono + width =====
    const double ratio = 1.0 + 2.2 * s.glue, thOff = 3.0 - 2.0 * s.glue;
    const bool glueOn = s.glue > 0.001f;

    for (int i = 0; i < len; ++i)
    {
        const double g = driveSm.getNextValue();
        double lo[2], mi[2], hi[2];

        for (int c = 0; c < 2; ++c)
        {
            double x = (c == 0 ? l[i] : r[i]) * g;
            x = eqAir.process (c, eqPres.process (c, eqLow.process (c, hpf.process (c, x))));
            const double low  = xLpA2.process (c, xLpA1.process (c, x));
            const double rest = xHpA2.process (c, xHpA1.process (c, x));
            mi[c] = xLpB2.process (c, xLpB1.process (c, rest));
            hi[c] = xHpB2.process (c, xHpB1.process (c, rest));
            lo[c] = apLp2.process (c, apLp1.process (c, low)) + apHp2.process (c, apHp1.process (c, low));   // phase-align low band
        }

        if (glueOn)
        {
            const double gl = compLow.process  (lo[0], lo[1], ratio, thOff);
            const double gm = compMid.process  (mi[0], mi[1], ratio, thOff);
            const double gh = compHigh.process (hi[0], hi[1], ratio, thOff);
            for (int c = 0; c < 2; ++c) { lo[c] *= gl; mi[c] *= gm; hi[c] *= gh; }
        }

        const double bass = 0.5 * (lo[0] + lo[1]);                                         // mono below ~140 Hz
        const double mM = 0.5 * (mi[0] + mi[1]), mS = 0.5 * (mi[0] - mi[1]) * s.width;
        const double hM = 0.5 * (hi[0] + hi[1]), hS = 0.5 * (hi[0] - hi[1]) * s.width;
        l[i] = (float) (bass + mM + mS + hM + hS);
        r[i] = (float) (bass + mM - mS + hM - hS);
    }

    // ===== 2x oversampled: exciter -> soft clipper, then true-peak limiter at the normal rate =====
    float* ch[2] = { l, r };
    juce::dsp::AudioBlock<float> block (ch, 2, (size_t) len);
    auto up = os.processSamplesUp (block);
    float* u[2] = { up.getChannelPointer (0), up.getChannelPointer (1) };
    const int m = (int) up.getNumSamples();

    if (s.crisp > 0.001f)   // level-adaptive harmonic exciter on the highs (> 4 kHz): the drive follows the band's RMS,
    {                       // so the amount of added harmonics is the same at any loudness. The saturator's linear part is subtracted,
                            // so only new harmonic content is added and the fundamental level stays put.
        const double amt = 2.5 * s.crisp;
        for (int i = 0; i < m; ++i)
            for (int c = 0; c < 2; ++c)
            {
                const double hp = exciterHp.process (c, u[c][i]);
                excMs[c] += excCoef * (hp * hp - excMs[c]);
                const double d = std::clamp (kExciterTargetU / std::sqrt (excMs[c] + 1e-12), 1.0, 60.0);
                const double sat = std::tanh (d * hp) / d;
                excCross[c] += excCoef * (sat * hp - excCross[c]);
                const double lin = std::clamp (excCross[c] / (excMs[c] + 1e-12), 0.0, 1.2);   // the saturator's linear gain
                u[c][i] += (float) (amt * (sat - lin * hp));                                    // add only the new harmonics
            }
    }

    if (s.clip > 0.001f)                       // soft-knee clipper: transparent below the knee, rounds peaks above it
    {
        const float ceilLin = std::pow (10.0f, s.ceilingDb / 20.0f);
        const float top = ceilLin * 1.2f, knee = top * (1.0f - 0.45f * s.clip), span = top - knee;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < m; ++i)
            {
                const float a = std::abs (u[c][i]);
                if (a > knee) u[c][i] = std::copysign (knee + span * std::tanh ((a - knee) / span), u[c][i]);
            }
    }

    os.processSamplesDown (block);                 // band-limits whatever the exciter/clipper generated
    limiter.process (l, r, len);                   // true-peak limiter at the normal rate
    limiter2.process (l, r, len);                  // fast second stage (tiny corrections only)

    for (int i = 0; i < len; ++i)              // last-resort safety, should never engage
    {
        l[i] = std::clamp (l[i], -1.0f, 1.0f);
        r[i] = std::clamp (r[i], -1.0f, 1.0f);
    }
}

// ---------------------------------------------------------------------------------------
void AutoMasterProcessor::setParam (const char* id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void AutoMasterProcessor::loadPreset (int index)
{
    index = juce::jlimit (0, kNumPresets - 1, index);
    const auto& p = kPresets[index];
    setParam (ids::preset, (float) index);
    setParam (ids::target, p.target);   setParam (ids::drive, p.drive);
    setParam (ids::low, p.low);         setParam (ids::presence, p.presence);
    setParam (ids::air, p.air);         setParam (ids::crisp, p.crisp);
    setParam (ids::glue, p.glue);       setParam (ids::width, p.width);
    setParam (ids::clip, p.clip);       setParam (ids::ceiling, p.ceiling);
    setParam (ids::release, p.release);
}

void AutoMasterProcessor::startAnalysis()
{
    cmdStop.store (false);
    analysisReady.store (false);
    cmdStart.store (true);
    analysisRequested.store (true);
}

void AutoMasterProcessor::stopAnalysis()
{
    analysisRequested.store (false);
    cmdStop.store (true);
}

bool AutoMasterProcessor::takeAnalysisResult (Features& out)
{
    if (! analysisReady.exchange (false)) return false;
    out = analyzer.finalise();
    return true;
}

juce::String AutoMasterProcessor::applyAnalysis (const Features& f)
{
    if (! f.valid)
        return "Not enough audio to analyse (" + juce::String (f.seconds, 1) + " s measured). Press Play in FL Studio and let "
               "10-60 s of the loudest part of the track run, then press 'Stop + apply'.";

    const auto g = classify (f);
    const bool sure = g.confidence >= 0.35f;
    const int idx = sure ? g.preset : 0;
    loadPreset (idx);
    const auto& p = kPresets[idx];

    const float airAdd = std::clamp (0.6f * (kRefSlope[idx] - f.slope), -2.f, 3.f);
    const float lowAdd = std::clamp (0.4f * (kRefBump[idx] - f.bassBump), -2.f, 2.f);
    const float drive  = std::clamp (p.target - f.lufs, -12.f, 24.f);
    setParam (ids::air, std::clamp (p.air + airAdd, 0.f, 6.f));
    setParam (ids::low, std::clamp (p.low + lowAdd, -6.f, 6.f));
    setParam (ids::drive, drive);
    resetMeters();

    juce::String r;
    r << "Genre guess: " << kPresets[g.preset].name << " (" << juce::String (g.confidence * 100.f, 0) << "% sure)"
      << ", runner-up: " << kPresets[g.runnerUp].name << ".";
    if (! sure) r << " Not sure enough, so I used the neutral 'Auto' preset.";
    r << "\nTempo about " << juce::String (f.bpm, 0) << " BPM, input " << juce::String (f.lufs, 1) << " LUFS, peak "
      << juce::String (f.peakDb, 1) << " dBFS, tilt " << juce::String (f.slope, 1) << " dB/oct, analysed "
      << juce::String (f.seconds, 0) << " s.";
    r << "\nApplied '" << p.name << "': target " << juce::String (p.target, 1) << " LUFS, drive "
      << juce::String (drive, 1) << " dB, low " << juce::String (p.low + lowAdd, 1) << " dB, air "
      << juce::String (p.air + airAdd, 1) << " dB.";
    r << "\nNext: play the whole track once, then press 'Match target' to fine-tune the level.";
    return r;
}

juce::String AutoMasterProcessor::matchTarget()
{
    const float out = mIntegrated.load();
    if (out < -60.f)
        return "No output measurement yet. Press 'Reset meters', play the whole track, then press 'Match target'.";

    const float target = pTarget->load();
    // When the limiter is working, only part of any extra drive turns into loudness (about 65 % in
    // tests), so ask for proportionally more. Without limiting, drive and loudness move 1:1.
    const float efficiency = mGrMax.load() > 1.0f ? 0.65f : 1.0f;
    const float delta = std::clamp ((target - out) / efficiency, -6.f, 6.f);
    setParam (ids::drive, std::clamp (pDrive->load() + delta, -12.f, 24.f));
    resetMeters();
    return "Measured " + juce::String (out, 1) + " LUFS, target " + juce::String (target, 1) + " LUFS: drive changed by "
           + juce::String (delta, 1) + " dB. Meters reset - play the track again to verify.";
}

// ---------------------------------------------------------------------------------------
void AutoMasterProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void AutoMasterProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* AutoMasterProcessor::createEditor() { return new AutoMasterEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AutoMasterProcessor(); }
