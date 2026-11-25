#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class AutoMasterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AutoMasterEditor (AutoMasterProcessor&);
    ~AutoMasterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void addKnob (Knob&, const char* paramId, const juce::String& name);
    static void drawBar (juce::Graphics&, juce::Rectangle<float>, float value01, juce::Colour);

    AutoMasterProcessor& proc;

    juce::ComboBox presetBox;
    juce::TextButton analyseBtn { "Analyse" }, matchBtn { "Match target" }, resetBtn { "Reset meters" };
    juce::Label reportLabel;

    Knob kTarget, kDrive, kLow, kPres, kAir, kCrisp, kGlue, kWidth, kClip, kCeil, kRel;
    int waitTicks = 0;
    bool waiting = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoMasterEditor)
};
