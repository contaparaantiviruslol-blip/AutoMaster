#pragma once
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Dsp/Analyzer.h"
#include "Dsp/Biquad.h"
#include "Dsp/Genre.h"
#include "Dsp/Limiter.h"
#include "Dsp/LoudnessMeter.h"
#include "Params.h"

class AutoMasterProcessor : public juce::AudioProcessor
{
public:
    AutoMasterProcessor();
    ~AutoMasterProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AutoMaster"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- message-thread API (editor) -------------------------------------------------
    void loadPreset (int index);
    void startAnalysis();
    void stopAnalysis();
    bool isAnalysing() const { return analysisRequested.load(); }
    bool takeAnalysisResult (am::Features& out);          // true once, after a stop
    juce::String applyAnalysis (const am::Features&);     // picks preset + sets parameters, returns a report
    juce::String matchTarget();                           // nudges Drive so the measured output hits Target
    void resetMeters() { resetMetersFlag = true; }

    // ---- meters (written by the audio thread, read by the GUI) ------------------------
    std::atomic<float> mMomentary { -100.f }, mShort { -100.f }, mIntegrated { -100.f };
    std::atomic<float> mPeakDb { -100.f }, mGrDb { 0.f }, mAnalysisSeconds { 0.f };
    std::atomic<float> mGrMax { 0.f };                          // largest limiter reduction since the last meter reset
    const am::Analyzer& getAnalyzer() const { return analyzer; }

    juce::AudioProcessorValueTreeState apvts;

private:
    struct Snapshot
    {
        float driveDb, lowDb, presDb, airDb, crisp, glue, width, clip, ceilingDb, releaseMs;
    };
    Snapshot readParams() const;
    void updateFilters (const Snapshot&);
    void processChunk (float* l, float* r, int len, const Snapshot&);
    void setParam (const char* id, float value);

    std::atomic<float> *pDrive {}, *pLow {}, *pPres {}, *pAir {}, *pCrisp {}, *pGlue {}, *pWidth {}, *pClip {}, *pCeil {}, *pRel {}, *pTarget {};

    double sr = 44100.0;
    float lastLow = 1e9f, lastPres = 1e9f, lastAir = 1e9f;

    am::Biquad hpf, eqLow, eqPres, eqAir;
    am::Biquad xLpA1, xLpA2, xHpA1, xHpA2, xLpB1, xLpB2, xHpB1, xHpB2, apLp1, apLp2, apHp1, apHp2;
    am::BandComp compLow, compMid, compHigh;
    juce::SmoothedValue<float> driveSm;

    juce::dsp::Oversampling<float> os;
    am::Biquad exciterHp;
    double excMs[2] { 1e-6, 1e-6 }, excCross[2] { 0, 0 }, excCoef = 1e-4;   // slow level of the exciter's high band (per channel)
    am::LookaheadLimiter limiter, limiter2;     // main limiter + fast second stage that catches residual inter-sample overshoot

    am::LoudnessMeter outMeter;
    am::Analyzer analyzer;

    std::atomic<bool> cmdStart { false }, cmdStop { false }, analysing { false }, analysisReady { false },
                      analysisRequested { false }, resetMetersFlag { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoMasterProcessor)
};
